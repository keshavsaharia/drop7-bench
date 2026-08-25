#!/usr/bin/env python3
"""Stage-D0 analysis: tau, the four R numbers, halves, flow bands, top-1 rates.

Reads pools.json (d0-relabel output) and the generator summary, writes
d0-result.json.  Dependency-free.  Every number here is computed from the
records in pools.json; nothing is inferred.

Definitions (EX-20260823-hpool-stage-d0-e0ad1c65):
  R_fair(X)  mean over matched X states of relabel.meanMoves (K = 32 public
             futures, fair D1 continuation, horizon 25; survivors count 25).
  R_tape(O)  mean over matched O states of remainingCapped (realised remaining
             moves on the oracle's own trajectory, capped at 25).
  R_real(F)  the same on the matched F states' own fair-D4 trajectories.
  tau        (R_fair(O) - R_fair(F)) / (R_tape(O) - R_real(F)).
  halves     O origin games [0, 32) and [32, 64) with their matched partners.
  top-1      at each O root, a column is fair-top-1 when its sibling mean
             moves (same 32 futures, common random numbers) equals the maximum
             over legal columns (ties count; the strict version is reported
             too).
Uncertainty: cluster bootstrap over O origin games carrying each O state's
matched F partner (clusters = 64 O games), 10,000 resamples, seed 0xb0071eaf,
percentile two-sided 95% interval for tau.  A second, independent-clusters
variant (O games and F games resampled separately, unpaired means) is
reported as a sensitivity check.
"""

import json
import math
import random
import sys
from collections import Counter, defaultdict

BOOTSTRAP_RESAMPLES = 10_000
BOOTSTRAP_SEED = 0xB0071EAF
TAU_THRESHOLD = 0.25
MATCHED_FLOOR = 500
HORIZON = 25
TIE_EPSILON = 0.0  # sibling means are multiples of 1/32; exact comparison


def mean(values):
    return sum(values) / len(values) if values else float("nan")


def standard_error(values):
    n = len(values)
    if n < 2:
        return float("nan")
    m = mean(values)
    return math.sqrt(sum((v - m) ** 2 for v in values) / (n - 1) / n)


def cluster_standard_error(values, clusters):
    by = defaultdict(list)
    for value, cluster in zip(values, clusters):
        by[cluster].append(value)
    cluster_means = [mean(v) for v in by.values()]
    return standard_error(cluster_means)


def tau_of(o_fair, f_fair, o_tape, f_real):
    denominator = mean(o_tape) - mean(f_real)
    numerator = mean(o_fair) - mean(f_fair)
    if denominator == 0:
        return float("nan")
    return numerator / denominator


def top1_flags(record):
    sib = record["relabel"]["siblingMeanMoves"]
    legal = [v for v in sib if v is not None]
    best = max(legal)
    strict = sum(1 for v in legal if v == best) == 1
    def is_top(column):
        value = sib[column]
        return value is not None and value >= best - TIE_EPSILON
    return is_top(record["column"]), is_top(record["d4Column"]), strict, best


