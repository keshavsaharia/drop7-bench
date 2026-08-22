#!/usr/bin/env python3
"""Entombed-disc analysis on the existing training corpus (no seed is opened).

A numbered disc of value n is ENTOMBED when its column already holds more than
n discs (a gravity-packed column's vertical run is its height, so the disc can
never clear vertically until the column shrinks) AND the contiguous horizontal
run through it already exceeds n.  It can then only clear after neighbours are
removed.  The frozen leaf prices this for 1s and 2s (dead_low_numbers,
low_number_height_risk, high_low_numbers) and not for 3s and above.

Three questions, answered from runs/RUN-A51D-corpus/all.states and the frozen
leaf features dumped by leafdump.cpp:

  1. prevalence: how often the behaviour policies (depth 1-4) leave an
     entombed disc of value >= 3 on the board, by move and at death;
  2. lead time: in complete games, how many moves before death the first
     persistent entombed >= 3 disc appears (the window an emergency mode
     would have);
  3. incremental signal: held-out R^2 for log1p(moves to death) of the 18
     frozen leaf features + occupancy + rise clock, versus the same plus the
     entombed features, on a whole-origin split (dataset.py's split).

Usage: analyze.py <corpus.states> <leaf.f32> <out.json>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "approaches/lifetime-objective/afterstate-net"))
import dataset as ds  # noqa: E402

LEAF_NAMES = [
    "open_columns", "height_load", "solid_cells", "cracked_cells", "numbered_cells", "high_low_numbers",
    "direct_potential", "latent_chain_potential", "cracked_exposure", "solid_exposure", "adjacent_ones",
    "triple_twos", "dead_low_numbers", "covered_height_risk", "low_number_height_risk", "danger_height_squared",
    "rise_pressure", "next_disc_vertical_options",
]
B = 7


def entombed_features(boards: np.ndarray) -> dict[str, np.ndarray]:
    """boards: (N,49) uint8 row-major from the top.  Vectorised."""
    n = boards.shape[0]
    grid = boards.reshape(n, B, B)
    occupied = grid != 0
    numbered = (grid >= 1) & (grid <= 7)
    cover = (grid == 8) | (grid == 9)
    height = occupied.sum(axis=1)  # (N,7) per column; packed columns
    # elevation of row r (1 = bottom): B - r
    elevation = np.broadcast_to((B - np.arange(B))[None, :, None], (n, B, B))
    # horizontal run length through each cell: left-extent + right-extent - 1
    left = np.zeros((n, B, B), dtype=np.int16)
    right = np.zeros((n, B, B), dtype=np.int16)
    for c in range(B):
        left[:, :, c] = np.where(occupied[:, :, c], (left[:, :, c - 1] if c else 0) + 1, 0)
    for c in range(B - 1, -1, -1):
        right[:, :, c] = np.where(occupied[:, :, c], (right[:, :, c + 1] if c < B - 1 else 0) + 1, 0)
    hrun = left + right - 1
    value = grid.astype(np.int16)
    col_height = np.broadcast_to(height[:, None, :], (n, B, B))
    entombed = numbered & (col_height > value) & (hrun > value)
    # covers directly beneath a cell in the same column (rows below = larger row index)
    covers_below = np.cumsum(cover[:, ::-1, :], axis=1)[:, ::-1, :] - cover
    top_of_column = occupied & ~np.roll(occupied, 1, axis=1)
    top_of_column[:, 0, :] = occupied[:, 0, :]
    ge3 = entombed & (value >= 3)
    le2 = entombed & (value <= 2)
    return {
        "ent_count_ge3": ge3.sum(axis=(1, 2)).astype(np.float32),
        "ent_mass_ge3": (ge3 * elevation ** 2).sum(axis=(1, 2)).astype(np.float32),
        "ent_covers_ge3": (ge3 * covers_below).sum(axis=(1, 2)).astype(np.float32),
        "ent_top_ge3": (ge3 & top_of_column).sum(axis=(1, 2)).astype(np.float32),
        "ent_count_le2": le2.sum(axis=(1, 2)).astype(np.float32),
        "ent_count_34": (entombed & ((value == 3) | (value == 4))).sum(axis=(1, 2)).astype(np.float32),
    }


def ols_r2(xtr, ytr, xte, yte) -> tuple[float, float]:
    beta, *_ = np.linalg.lstsq(xtr, ytr, rcond=None)
    pred = xte @ beta
    ss_res = float(((yte - pred) ** 2).sum())
    ss_tot = float(((yte - yte.mean()) ** 2).sum())
    r = float(np.corrcoef(pred, yte)[0, 1])
    return 1 - ss_res / ss_tot, r


def main() -> None:
    corpus, leaf_path, out_path = sys.argv[1:4]
    records = ds.load_states(corpus)
    leaf = np.memmap(leaf_path, dtype=np.float32, mode="r").reshape(-1, 19)
    assert leaf.shape[0] == len(records), (leaf.shape, len(records))
    valid = np.isfinite(leaf[:, 18]) & (np.asarray(records["explored"]) == 0)
    idx = np.flatnonzero(valid)
    rec = records[idx]
    leaf = np.asarray(leaf[idx])
    boards = np.asarray(rec["board"])
    feats = {}
    for start in range(0, len(rec), 250_000):  # chunked to bound memory
        part = entombed_features(boards[start:start + 250_000])
        for k, v in part.items():
            feats.setdefault(k, []).append(v)
    feats = {k: np.concatenate(v) for k, v in feats.items()}
    depth = np.asarray(rec["behaviorDepth"])
    mtd = np.asarray(rec["movesToDeath"]).astype(np.float64)
    censored = np.asarray(rec["censoredGame"]) == 1
    occ = np.asarray(rec["occupiedCells"]).astype(np.float64)
    out: dict = {"format": "drop7-entombed-analysis-v1", "records": int(len(rec)), "excludedExplored": int((np.asarray(records["explored"]) != 0).sum()),
                 "invalidLeafRecords": int((~np.isfinite(np.asarray(np.memmap(leaf_path, dtype=np.float32, mode="r").reshape(-1, 19)[:, 18]))).sum())}

    # 1. prevalence by behaviour depth and by moves-to-death bucket
    prev = {}
    for d in (1, 2, 3, 4):
        m = depth == d
        buckets = {}
        for lo, hi, name in ((0, 5, "death<=5"), (6, 15, "death6-15"), (16, 40, "death16-40"), (41, 10_000, "death>40")):
            mm = m & (mtd >= lo) & (mtd <= hi) & ~censored
            if mm.sum() == 0:
                continue
            buckets[name] = {"states": int(mm.sum()), "fracWithEntombedGe3": float((feats["ent_count_ge3"][mm] > 0).mean()),
                             "meanEntombedGe3": float(feats["ent_count_ge3"][mm].mean()), "meanOccupancy": float(occ[mm].mean()),
                             "fracWithEntombed34": float((feats["ent_count_34"][mm] > 0).mean())}
        prev[f"depth{d}"] = {"states": int(m.sum()), "fracWithEntombedGe3": float((feats["ent_count_ge3"][m] > 0).mean()),
                             "fracWithEntombedLe2": float((feats["ent_count_le2"][m] > 0).mean()), "byMovesToDeath": buckets}
    out["prevalence"] = prev

    # occupancy-matched comparison (is it just a proxy for a full board?)
    matched = {}
    for lo in range(8, 40, 4):
        m = (occ >= lo) & (occ < lo + 4) & ~censored
        with_e, without = m & (feats["ent_count_ge3"] > 0), m & (feats["ent_count_ge3"] == 0)
        if with_e.sum() > 500 and without.sum() > 500:
            matched[f"occ{lo}-{lo+3}"] = {"with": int(with_e.sum()), "without": int(without.sum()),
                                        "meanMovesToDeathWith": float(mtd[with_e].mean()), "meanMovesToDeathWithout": float(mtd[without].mean()),
                                        "medianWith": float(np.median(mtd[with_e])), "medianWithout": float(np.median(mtd[without]))}
    out["occupancyMatched"] = matched

    # 2. lead time in complete games (depth 3 and 4 behaviour)
    lead = {}
    for d in (3, 4):
        m = (depth == d) & ~censored
        seeds = np.asarray(rec["gameSeed"])[m]
        moves = np.asarray(rec["moveIndex"])[m]
        ent = feats["ent_count_ge3"][m] > 0
        mt = mtd[m]
        order = np.lexsort((moves, seeds))
        seeds, moves, ent, mt = seeds[order], moves[order], ent[order], mt[order]
        starts = np.flatnonzero(np.r_[True, seeds[1:] != seeds[:-1]])
        ends = np.r_[starts[1:], len(seeds)]
        leads, at_death, lengths, games = [], 0, [], 0
        for s, e in zip(starts, ends):
            games += 1
            lengths.append(int(mt[s]) + int(moves[s]))
            g_ent = ent[s:e]
            if not g_ent[-1]:
                continue
            at_death += 1
            # first index of the final run of True
            k = e - s - 1
            while k > 0 and g_ent[k - 1]:
                k -= 1
            leads.append(float(mt[s + k]))
        leads = np.array(leads)
        lead[f"depth{d}"] = {"completeGames": games, "gamesWithEntombedGe3AtDeath": at_death, "fracAtDeath": at_death / max(games, 1),
                             "leadTimeMoves": {"median": float(np.median(leads)) if len(leads) else None, "q25": float(np.quantile(leads, 0.25)) if len(leads) else None,
                                               "q75": float(np.quantile(leads, 0.75)) if len(leads) else None, "mean": float(leads.mean()) if len(leads) else None,
                                               "fracAtLeast10": float((leads >= 10).mean()) if len(leads) else None, "fracAtLeast20": float((leads >= 20).mean()) if len(leads) else None},
                             "meanGameLength": float(np.mean(lengths))}
    out["leadTime"] = lead

    # 3. incremental held-out signal, whole-origin split
    train_mask, val_mask, test_mask = ds.split_by_origin(rec)
    keep = ~censored
    y = np.log1p(mtd)
    base = np.column_stack([leaf[:, :18], occ, np.asarray(rec["movesRemaining"]).astype(np.float64), np.ones(len(rec))])
    base_scalar = np.column_stack([leaf[:, 18], occ, np.asarray(rec["movesRemaining"]).astype(np.float64), np.ones(len(rec))])
    ent_cols = np.column_stack([feats[k] for k in ("ent_count_ge3", "ent_mass_ge3", "ent_covers_ge3", "ent_top_ge3", "ent_count_le2")]).astype(np.float64)
    full = np.column_stack([base, ent_cols])
    # standardise columns for numerical sanity (lstsq on raw scales is fine but keep it clean)
    def fit(name, X, sub=None):
        tr = train_mask & keep if sub is None else train_mask & keep & sub
        te = test_mask & keep if sub is None else test_mask & keep & sub
        mu, sd = X[tr].mean(axis=0), X[tr].std(axis=0) + 1e-9
        Xs = (X - mu) / sd
        Xs[:, -1] = 1.0 if X.shape[1] == base.shape[1] or X.shape[1] == full.shape[1] or True else Xs[:, -1]
        r2, r = ols_r2(Xs[tr], y[tr], Xs[te], y[te])
        return {"heldOutR2": r2, "heldOutPearson": r, "train": int(tr.sum()), "test": int(te.sum())}
    signal = {
        "leafScalar+occupancy": fit("scalar", base_scalar),
        "leafFeatures18+occupancy": fit("base", base),
        "leafFeatures18+occupancy+entombed": fit("full", full),
        "entombedOnly+occupancy": fit("ent", np.column_stack([ent_cols, occ, np.ones(len(rec))])),
        "depth4only": {"leafFeatures18+occupancy": fit("base4", base, depth == 4), "leafFeatures18+occupancy+entombed": fit("full4", full, depth == 4)},
        "depth3only": {"leafFeatures18+occupancy": fit("base3", base, depth == 3), "leafFeatures18+occupancy+entombed": fit("full3", full, depth == 3)},
    }
    # univariate: partial correlation of ent_count_ge3 with y after the 18 features + occupancy, on held-out
    tr, te = train_mask & keep, test_mask & keep
    mu, sd = base[tr].mean(axis=0), base[tr].std(axis=0) + 1e-9
    Xs = (base - mu) / sd; Xs[:, -1] = 1.0
    beta_y, *_ = np.linalg.lstsq(Xs[tr], y[tr], rcond=None)
    res_y = y[te] - Xs[te] @ beta_y
    partial = {}
    for k in ("ent_count_ge3", "ent_mass_ge3", "ent_covers_ge3", "ent_top_ge3", "ent_count_34"):
        z = feats[k].astype(np.float64)
        beta_z, *_ = np.linalg.lstsq(Xs[tr], z[tr], rcond=None)
        res_z = z[te] - Xs[te] @ beta_z
        partial[k] = float(np.corrcoef(res_y, res_z)[0, 1]) if res_z.std() > 0 else None
    signal["partialCorrelationHeldOut"] = partial
    out["incrementalSignal"] = signal
    out["featureMeans"] = {k: float(v.mean()) for k, v in feats.items()}
    Path(out_path).write_text(json.dumps(out, indent=2) + "\n")
    print(json.dumps({k: out[k] for k in ("records", "leadTime", "incrementalSignal")}, indent=2))


if __name__ == "__main__":
    main()
