#!/usr/bin/env python3
"""Human-observed 'danger patterns' checked against the corpus the same way the
entombed disc was: prevalence in depth-4 play, occupancy-matched lifetime, and
held-out partial correlation with log remaining lifetime AFTER the eighteen
frozen leaf features, occupancy and the rise clock.  Diagnostic only.

Usage: patterns.py <corpus.states> <leaf.f32> <out.json>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "approaches/lifetime-objective/afterstate-net"))
import dataset as ds  # noqa: E402

B = 7


def neighbours(mask: np.ndarray) -> np.ndarray:
    """Count of 4-neighbours of each cell for which mask is true."""
    n = np.zeros(mask.shape, dtype=np.int8)
    n[:, 1:, :] += mask[:, :-1, :]
    n[:, :-1, :] += mask[:, 1:, :]
    n[:, :, 1:] += mask[:, :, :-1]
    n[:, :, :-1] += mask[:, :, 1:]
    return n


def pattern_features(boards: np.ndarray) -> dict[str, np.ndarray]:
    n = boards.shape[0]
    g = boards.reshape(n, B, B)
    solid, cracked = g == 8, g == 9
    cover = solid | cracked
    occupied = g != 0
    adj_solid = neighbours(solid)
    adj_cover = neighbours(cover)
    adj_occ = neighbours(occupied)
    height = occupied.sum(axis=1)
    col_height = np.broadcast_to(height[:, None, :], (n, B, B))
    out = {}
    for v in (1, 2, 3, 4):
        cell = g == v
        out[f"v{v}_adj_solid"] = ((cell & (adj_solid > 0))).sum(axis=(1, 2)).astype(np.float32)
        out[f"v{v}_adj_cover"] = ((cell & (adj_cover > 0))).sum(axis=(1, 2)).astype(np.float32)
    ones = g == 1
    # a 1 with any occupied neighbour in its row, or a column taller than 1, cannot clear now
    left = np.zeros_like(occupied); left[:, :, 1:] = occupied[:, :, :-1]
    right = np.zeros_like(occupied); right[:, :, :-1] = occupied[:, :, 1:]
    out["ones_blocked"] = (ones & ((left | right) | (col_height > 1))).sum(axis=(1, 2)).astype(np.float32)
    out["ones_under_cover"] = (ones & (np.roll(cover, 1, axis=1) & (np.arange(B)[None, :, None] > 0))).sum(axis=(1, 2)).astype(np.float32)
    out["numbered_adj_solid_total"] = (((g >= 1) & (g <= 7)) & (adj_solid > 0)).sum(axis=(1, 2)).astype(np.float32)
    out["solid_count"] = solid.sum(axis=(1, 2)).astype(np.float32)
    out["isolated_numbered"] = (((g >= 1) & (g <= 7)) & (adj_occ == 0)).sum(axis=(1, 2)).astype(np.float32)
    return out


def main() -> None:
    corpus, leaf_path, out_path = sys.argv[1:4]
    records = ds.load_states(corpus)
    leaf = np.memmap(leaf_path, dtype=np.float32, mode="r").reshape(-1, 19)
    valid = np.isfinite(leaf[:, 18]) & (np.asarray(records["explored"]) == 0)
    idx = np.flatnonzero(valid)
    rec = records[idx]
    leaf = np.asarray(leaf[idx])
    boards = np.asarray(rec["board"])
    feats: dict[str, list] = {}
    for start in range(0, len(rec), 250_000):
        for k, v in pattern_features(boards[start:start + 250_000]).items():
            feats.setdefault(k, []).append(v)
    feats = {k: np.concatenate(v) for k, v in feats.items()}
    mtd = np.asarray(rec["movesToDeath"]).astype(np.float64)
    censored = np.asarray(rec["censoredGame"]) == 1
    occ = np.asarray(rec["occupiedCells"]).astype(np.float64)
    depth = np.asarray(rec["behaviorDepth"])
    keep = ~censored
    train_mask, _, test_mask = ds.split_by_origin(rec)
    y = np.log1p(mtd)
    base = np.column_stack([leaf[:, :18], occ, np.asarray(rec["movesRemaining"]).astype(np.float64), np.ones(len(rec))])
    tr, te = train_mask & keep, test_mask & keep
    mu, sd = base[tr].mean(axis=0), base[tr].std(axis=0) + 1e-9
    Xs = (base - mu) / sd; Xs[:, -1] = 1.0
    beta_y, *_ = np.linalg.lstsq(Xs[tr], y[tr], rcond=None)
    res_y = y[te] - Xs[te] @ beta_y
    out = {"format": "drop7-pattern-diagnostic-v1", "records": int(len(rec)), "patterns": {}}
    d4 = depth == 4
    for k, z in feats.items():
        zf = z.astype(np.float64)
        beta_z, *_ = np.linalg.lstsq(Xs[tr], zf[tr], rcond=None)
        res_z = zf[te] - Xs[te] @ beta_z
        partial = float(np.corrcoef(res_y, res_z)[0, 1]) if res_z.std() > 0 else None
        raw = float(np.corrcoef(zf[keep], y[keep])[0, 1])
        matched = {}
        for lo in (16, 20, 24, 28, 32):
            m = (occ >= lo) & (occ < lo + 4) & keep
            w, wo = m & (z > 0), m & (z == 0)
            if w.sum() > 500 and wo.sum() > 500:
                matched[f"occ{lo}-{lo+3}"] = [round(float(mtd[w].mean()), 1), round(float(mtd[wo].mean()), 1), int(w.sum()), int(wo.sum())]
        near = d4 & keep & (mtd <= 5); far = d4 & keep & (mtd > 40)
        out["patterns"][k] = {"meanPerState": float(zf.mean()), "fracStatesDepth4": float((z[d4] > 0).mean()),
                              "fracDepth4Within5OfDeath": float((z[near] > 0).mean()), "fracDepth4MoreThan40FromDeath": float((z[far] > 0).mean()),
                              "rawCorrelationLogLifetime": raw, "heldOutPartialCorrelationBeyondLeaf": partial,
                              "occupancyMatchedMeanMovesToDeath[with,without,n_with,n_without]": matched}
    Path(out_path).write_text(json.dumps(out, indent=2) + "\n")
    for k, v in out["patterns"].items():
        print(f"{k:26} d4 prevalence {v['fracStatesDepth4']:.3f}  near-death {v['fracDepth4Within5OfDeath']:.3f}  far {v['fracDepth4MoreThan40FromDeath']:.3f}  raw r {v['rawCorrelationLogLifetime']:+.3f}  partial r beyond leaf {v['heldOutPartialCorrelationBeyondLeaf']:+.4f}  matched {v['occupancyMatchedMeanMovesToDeath[with,without,n_with,n_without]'].get('occ24-27')}")


if __name__ == "__main__":
    main()