def main():
    if len(sys.argv) != 4:
        print("usage: analyze.py POOLS.json GENERATE-SUMMARY.json OUT.json")
        return 2
    pools = json.load(open(sys.argv[1]))
    summary = json.load(open(sys.argv[2]))
    states = pools["states"]
    by_id = {s["id"]: s for s in states}
    o_all = [s for s in states if s["pool"] == "O"]
    o_matched = [s for s in o_all if s["match"] >= 0]
    pairs = [(s, by_id[s["match"]]) for s in o_matched]
    for o, f in pairs:
        assert f["pool"] == "F" and f["match"] == o["id"]
        assert f["bucket"] == o["bucket"]

    result = {
        "format": "drop7-hpool-d0-result-v1",
        "experimentId": "EX-20260823-hpool-stage-d0-e0ad1c65",
        "inputs": {"pools": sys.argv[1], "generateSummary": sys.argv[2]},
        "generation": {
            "partial": summary["partial"],
            "stopReason": summary["stopReason"],
            "oracleGames": summary["oracle"]["games"] if isinstance(summary["oracle"]["games"], int) else len(summary["oracle"]["games"]),
            "oracleEligibleVisits": summary["oracle"]["eligibleVisits"],
            "oracleGameMoves": [g["moves"] for g in summary["oracle"]["games"]],
            "oracleGamesCensoredAt500": sum(1 for g in summary["oracle"]["games"] if g["censored"]),
            "fairGamesPlayed": summary["fair"]["gamesPlayed"],
            "fairGameMoves": [g["moves"] for g in summary["fair"]["games"]],
            "fairSeedsAvailable": summary["fair"]["seedsAvailable"],
            "samplingRule": summary["oracle"]["samplingRule"],
            "matchingRule": summary["fair"]["matchingRule"],
            "generateWallSeconds": summary["wallSeconds"],
            "relabelWallSeconds": pools["relabel"]["wallSeconds"],
        },
        "pools": {
            "oStates": len(o_all),
            "oMatched": len(o_matched),
            "oUnmatched": len(o_all) - len(o_matched),
            "matchRate": len(o_matched) / len(o_all) if o_all else None,
            "fStates": len(pairs),
            "fOriginGames": len({f["game"] for _, f in pairs}),
            "oOriginGames": len({o["game"] for o in o_matched}),
            "bucketsMatched": len({tuple(sorted(o["bucket"].items())) for o in o_matched}),
            "unservedBuckets": summary["matching"]["unservedBuckets"],
        },
        "underpowered": len(o_matched) < MATCHED_FLOOR,
    }
    if len(o_matched) < MATCHED_FLOOR or summary["partial"]:
        result["gateEvaluated"] = False
        result["note"] = (
            "fewer than 500 matched O states or partial generation: "
            "preregistration says record partial and do not evaluate the gate"
        )
    else:
        result["gateEvaluated"] = True

    # --- the four R numbers ------------------------------------------------
    o_fair = [o["relabel"]["meanMoves"] for o, _ in pairs]
    f_fair = [f["relabel"]["meanMoves"] for _, f in pairs]
    o_tape = [o["remainingCapped"] for o, _ in pairs]
    f_real = [f["remainingCapped"] for _, f in pairs]
    o_games = [o["game"] for o, _ in pairs]
    f_games = [f["game"] for _, f in pairs]

    def block(values, clusters):
        return {
            "mean": mean(values),
            "seStates": standard_error(values),
            "seOriginGames": cluster_standard_error(values, clusters),
            "n": len(values),
        }

    result["R"] = {
        "R_fair_O": block(o_fair, o_games),
        "R_fair_F": block(f_fair, f_games),
        "R_tape_O": block(o_tape, o_games),
        "R_real_F": block(f_real, f_games),
        "fairDifference": mean(o_fair) - mean(f_fair),
        "realisedDifference": mean(o_tape) - mean(f_real),
        "horizon": HORIZON,
        "scenarios": pools["relabel"]["scenarios"],
    }
    result["censoring"] = {
        "R_tape_O_cappedAt25Fraction": mean([1.0 if v >= HORIZON else 0.0 for v in o_tape]),
        "R_real_F_cappedAt25Fraction": mean([1.0 if v >= HORIZON else 0.0 for v in f_real]),
        "R_tape_O_originCensoredAt500": sum(1 for o, _ in pairs if o["remainingCensored"]),
        "R_real_F_originCensoredAt500": sum(1 for _, f in pairs if f["remainingCensored"]),
        "R_fair_O_survivedHorizonFraction": mean([o["relabel"]["survived"] / o["relabel"]["scenarios"] for o, _ in pairs]),
        "R_fair_F_survivedHorizonFraction": mean([f["relabel"]["survived"] / f["relabel"]["scenarios"] for _, f in pairs]),
    }
    tau = tau_of(o_fair, f_fair, o_tape, f_real)
    result["tau"] = {"pooled": tau}

    # --- halves by O origin game -------------------------------------------
    halves = {}
    for half in (0, 1):
        sub = [(o, f) for o, f in pairs if o["half"] == half]
        if not sub:
            halves[str(half)] = None
            continue
        hf_o = [o["relabel"]["meanMoves"] for o, _ in sub]
        hf_f = [f["relabel"]["meanMoves"] for _, f in sub]
        ht_o = [o["remainingCapped"] for o, _ in sub]
        hr_f = [f["remainingCapped"] for _, f in sub]
        halves[str(half)] = {
            "oGames": sorted({o["game"] for o, _ in sub}),
            "n": len(sub),
            "R_fair_O": mean(hf_o), "R_fair_F": mean(hf_f),
            "R_tape_O": mean(ht_o), "R_real_F": mean(hr_f),
            "fairDifference": mean(hf_o) - mean(hf_f),
            "tau": tau_of(hf_o, hf_f, ht_o, hr_f),
        }
    result["halves"] = halves
    signs_agree = (
        halves["0"] is not None and halves["1"] is not None
        and (halves["0"]["fairDifference"] > 0) == (halves["1"]["fairDifference"] > 0)
        and halves["0"]["fairDifference"] != 0 and halves["1"]["fairDifference"] != 0
    )

    # --- flow bands ----------------------------------------------------------
    def bands(records):
        counter = Counter(r["relabel"]["flowBand"] for r in records)
        n = len(records)
        return {band: {"count": counter.get(band, 0), "fraction": counter.get(band, 0) / n if n else None}
                for band in ("blocked", "closed", "recovering", "flowing")}
    result["flowBands"] = {
        "O_matched": bands([o for o, _ in pairs]),
        "O_all": bands(o_all),
        "F": bands([f for _, f in pairs]),
    }

    # --- top-1 at O roots ----------------------------------------------------
    oracle_top = []
    d4_top = []
    strict_count = 0
    same_column = 0
    oracle_top_strict = []
    d4_top_strict = []
    for o, _ in pairs:
        ot, dt, strict, _best = top1_flags(o)
        oracle_top.append(1.0 if ot else 0.0)
        d4_top.append(1.0 if dt else 0.0)
        strict_count += strict
        same_column += o["column"] == o["d4Column"]
        if strict:
            oracle_top_strict.append(1.0 if ot else 0.0)
            d4_top_strict.append(1.0 if dt else 0.0)
    result["top1AtORoots"] = {
        "roots": len(pairs),
        "subsampled": False,
        "oracleColumnTop1Rate": mean(oracle_top),
        "fairD4ColumnTop1Rate": mean(d4_top),
        "difference": mean(oracle_top) - mean(d4_top),
        "differenceSeOriginGames": cluster_standard_error(
            [a - b for a, b in zip(oracle_top, d4_top)], o_games),
        "oracleEqualsD4ColumnRate": same_column / len(pairs) if pairs else None,
        "uniqueMaximumRoots": strict_count,
        "oracleColumnTop1RateStrictRoots": mean(oracle_top_strict),
        "fairD4ColumnTop1RateStrictRoots": mean(d4_top_strict),
        "tieRule": "a column is top-1 when its sibling mean equals the maximum over legal columns; ties count for every tied column",
    }

    # --- cluster bootstrap -----------------------------------------------------
    rng = random.Random(BOOTSTRAP_SEED)
    by_o_game = defaultdict(list)
    for index, (o, _) in enumerate(pairs):
        by_o_game[o["game"]].append(index)
    o_game_ids = sorted(by_o_game)
    taus = []
    fair_diffs = []
    top1_diffs = []
    for _ in range(BOOTSTRAP_RESAMPLES):
        picked = [rng.choice(o_game_ids) for _ in o_game_ids]
        idx = [i for g in picked for i in by_o_game[g]]
        bo = [o_fair[i] for i in idx]; bf = [f_fair[i] for i in idx]
        bt = [o_tape[i] for i in idx]; br = [f_real[i] for i in idx]
        taus.append(tau_of(bo, bf, bt, br))
        fair_diffs.append(mean(bo) - mean(bf))
        top1_diffs.append(mean([oracle_top[i] for i in idx]) - mean([d4_top[i] for i in idx]))

    def interval(samples):
        finite = sorted(s for s in samples if not math.isnan(s))
        if not finite:
            return None
        lo = finite[int(0.025 * (len(finite) - 1))]
        hi = finite[int(0.975 * (len(finite) - 1))]
        return {"low": lo, "high": hi, "finite": len(finite), "nonFinite": len(samples) - len(finite)}

    # sensitivity: independent O-game and F-game clusters, unpaired means
    rng2 = random.Random(BOOTSTRAP_SEED ^ 0x5A5A5A5A)
    by_f_game = defaultdict(list)
    for index, (_, f) in enumerate(pairs):
        by_f_game[f["game"]].append(index)
    f_game_ids = sorted(by_f_game)
    taus2 = []
    for _ in range(BOOTSTRAP_RESAMPLES):
        po = [rng2.choice(o_game_ids) for _ in o_game_ids]
        pf = [rng2.choice(f_game_ids) for _ in f_game_ids]
        io = [i for g in po for i in by_o_game[g]]
        jf = [i for g in pf for i in by_f_game[g]]
        taus2.append(tau_of([o_fair[i] for i in io], [f_fair[j] for j in jf],
                            [o_tape[i] for i in io], [f_real[j] for j in jf]))

    result["bootstrap"] = {
        "method": "cluster bootstrap over O origin games carrying matched F partners",
        "resamples": BOOTSTRAP_RESAMPLES,
        "seed": hex(BOOTSTRAP_SEED),
        "clusters": len(o_game_ids),
        "tau95": interval(taus),
        "fairDifference95": interval(fair_diffs),
        "top1Difference95": interval(top1_diffs),
        "sensitivityIndependentClusters": {
            "method": "O games and F games resampled independently; unpaired means",
            "oClusters": len(o_game_ids), "fClusters": len(f_game_ids),
            "tau95": interval(taus2),
        },
    }

    # --- gate ---------------------------------------------------------------
    checks = [
        {"criterion": "tau >= 0.25 pooled", "passed": (tau >= TAU_THRESHOLD) if result["gateEvaluated"] else None,
         "observed": f"tau = {tau:.4f}, 95% cluster interval [{result['bootstrap']['tau95']['low']:.4f}, {result['bootstrap']['tau95']['high']:.4f}]" if result["bootstrap"]["tau95"] else f"tau = {tau}"},
        {"criterion": "sign of R_fair(O) - R_fair(F) agrees in both origin-game halves",
         "passed": signs_agree if result["gateEvaluated"] else None,
         "observed": "half 0: {:.4f}, half 1: {:.4f}".format(
             halves["0"]["fairDifference"] if halves["0"] else float("nan"),
             halves["1"]["fairDifference"] if halves["1"] else float("nan"))},
        {"criterion": "oracle-column fair-top-1 rate at O roots >= fair-D4-column fair-top-1 rate",
         "passed": (mean(oracle_top) >= mean(d4_top)) if result["gateEvaluated"] else None,
         "observed": f"oracle {mean(oracle_top):.4f} vs fair D4 {mean(d4_top):.4f} over {len(pairs)} roots (ties count)"},
    ]
    result["gateChecks"] = checks
    if result["gateEvaluated"]:
        result["gateVerdict"] = "pass" if all(c["passed"] for c in checks) else "fail"
    else:
        result["gateVerdict"] = "not-evaluated"
    json.dump(result, open(sys.argv[3], "w"), indent=2)
    print(json.dumps({k: result[k] for k in ("pools", "R", "tau", "halves", "top1AtORoots", "bootstrap", "gateChecks", "gateVerdict")}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
