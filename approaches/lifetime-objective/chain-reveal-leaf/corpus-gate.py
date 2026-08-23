#!/usr/bin/env python3
"""Seed-free corpus gate for the reveal-construction leaf terms
(EX-20260823-reveal-construction-screen-371fd638, first passCriteria entry).

Reuses approaches/lifetime-objective/entombed-discs/analyze.py (ols_r2, the
held-out partial-correlation recipe) and afterstate-net/dataset.py
(split_by_origin: whole-origin split, never by state row).  Reads the
already-opened training corpus and the dump written by corpus-dump.cpp; opens
no seed.

Depth-4 subset for (a)-(c): behaviorDepth == 4, explored == 0, not censored,
valid leaf (same filters as analyze.py's depth4only fit).  The regression base
is the 18 frozen features + occupancy + rise clock + intercept, standardised on
the depth-4 training rows; y = log1p(movesToDeath).
  (a) held-out partial r: residualise y and the term on the base (fit on train
      rows), correlate the residuals on test rows;
  (b) incremental held-out R^2: R^2(base + term) - R^2(base), both fit on the
      depth-4 train rows and scored on the depth-4 test rows;
  (c) prevalence: fraction of depth-4 rows with the term > 0;
  (d) uncollected-setup rate (aligned_double_hit; analogous for
      chain_to_crack_solid): over (position, kSolid cell) pairs at non-explored
      depth-4 positions whose best support-disjoint pair product >= 0.5, the
      fraction where the tracked cell is NOT numbered at either of the next two
      positions of the same game.  Between consecutive positions the cell
      moves up one row when movesRemaining == 1 at the earlier position (a
      rise); a cell pushed off the top counts as not revealed.  Setups with no
      following position in the game are excluded and counted.  "Not revealed"
      is split into still-covered (S/C at both later positions) and
      ambiguous (the cell became empty without ever showing a number -- the
      disc fell or was cleared after a reveal we did not observe); the gating
      rate counts both as not revealed, per the experiment record.

Usage: corpus-gate.py <corpus.states> <dump-dir> <out-dir>
"""
from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "approaches/lifetime-objective/afterstate-net"))
sys.path.insert(0, str(ROOT / "approaches/lifetime-objective/entombed-discs"))
import dataset as ds  # noqa: E402
import analyze as ent  # noqa: E402

EXTRA = ["aligned_double_hit", "chain_to_crack_cracked", "chain_to_crack_solid", "entombed_high",
         "aligned_double_hit_gated", "chain_to_crack_cracked_gated", "chain_to_crack_solid_gated"]
GATED = "aligned_double_hit"
REPORTED = ["aligned_double_hit", "chain_to_crack_cracked", "chain_to_crack_solid"]
THRESHOLDS = {"partialR": 0.05, "incrementalR2": 0.005, "prevalence": 0.05, "uncollectedRate": 0.30}
B = 7


def uncollected(rec4, setups, which, explored4, threshold=0.5):
    """rec4: depth-4 records (all, including explored) in corpus order; setups: (N,98)."""
    seeds = np.asarray(rec4["gameSeed"]).astype(np.int64)
    moves = np.asarray(rec4["moveIndex"]).astype(np.int64)
    order = np.lexsort((moves, seeds))
    seeds, moves = seeds[order], moves[order]
    boards = np.asarray(rec4["board"])[order].reshape(-1, B, B)
    rise = np.asarray(rec4["movesRemaining"])[order] == 1
    prod = np.asarray(setups[:, 49 * which:49 * which + 49])[order].reshape(-1, B, B)
    expl = explored4[order]
    n = len(seeds)
    # consecutive-position validity: same game and moveIndex + k
    def follows(k):
        ok = np.zeros(n, dtype=bool)
        ok[:n - k] = (seeds[k:] == seeds[:-k]) & (moves[k:] == moves[:-k] + k)
        return ok
    f1, f2 = follows(1), follows(2)
    pos, row, col = np.nonzero(prod >= threshold)
    live = ~expl[pos]
    pos, row, col = pos[live], row[live], col[live]
    total = len(pos)
    has1 = f1[pos]
    tracked = has1
    out = {"setups": int(total), "noFollowingPosition": int((~has1).sum()), "tracked": int(tracked.sum())}
    pos, row, col = pos[tracked], row[tracked], col[tracked]
    # position t+1
    r1 = row - rise[pos].astype(np.int64)
    off1 = r1 < 0
    v1 = np.where(off1, 255, boards[pos + 1, np.clip(r1, 0, B - 1), col])
    has2 = f2[pos]
    r2 = r1 - rise[pos + 1].astype(np.int64)
    off2 = off1 | (r2 < 0) | ~has2
    v2 = np.where(off2, 255, boards[np.minimum(pos + 2, len(boards) - 1), np.clip(r2, 0, B - 1), col])
    numbered = lambda v: (v >= 1) & (v <= 7)
    covered = lambda v: (v == 8) | (v == 9)
    revealed = numbered(v1) | (has2 & numbered(v2))
    empty_seen = (v1 == 0) | (has2 & (v2 == 0))
    ambiguous = ~revealed & empty_seen
    still_covered = ~revealed & ~ambiguous
    out.update({
        "revealedWithin2": int(revealed.sum()),
        "stillCoveredWithin2": int(still_covered.sum()),
        "ambiguousEmptyWithin2": int(ambiguous.sum()),
        "pushedOffBoard": int(((off1 | (has2 & (r2 < 0))) & ~revealed).sum()),
        "onlyOneFollowingPosition": int((~has2).sum()),
        "uncollectedRate": float((~revealed).sum() / max(len(pos), 1)),
        "uncollectedRateExcludingAmbiguous": float(still_covered.sum() / max((still_covered | revealed).sum(), 1)),
        "threshold": threshold,
    })
    return out


