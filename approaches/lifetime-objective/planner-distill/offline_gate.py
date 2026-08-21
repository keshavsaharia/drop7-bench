"""The offline sibling-ranking gate of `docs/benchmarks.md`, run before any
gameplay.

Reports, on held-out ORIGIN GAMES only:

  * action completeness and missing-label rate;
  * the TEACHER's own split-half argmax agreement - the ceiling, because no
    student can agree with a label more often than the label agrees with itself,
    and nothing in this repository has previously reported it;
  * student top-1, top-2, within-root pairwise accuracy and normalised regret,
    under a one-realisation score and under an eight-realisation score;
  * calibration of the value head against the teacher residual;
  * the same statistics for the unmodified fair depth-4 search on the same
    roots, which is the comparator the preregistration names; and
  * breakouts by rise phase, occupancy band, legal-action count and origin fold.

Played-action value error is deliberately absent: the repository's own record
(audit-05 section 4, experiments 12 and 14) is that it never predicted ranking.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import sys
import time

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "learned-leaf"))
sys.path.insert(0, HERE)

import dataset as pd            # noqa: E402
import leaf_features as lf      # noqa: E402
import train_student as ts      # noqa: E402

AFTER_DTYPE = np.dtype([
    ("row", np.uint32), ("column", np.uint8), ("draw", np.uint8),
    ("survived", np.uint8), ("clears", np.uint8), ("reveals", np.uint8),
    ("nextDisc", np.uint8), ("movesRemaining", np.uint8), ("occupied", np.uint8),
    ("board", np.uint8, (49,)), ("padding", np.uint8, (3,)),
])
assert AFTER_DTYPE.itemsize == 64

RANK_DTYPE = np.dtype([
    ("row", np.uint32), ("action", np.int8), ("completedDepth", np.uint8),
    ("legalMask", np.uint8), ("padding", np.uint8),
    ("value", np.float32, (7,)), ("work", np.uint64),
])
assert RANK_DTYPE.itemsize == 44


# ---------------------------------------------------------------------------

def ranking_block(score, root, column, value, root_count, min_gap=0.02):
    """top-1 / top-2 / pairwise / normalised regret for one flat score vector."""
    student = pd.argmax_by_root(score, root, column, root_count)
    teacher = pd.argmax_by_root(value, root, column, root_count)
    top2 = pd.topk_by_root(score, root, column, root_count, 2)
    covered = teacher >= 0
    grid = np.full((root_count, pd.COLUMNS), np.nan)
    grid[root, column] = value
    with np.errstate(invalid="ignore"):
        high = np.nanmax(grid, axis=1)
        low = np.nanmin(grid, axis=1)
    picked = grid[np.arange(root_count), np.maximum(student, 0)]
    spread = high - low
    usable = covered & (spread > 1e-9) & np.isfinite(picked)

    rng = np.random.default_rng(0xA526_6A7E)
    pairs = ts.build_pairs(root, value, min_gap, rng, max_per_root=64)
    pairwise = (float(np.mean(score[pairs[:, 0]] > score[pairs[:, 1]]))
                if len(pairs) else float("nan"))
    return {
        "top1": float(np.mean(student[covered] == teacher[covered])),
        "top2": float(np.mean((top2[covered, 0] == teacher[covered]) |
                              (top2[covered, 1] == teacher[covered]))),
        "pairwise": pairwise,
        "pairCount": int(len(pairs)),
        "normalisedRegret": float(np.mean((high[usable] - picked[usable]) /
                                          spread[usable])),
        "roots": int(covered.sum()),
    }, student, teacher


def breakout(score, root, column, value, roots_struct, root_count, key_fn, name):
    student = pd.argmax_by_root(score, root, column, root_count)
    teacher = pd.argmax_by_root(value, root, column, root_count)
    keys = key_fn(roots_struct)
    out = {}
    for key in np.unique(keys):
        take = (keys == key) & (teacher >= 0)
        if take.sum() < 20:
            continue
        out[str(key)] = {"roots": int(take.sum()),
                         "top1": float(np.mean(student[take] == teacher[take]))}
    return {name: out}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", nargs="+", required=True)
    parser.add_argument("--after", nargs="+", default=[])
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--d4-rank", default="")
    parser.add_argument("--split", default="test", choices=("train", "val", "test"))
    parser.add_argument("--folds", type=int, default=5)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--batch", type=int, default=8192)
    parser.add_argument("--max-draws", type=int, default=8,
                        help="cap the chance realisations per sibling; the CNN "
                             "arm is 3,000x more expensive per state than the "
                             "leaf arm and does not need all of them")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    records = pd.load(*args.corpus)
    masks = dict(zip(("train", "val", "test"), pd.split_by_origin(records)))
    mask = masks[args.split]
    calibration_mask = masks["val"] if args.split != "val" else masks["train"]
    rows_kept = np.flatnonzero(mask)
    subset = np.asarray(records[mask])
    panel = pd.Panel(subset)
    root_count = panel.root_count

    result = {
        "split": args.split,
        "roots": int(root_count),
        "origins": int(len(np.unique(subset["gameSeed"]))),
        "siblingPairs": int(len(panel)),
        "meanLegalColumns": float(panel.legal_count.mean()),
        "missingLabels": panel.missing,
        "illegalLabelled": panel.illegal_labelled,
        "actionCompleteness": 1.0 - panel.missing / max(len(panel), 1),
    }

    # ---- the ceiling: how often does the teacher agree with itself? --------
    lo = pd.argmax_by_root(panel.value_lo, panel.root, panel.column, root_count)
    hi = pd.argmax_by_root(panel.value_hi, panel.root, panel.column, root_count)
    full = pd.argmax_by_root(panel.value, panel.root, panel.column, root_count)
    result["teacherSplitHalfAgreement"] = float(np.mean(lo == hi))
    result["teacherHalfAgreesWithFull"] = float(0.5 * (np.mean(lo == full) +
                                                       np.mean(hi == full)))
    gap = np.abs(panel.value_lo - panel.value_hi)
    result["teacherHalfValueMeanAbsoluteGap"] = float(gap.mean())
    # How much does the disagreement COST?  A low self-agreement is only fatal
    # if the columns the two halves prefer are actually worth different amounts.
    # Judged by the full-K values, half A's choice has this normalised regret -
    # which is the floor on any student's regret, in the same units.
    half_block, _, _ = ranking_block(panel.value_lo, panel.root, panel.column,
                                     panel.value, root_count)
    result["teacherHalfAsStudent"] = half_block
    grid_full = np.full((root_count, pd.COLUMNS), np.nan)
    grid_full[panel.root, panel.column] = panel.value
    with np.errstate(invalid="ignore"):
        spread_full = np.nanmax(grid_full, axis=1) - np.nanmin(grid_full, axis=1)
    both = (lo >= 0) & (hi >= 0) & (spread_full > 1e-9)
    disagree = both & (lo != hi)
    if disagree.any():
        cost = np.abs(grid_full[np.arange(root_count), np.maximum(lo, 0)] -
                      grid_full[np.arange(root_count), np.maximum(hi, 0)])
        result["teacherHalfDisagreementCostDiscs"] = float(cost[disagree].mean())
        result["teacherHalfDisagreementCostNormalised"] = float(
            (cost[disagree] / spread_full[disagree]).mean())
    result["teacherRootValueSpreadMeanDiscs"] = float(
        np.nanmean(spread_full[np.isfinite(spread_full)]))
    top_two = pd.topk_by_root(panel.value, panel.root, panel.column, root_count, 2)
    have = (top_two[:, 0] >= 0) & (top_two[:, 1] >= 0)
    margin = (grid_full[np.arange(root_count), np.maximum(top_two[:, 0], 0)] -
              grid_full[np.arange(root_count), np.maximum(top_two[:, 1], 0)])
    result["teacherTop1MinusTop2MeanDiscs"] = float(np.nanmean(margin[have]))
    result["teacherTop1MinusTop2MedianDiscs"] = float(np.nanmedian(margin[have]))

    # ---- the student -------------------------------------------------------
    blob = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    arch = blob["arch"]
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")
    if arch == "leaf":
        model = ts.LeafStudent(blob["hidden"], blob["mid"])
    else:
        model = ts.CnnStudent(blob["channels"], blob["blocks"])
    model.load_state_dict(blob["model"])
    model = model.to(device).eval()

    def evaluate_boards(board, next_disc, moves_remaining):
        if arch == "leaf":
            feats = ts.leaf_index(board, next_disc, moves_remaining)
        else:
            feats = (ts.plane_encode(board, next_disc, moves_remaining),)
        return ts.predict(model, arch, feats, device, args.batch)

    started = time.time()
    single = evaluate_boards(panel.after_board, panel.after_next_disc,
                             panel.after_moves_remaining)
    result["studentSeconds1Draw"] = time.time() - started
    result["studentMicrosecondsPerStateBatched"] = (
        1e6 * result["studentSeconds1Draw"] / max(len(panel), 1))

    # A within-root listwise loss is invariant to any per-root shift, so it does
    # not by itself pin the SCALE of `f` relative to the immediate term - and the
    # search's ranking is `immediate + f`, which is not scale invariant.  The
    # affine map is therefore fitted on the VALIDATION origins and applied
    # unchanged to the held-out split, which is a legitimate use of validation
    # data and is reported as a separate arm rather than folded into the
    # headline silently.
    calibration_panel = pd.Panel(np.asarray(records[calibration_mask]))
    calibration_pred = evaluate_boards(calibration_panel.after_board,
                                       calibration_panel.after_next_disc,
                                       calibration_panel.after_moves_remaining)
    design = np.stack([calibration_pred,
                       np.ones_like(calibration_pred)], axis=1)
    slope, intercept = np.linalg.lstsq(
        design, calibration_panel.residual, rcond=None)[0]
    result["recalibration"] = {"slope": float(slope), "intercept": float(intercept),
                               "fittedOnRoots": int(calibration_panel.root_count)}

    score_single = panel.immediate + single
    block, _, _ = ranking_block(score_single, panel.root, panel.column,
                                panel.value, root_count)
    result["student1Draw"] = block

    # A second variant that uses the realised immediate clears rather than the
    # teacher's mean, i.e. exactly what a single-stratum search would compute.
    block_realised, _, _ = ranking_block(panel.after_clears + single, panel.root,
                                         panel.column, panel.value, root_count)
    result["student1DrawRealisedImmediate"] = block_realised

    # ---- the eight-realisation score --------------------------------------
    if args.after:
        after = np.concatenate([np.fromfile(p, dtype=AFTER_DTYPE)
                                for p in args.after])
        keep = np.isin(after["row"], rows_kept) & (after["draw"] < args.max_draws)
        after = after[keep]
        remap = -np.ones(int(records.shape[0]), dtype=np.int64)
        remap[rows_kept] = np.arange(len(rows_kept))
        after_root = remap[after["row"].astype(np.int64)]
        values = evaluate_boards(np.ascontiguousarray(after["board"]),
                                 after["nextDisc"], after["movesRemaining"])
        per_draw = values + after["clears"].astype(np.float32)
        key = after_root * pd.COLUMNS + after["column"].astype(np.int64)
        order = np.argsort(key, kind="stable")
        key_sorted = key[order]
        sums = np.bincount(key_sorted, weights=per_draw[order],
                           minlength=root_count * pd.COLUMNS)
        counts = np.bincount(key_sorted, minlength=root_count * pd.COLUMNS)
        pair_key = panel.root * pd.COLUMNS + panel.column
        drawn = counts[pair_key]
        mean_score = np.where(drawn > 0, sums[pair_key] / np.maximum(drawn, 1),
                              panel.immediate + single)
        result["drawsPerSibling"] = float(drawn.mean())
        block8, student8, teacher8 = ranking_block(
            mean_score.astype(np.float32), panel.root, panel.column,
            panel.value, root_count)
        result["studentMultiDraw"] = block8
        result["studentAgreesWithTeacherHalfLo"] = float(np.mean(student8 == lo))
        result["studentAgreesWithTeacherHalfHi"] = float(np.mean(student8 == hi))
        result["studentHalfStabilityGap"] = abs(
            result["studentAgreesWithTeacherHalfLo"] -
            result["studentAgreesWithTeacherHalfHi"])
        headline = mean_score.astype(np.float32)
        # The recalibrated multi-draw arm: the same eight draws, with `f` mapped
        # onto the teacher-residual scale by the validation-fitted affine map.
        per_draw_cal = slope * values + intercept + after["clears"].astype(np.float32)
        sums_cal = np.bincount(key_sorted, weights=per_draw_cal[order],
                               minlength=root_count * pd.COLUMNS)
        mean_cal = np.where(drawn > 0, sums_cal[pair_key] / np.maximum(drawn, 1),
                            panel.immediate + slope * single + intercept)
        block_cal8, _, _ = ranking_block(mean_cal.astype(np.float32), panel.root,
                                         panel.column, panel.value, root_count)
        result["studentMultiDrawRecalibrated"] = block_cal8
        # FIXED IN ADVANCE, not chosen by looking at the test numbers: the
        # headline arm is the eight-draw score with the validation-fitted affine
        # map, because that map is fitted on validation data and is therefore
        # part of the model definition rather than a test-set choice.
        headline = mean_cal.astype(np.float32)
        result["headlineArm"] = "multiDrawRecalibrated"
    else:
        headline = score_single
        result["studentMultiDraw"] = block

    block_cal, _, _ = ranking_block(
        (panel.immediate + slope * single + intercept).astype(np.float32),
        panel.root, panel.column, panel.value, root_count)
    result["student1DrawRecalibrated"] = block_cal

    # ---- reference scorers, so the student's number has a floor -----------
    # `immediateOnly` is what a search with a constant leaf would rank by: the
    # discs the move itself clears, and nothing else.  Any learned evaluator
    # that does not beat it has added nothing.  `negativeOccupancy` is the
    # cheapest non-trivial hand heuristic in the same units.
    ref_blocks = {}
    for name, scorer in (
            ("immediateOnly", panel.immediate.astype(np.float32)),
            ("immediateMinusOccupancy",
             (panel.immediate - 0.1 * np.asarray(
                 panel.roots["afterOccupied"], dtype=np.float32)[
                     panel.root, panel.column]).astype(np.float32)),
            ("survivalOnly", (panel.after_survived.astype(np.float32) * 10.0
                              + panel.immediate).astype(np.float32)),
    ):
        block_ref, _, _ = ranking_block(scorer, panel.root, panel.column,
                                        panel.value, root_count)
        ref_blocks[name] = block_ref
    result["references"] = ref_blocks

    # ---- calibration -------------------------------------------------------
    residual = panel.residual
    result["calibration"] = {
        "pearson": float(np.corrcoef(single, residual)[0, 1]),
        "meanAbsoluteError": float(np.abs(single - residual).mean()),
        "targetMean": float(residual.mean()),
        "targetStd": float(residual.std()),
        "predictionMean": float(single.mean()),
        "predictionStd": float(single.std()),
    }
    bins = np.quantile(single, np.linspace(0, 1, 11))
    table = []
    for low, high in zip(bins[:-1], bins[1:]):
        take = (single >= low) & (single <= high)
        if take.sum() < 10:
            continue
        table.append({"predictedMean": float(single[take].mean()),
                      "actualMean": float(residual[take].mean()),
                      "count": int(take.sum())})
    result["calibration"]["reliability"] = table

    # ---- the comparator ----------------------------------------------------
    if args.d4_rank:
        rank = np.fromfile(args.d4_rank, dtype=RANK_DTYPE)
        keep = np.isin(rank["row"], rows_kept)
        rank = rank[keep]
        remap = -np.ones(int(records.shape[0]), dtype=np.int64)
        remap[rows_kept] = np.arange(len(rows_kept))
        rank_root = remap[rank["row"].astype(np.int64)]
        grid = np.full((root_count, pd.COLUMNS), -np.inf, dtype=np.float64)
        grid[rank_root] = rank["value"]
        covered_roots = np.zeros(root_count, dtype=bool)
        covered_roots[rank_root] = True
        take = covered_roots[panel.root]
        d4_score = grid[panel.root[take], panel.column[take]]
        sub_root = panel.root[take]
        # Re-index so the comparator's roots are contiguous.
        uniq, inverse = np.unique(sub_root, return_inverse=True)
        block_d4, _, _ = ranking_block(d4_score.astype(np.float32), inverse,
                                       panel.column[take], panel.value[take],
                                       len(uniq))
        result["fairD4"] = block_d4
        # The student restricted to exactly the comparator's roots, so the two
        # are read on the same denominator.
        block_student_same, _, _ = ranking_block(
            headline[take], inverse, panel.column[take], panel.value[take],
            len(uniq))
        result["studentOnComparatorRoots"] = block_student_same
        lo_sub = pd.argmax_by_root(panel.value_lo[take], inverse,
                                   panel.column[take], len(uniq))
        hi_sub = pd.argmax_by_root(panel.value_hi[take], inverse,
                                   panel.column[take], len(uniq))
        result["teacherSplitHalfAgreementOnComparatorRoots"] = float(
            np.mean(lo_sub == hi_sub))
        result["comparatorCompletedDepth4Share"] = float(
            np.mean(rank["completedDepth"] == 4))

    # ---- breakouts ---------------------------------------------------------
    result["breakouts"] = {}
    for name, fn in (
        ("risePhase", lambda r: np.asarray(r["movesRemaining"])),
        ("occupancyBand", lambda r: (np.asarray(r["occupied"]) // 5) * 5),
        ("legalCount", lambda r: np.asarray(
            [bin(int(m)).count("1") for m in r["legalMask"]])),
    ):
        result["breakouts"].update(
            breakout(headline, panel.root, panel.column, panel.value,
                     panel.roots, root_count, fn, name))

    # ---- origin folds ------------------------------------------------------
    seeds = np.unique(subset["gameSeed"])
    fold_of_seed = {int(s): i % args.folds for i, s in enumerate(seeds)}
    root_fold = np.array([fold_of_seed[int(s)] for s in subset["gameSeed"]])
    folds = []
    for fold in range(args.folds):
        take_root = root_fold == fold
        take_pair = take_root[panel.root]
        if take_pair.sum() < 50:
            continue
        uniq, inverse = np.unique(panel.root[take_pair], return_inverse=True)
        entry = {"fold": fold, "roots": int(len(uniq))}
        block_s, _, _ = ranking_block(headline[take_pair], inverse,
                                      panel.column[take_pair],
                                      panel.value[take_pair], len(uniq))
        entry["studentTop1"] = block_s["top1"]
        entry["studentPairwise"] = block_s["pairwise"]
        entry["studentRegret"] = block_s["normalisedRegret"]
        if args.d4_rank and "fairD4" in result:
            both = take_pair & take
            if both.sum() > 50:
                uniq2, inverse2 = np.unique(panel.root[both], return_inverse=True)
                sel = both[take]
                block_d, _, _ = ranking_block(
                    d4_score.astype(np.float32)[sel], inverse2,
                    panel.column[both], panel.value[both], len(uniq2))
                entry["fairD4Top1"] = block_d["top1"]
                entry["fairD4Pairwise"] = block_d["pairwise"]
                entry["fairD4Regret"] = block_d["normalisedRegret"]
                block_s2, _, _ = ranking_block(
                    headline[both], inverse2, panel.column[both],
                    panel.value[both], len(uniq2))
                entry["studentTop1SameRoots"] = block_s2["top1"]
        folds.append(entry)
    result["folds"] = folds

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps({k: v for k, v in result.items()
                      if k not in ("breakouts", "folds", "calibration")},
                     indent=2))


if __name__ == "__main__":
    main()
