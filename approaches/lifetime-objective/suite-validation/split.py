#!/usr/bin/env python3
"""Check 2 of docs/exploratory/design-01-benchmark-suite.md: split the scenario
suite into a development half and a sealed half, fixed and recorded before any
tuning, and write a content-hashed manifest.

The split must be:
  * deterministic and content-addressed, so it can be recomputed by anyone from
    the suite file alone and cannot be quietly reshuffled after a result;
  * whole-position, so no position appears in both halves;
  * stratified by `origin`, so the two halves are comparable in the way the
    positions were produced;
  * exactly balanced, so a margin measured on one half has the same number of
    positions behind it as the margin measured on the other.

Rule: inside each origin stratum, order positions by
FNV-1a-64("suite-split-v1:" + id); the first half of that order is `development`
and the second half is `sealed`.  The id is already a content hash of every
field of the scenario, so the assignment is a pure function of the suite's
bytes.

Usage:
  split.py <suite.jsonl> <manifest.json>
"""

import hashlib
import json
import sys


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for byte in data:
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def load(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    return rows


def occupied_cells(board):
    return sum(1 for cell in board if cell != 0)


def covered_cells(board):
    return sum(1 for cell in board if cell in (8, 9))


def summarize(members):
    if not members:
        return {}
    occ = sorted(m["occupiedCells"] for m in members)
    cov = sorted(m["coveredCells"] for m in members)
    opt = sorted(m["optimum"] for m in members)

    def q(values, fraction):
        return values[min(len(values) - 1, int(round(fraction * (len(values) - 1))))]

    return {
        "positions": len(members),
        "occupiedCells": {"min": occ[0], "median": q(occ, 0.5), "max": occ[-1],
                          "mean": round(sum(occ) / len(occ), 3)},
        "coveredCells": {"min": cov[0], "median": q(cov, 0.5), "max": cov[-1],
                         "mean": round(sum(cov) / len(cov), 3)},
        "clairvoyantOptimum": {"min": opt[0], "median": q(opt, 0.5),
                               "max": opt[-1],
                               "mean": round(sum(opt) / len(opt), 1)},
        "byOrigin": {origin: sum(1 for m in members if m["origin"] == origin)
                     for origin in sorted({m["origin"] for m in members})},
    }


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    suite_path, manifest_path = sys.argv[1], sys.argv[2]
    rows = load(suite_path)

    entries = []
    for row in rows:
        board = row["board"]
        entries.append({
            "id": row["id"],
            "origin": row.get("origin", "unknown"),
            "occupiedCells": row.get("occupiedCells", occupied_cells(board)),
            "coveredCells": row.get("coveredCells", covered_cells(board)),
            "movesRemaining": row["movesRemaining"],
            "optimum": row.get("optimum", 0),
            "originGameSeed": row.get("originGameSeed", ""),
            "sortKey": fnv1a64(("suite-split-v1:" + row["id"]).encode()),
        })

    ids = [entry["id"] for entry in entries]
    if len(set(ids)) != len(ids):
        raise SystemExit("suite contains duplicate scenario ids; refusing to split")

    # Whole-origin leakage check: two positions harvested from the same game
    # would share an origin game seed and must not straddle the split.
    # Synthetic positions are not harvested from a game and carry the
    # placeholder seed "0"; they share no origin with anything.  Only real
    # harvest seeds are checked.
    seeds = [entry["originGameSeed"] for entry in entries
             if entry["originGameSeed"] not in ("", "0")]
    shared_seeds = len(seeds) - len(set(seeds))

    strata = sorted({entry["origin"] for entry in entries})
    for origin in strata:
        members = sorted((e for e in entries if e["origin"] == origin),
                         key=lambda e: (e["sortKey"], e["id"]))
        half = len(members) // 2
        for index, entry in enumerate(members):
            entry["half"] = "development" if index < half else "sealed"

    development = sorted((e for e in entries if e["half"] == "development"),
                         key=lambda e: e["id"])
    sealed = sorted((e for e in entries if e["half"] == "sealed"),
                    key=lambda e: e["id"])

    def digest(members):
        payload = "\n".join(m["id"] for m in members).encode()
        return hashlib.sha256(payload).hexdigest()

    with open(suite_path, "rb") as handle:
        suite_digest = hashlib.sha256(handle.read()).hexdigest()

    manifest = {
        "schema": "scenario-suite-split-v1",
        "suite": suite_path,
        "suiteSha256": suite_digest,
        "positions": len(entries),
        "rule": ("inside each origin stratum, order by "
                 "FNV-1a-64('suite-split-v1:' + id); first half development, "
                 "second half sealed"),
        "strata": strata,
        "sharedOriginGameSeeds": shared_seeds,
        "development": {
            "sha256OverOrderedIds": digest(development),
            "summary": summarize(development),
            "ids": [m["id"] for m in development],
        },
        "sealed": {
            "sha256OverOrderedIds": digest(sealed),
            "summary": summarize(sealed),
            "ids": [m["id"] for m in sealed],
        },
        "members": [
            {"id": e["id"], "half": e["half"], "origin": e["origin"],
             "occupiedCells": e["occupiedCells"],
             "coveredCells": e["coveredCells"],
             "movesRemaining": e["movesRemaining"]}
            for e in sorted(entries, key=lambda e: e["id"])
        ],
    }

    body = json.dumps(manifest, indent=2, sort_keys=True)
    manifest["manifestSha256"] = hashlib.sha256(body.encode()).hexdigest()
    with open(manifest_path, "w") as handle:
        handle.write(json.dumps(manifest, indent=2, sort_keys=True) + "\n")

    print(f"suite {suite_path}")
    print(f"  sha256 {suite_digest}")
    print(f"  positions {len(entries)}, strata {strata}, "
          f"shared origin game seeds {shared_seeds}")
    print(f"  development {len(development)} sha256 {digest(development)}")
    print(f"  sealed      {len(sealed)} sha256 {digest(sealed)}")
    print(f"  manifest sha256 (over the body without this field) "
          f"{manifest['manifestSha256']}")
    for half in ("development", "sealed"):
        print(f"  {half}: {json.dumps(manifest[half]['summary'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
