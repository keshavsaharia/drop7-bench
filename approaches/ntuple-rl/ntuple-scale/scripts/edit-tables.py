#!/usr/bin/env python3
"""Write the two no-training edits of a frozen n-tuple table file.

An entry that training never touched still holds the optimistic starting
value (optimistic_total / active_count, 20/74 rise units for the first
experiment's layout).  Whether an entry was touched is read from the frozen
file alone: an entry is untouched when its stored value equals the starting
value bit for bit (a touched entry moves by beta * alpha * delta / n on its
first update, which is never exactly zero in practice; a touched entry that
returned to the starting value exactly is treated as untouched, and the count
of such entries is reported by comparing against a checkpoint's accumulators
when one is given).

Two edits, each written as a complete table file with the same header:

  zeroed     every untouched entry becomes 0.0: an unfamiliar board loses
             the starting value once per untouched entry it reads, so
             unfamiliar boards are valued pessimistically instead of like a
             fresh board;
  classmean  every untouched entry becomes the mean of the touched entries in
             its class (same table, same phase slab, same number of occupied
             cells in the pattern): an unfamiliar board is valued like a
             typical familiar board of the same shape.  A class with no
             touched entry keeps the starting value (reported).

The layout must be rows,cols,win23,win32,phase=all (the first experiment's).
Nothing here reads a seed or plays a game.

Usage: edit-tables.py --source best-weights.bin --out-dir DIR [--checkpoint checkpoint.bin]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import time

import numpy as np

LINE = 10_000_000
WINDOW = 1_000_000
PHASES = 5
LAYOUT = "rows,cols,win23,win32,phase=all"
ACTIVE = 74
OPTIMISTIC = np.float32(20.0) / np.float32(ACTIVE)


def read_header(path):
    with open(path, "rb") as handle:
        magic = handle.read(8)
        if magic != b"D7NTUP01":
            raise SystemExit(f"{path}: not a D7NTUP01 file")
        spec_len = int.from_bytes(handle.read(4), "little")
        spec = handle.read(spec_len).decode()
        count = int.from_bytes(handle.read(8), "little")
        flag = handle.read(1)[0]
        header = 8 + 4 + spec_len + 8 + 1
    return spec, count, flag, header


def occupied_digits(patterns, digits):
    idx = np.arange(patterns, dtype=np.int64)
    occ = np.zeros(patterns, dtype=np.int8)
    for k in range(digits):
        occ += ((idx // 10**k) % 10 != 0).astype(np.int8)
    return occ


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--checkpoint", default=None, help="checkpoint.bin with accumulators, to report how many touched entries sit exactly at the starting value")
    args = parser.parse_args()
    spec, count, flag, header = read_header(args.source)
    if spec != LAYOUT:
        raise SystemExit(f"layout {spec!r} is not {LAYOUT!r}")
    if flag != 0:
        raise SystemExit("source must be a frozen (weights-only) file")
    families = [("rows", 7, LINE, 7), ("cols", 7, LINE, 7), ("win23", 30, WINDOW, 6), ("win32", 30, WINDOW, 6)]
    expected = sum(t * PHASES * p for _, t, p, _ in families)
    if count != expected:
        raise SystemExit(f"entry count {count} != {expected}")
    started = time.time()
    weights = np.fromfile(args.source, dtype=np.float32, offset=header, count=count)
    assert weights.shape[0] == count
    init_bits = OPTIMISTIC.view(np.uint32)
    untouched = weights.view(np.uint32) == init_bits
    report = {
        "format": "drop7-ntuple-scale-edits-v1",
        "source": os.path.abspath(args.source),
        "sourceSha256": sha256_file(args.source),
        "layout": spec,
        "entries": int(count),
        "optimisticStart": float(OPTIMISTIC),
        "untouchedEntries": int(untouched.sum()),
        "touchedEntries": int(count - untouched.sum()),
        "families": {},
    }
    if args.checkpoint:
        cspec, ccount, cflag, cheader = read_header(args.checkpoint)
        if cspec != spec or ccount != count or cflag != 1:
            raise SystemExit("checkpoint does not match the source layout")
        acc = np.memmap(args.checkpoint, dtype=np.float32, mode="r", offset=cheader + 8 * count, shape=(count,))
        touched_at_start = 0
        never_updated_off_start = 0
        step = 1 << 26
        for s in range(0, count, step):
            a = np.asarray(acc[s : s + step]) > 0
            u = untouched[s : s + step]
            touched_at_start += int((a & u).sum())
            never_updated_off_start += int((~a & ~u).sum())
        report["checkpoint"] = {
            "path": os.path.abspath(args.checkpoint),
            "touchedEntriesAtStartValue": touched_at_start,
            "neverUpdatedEntriesOffStartValue": never_updated_off_start,
            "note": "the checkpoint is the end of training, not the candidate's point; an entry touched only after the candidate's point is untouched in the candidate and counted as such",
        }
    zeroed = weights.copy()
    zeroed[untouched] = np.float32(0.0)
    classmean = weights.copy()
    base = 0
    classes_empty = 0
    for name, tables, patterns, digits in families:
        occ = occupied_digits(patterns, digits)
        fam = {"tables": tables, "patterns": patterns, "untouchedByOccupied": [0] * (digits + 1), "patternsByOccupied": [int((occ == k).sum()) for k in range(digits + 1)], "touchedMeanByOccupied": [None] * (digits + 1)}
        sums = np.zeros(digits + 1)
        counts = np.zeros(digits + 1, dtype=np.int64)
        for t in range(tables):
            for ph in range(PHASES):
                s = base + (t * PHASES + ph) * patterns
                w = weights[s : s + patterns]
                u = untouched[s : s + patterns]
                for k in range(digits + 1):
                    m = occ == k
                    tk = m & ~u
                    uk = m & u
                    nt = int(tk.sum())
                    nu = int(uk.sum())
                    fam["untouchedByOccupied"][k] += nu
                    if nt:
                        mean = float(w[tk].mean())
                        sums[k] += w[tk].sum()
                        counts[k] += nt
                        if nu:
                            classmean[s : s + patterns][uk] = np.float32(mean)
                    elif nu:
                        classes_empty += 1
        fam["touchedMeanByOccupied"] = [float(sums[k] / counts[k]) if counts[k] else None for k in range(digits + 1)]
        report["families"][name] = fam
        base += tables * PHASES * patterns
        print(f"{name}: done at {time.time() - started:.0f} s", flush=True)
    report["classesWithNoTouchedEntry"] = classes_empty
    os.makedirs(args.out_dir, exist_ok=True)
    with open(args.source, "rb") as handle:
        head = handle.read(header)
    outputs = {}
    for label, table in (("zeroed", zeroed), ("classmean", classmean)):
        path = os.path.join(args.out_dir, f"{label}-weights.bin")
        tmp = path + ".tmp"
        with open(tmp, "wb") as handle:
            handle.write(head)
            table.tofile(handle)
        os.replace(tmp, path)
        digest = sha256_file(path)
        with open(os.path.join(args.out_dir, f"{label}-weights.sha256"), "w", encoding="utf-8") as handle:
            handle.write(f"{digest}  {path}\n")
        changed = int((table.view(np.uint32) != weights.view(np.uint32)).sum())
        outputs[label] = {"path": path, "sha256": digest, "entriesChanged": changed, "entriesChangedOnlyWhereUntouched": bool(((table.view(np.uint32) != weights.view(np.uint32)) & ~untouched).sum() == 0)}
        print(f"{label}: {changed} entries changed, sha256 {digest}", flush=True)
    report["outputs"] = outputs
    report["wallSeconds"] = round(time.time() - started, 1)
    with open(os.path.join(args.out_dir, "edits.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("families",)}, indent=1))


if __name__ == "__main__":
    main()