def main() -> None:
    corpus, dump_dir, out_dir = sys.argv[1:4]
    dump_dir, out_dir = Path(dump_dir), Path(out_dir)
    records = ds.load_states(corpus)
    leaf_all = np.memmap(dump_dir / "leaf.f32", dtype=np.float32, mode="r").reshape(-1, 26)
    assert leaf_all.shape[0] == len(records), (leaf_all.shape, len(records))
    depth_all = np.asarray(records["behaviorDepth"])
    explored_all = np.asarray(records["explored"]) != 0
    valid = np.isfinite(leaf_all[:, 18]) & ~explored_all
    idx = np.flatnonzero(valid)
    rec = records[idx]
    leaf = np.asarray(leaf_all[idx])
    depth = depth_all[idx]
    mtd = np.asarray(rec["movesToDeath"]).astype(np.float64)
    censored = np.asarray(rec["censoredGame"]) == 1
    occ = np.asarray(rec["occupiedCells"]).astype(np.float64)
    clock = np.asarray(rec["movesRemaining"]).astype(np.float64)
    train_mask, val_mask, test_mask = ds.split_by_origin(rec)  # whole-origin split, as entombed-discs
    keep = ~censored
    sub = depth == 4
    tr, te = train_mask & keep & sub, test_mask & keep & sub
    y = np.log1p(mtd)
    base = np.column_stack([leaf[:, :18], occ, clock, np.ones(len(rec))])
    mu, sd = base[tr].mean(axis=0), base[tr].std(axis=0) + 1e-9
    Xs = (base - mu) / sd
    Xs[:, -1] = 1.0
    base_r2, base_r = ent.ols_r2(Xs[tr], y[tr], Xs[te], y[te])
    beta_y, *_ = np.linalg.lstsq(Xs[tr], y[tr], rcond=None)
    res_y = y[te] - Xs[te] @ beta_y

    out = {"format": "drop7-chain-reveal-corpus-gate-v1", "experimentId": "EX-20260823-reveal-construction-screen-371fd638",
           "corpus": str(corpus), "generatedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "records": int(len(records)), "validNonExplored": int(len(rec)), "depth4Rows": int(sub.sum()),
           "depth4Train": int(tr.sum()), "depth4Test": int(te.sum()), "depth4Games": int(len(np.unique(np.asarray(rec["gameSeed"])[sub]))),
           "baseHeldOutR2": base_r2, "baseHeldOutPearson": base_r, "thresholds": THRESHOLDS, "gatedTerm": GATED, "terms": {}}
    for k, name in enumerate(EXTRA):
        z = leaf[:, 19 + k].astype(np.float64)
        beta_z, *_ = np.linalg.lstsq(Xs[tr], z[tr], rcond=None)
        res_z = z[te] - Xs[te] @ beta_z
        partial = float(np.corrcoef(res_y, res_z)[0, 1]) if res_z.std() > 0 else 0.0
        zs = (z - z[tr].mean()) / (z[tr].std() + 1e-9)
        full = np.column_stack([Xs, zs])
        full_r2, _ = ent.ols_r2(full[tr], y[tr], full[te], y[te])
        out["terms"][name] = {"partialR": partial, "incrementalR2": full_r2 - base_r2, "fullHeldOutR2": full_r2,
                              "prevalence": float((z[sub] > 0).mean()), "meanDepth4": float(z[sub].mean()),
                              "meanAllDepths": float(z.mean()), "prevalenceAllDepths": float((z > 0).mean())}

    # (d) uncollected-setup rates on the depth-4 rows (all, for tracking)
    d4 = np.flatnonzero(depth_all == 4)
    setup_index = np.fromfile(dump_dir / "setups-index.u32", dtype=np.uint32)
    assert np.array_equal(setup_index, d4.astype(np.uint32)), "setup index does not match depth-4 rows"
    setups = np.memmap(dump_dir / "setups.f32", dtype=np.float32, mode="r").reshape(-1, 98)
    rec4 = records[d4]
    out["terms"]["aligned_double_hit"]["uncollected"] = uncollected(rec4, setups, 0, explored_all[d4])
    out["terms"]["chain_to_crack_solid"]["uncollected"] = uncollected(rec4, setups, 1, explored_all[d4])

    # evaluation
    lines = [f"corpus gate for {GATED} (EX-20260823-reveal-construction-screen-371fd638, passCriteria[0])",
             f"corpus {corpus}: {len(records)} records; depth-4 subset {int(sub.sum())} rows / {out['depth4Games']} games; train {int(tr.sum())} test {int(te.sum())} (whole-origin split)",
             f"base held-out R^2 (18 features + occupancy + rise clock, depth 4) = {base_r2:.4f}", ""]
    verdicts = {}
    for name in REPORTED:
        t = out["terms"][name]
        checks = {"partialR": t["partialR"] >= THRESHOLDS["partialR"], "incrementalR2": t["incrementalR2"] >= THRESHOLDS["incrementalR2"],
                  "prevalence": t["prevalence"] >= THRESHOLDS["prevalence"]}
        unc = t.get("uncollected")
        if unc is not None:
            checks["uncollectedRate"] = unc["uncollectedRate"] >= THRESHOLDS["uncollectedRate"]
        passed = all(checks.values())
        verdicts[name] = {"checks": checks, "pass": passed, "gating": name == GATED}
        tag = "GATING" if name == GATED else "reported only"
        lines.append(f"{name} [{tag}]: {'PASS' if passed else 'FAIL'}")
        lines.append(f"  partial r        = {t['partialR']:+.4f}  (>= +{THRESHOLDS['partialR']:.2f}) {'ok' if checks['partialR'] else 'FAIL'}")
        lines.append(f"  incremental R^2  = {t['incrementalR2']:+.5f}  (>= {THRESHOLDS['incrementalR2']}) {'ok' if checks['incrementalR2'] else 'FAIL'}")
        lines.append(f"  prevalence       = {100 * t['prevalence']:.2f}%  (>= 5%) {'ok' if checks['prevalence'] else 'FAIL'}")
        if unc is not None:
            lines.append(f"  uncollected rate = {100 * unc['uncollectedRate']:.2f}%  (>= 30%) {'ok' if checks['uncollectedRate'] else 'FAIL'}"
                         f"   [setups {unc['setups']}, tracked {unc['tracked']}, revealed {unc['revealedWithin2']}, still covered {unc['stillCoveredWithin2']},"
                         f" ambiguous-empty {unc['ambiguousEmptyWithin2']}, pushed off board {unc['pushedOffBoard']}, no following position {unc['noFollowingPosition']};"
                         f" excluding ambiguous {100 * unc['uncollectedRateExcludingAmbiguous']:.2f}%]")
        else:
            lines.append("  uncollected rate = n/a (not defined for this term)")
    for name in EXTRA:
        if name in REPORTED:
            continue
        t = out["terms"][name]
        lines.append(f"{name} [informational]: partial r {t['partialR']:+.4f}, incremental R^2 {t['incrementalR2']:+.5f}, prevalence {100 * t['prevalence']:.2f}%")
    gate_pass = verdicts[GATED]["pass"]
    lines.append("")
    lines.append(f"GATE {'PASS' if gate_pass else 'FAIL'}: {GATED} {'meets' if gate_pass else 'does not meet'} all four preregistered thresholds")
    out["verdicts"] = verdicts
    out["gatePass"] = gate_pass
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "corpus-gate.json").write_text(json.dumps(out, indent=2) + "\n")
    (out_dir / "corpus-gate.log").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
