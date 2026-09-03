"""Paired statistics for an evaluate.json cohort artifact
(EX-20260902-kf-linear-q-rust-transfer-4328a730).

Usage: summarize_evaluate.py EVALUATE_JSON [MORE_JSON ...] --reference random --baseline ARM [--out summary.json]

For every arm: whole-game score and lifetime summaries (mean, median, lower
quartile, min, max), censored games, clears and reveals per move, chain
depths, illegal and incomplete decisions, wall time.  For every arm against
the reference arm (and against the baseline arm) on the seeds both played:
mean paired delta, one-sided 95% percentile-bootstrap lower bound (10,000
resamples, RNG seed 0x6b660001), and the paired detection floor
1.645 * sd(delta) / sqrt(n).  Pure standard library so the numbers can be
recomputed anywhere.
"""
import argparse
import json
import math
import random
import statistics

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="+", help="one or more evaluate.json artifacts; arms are merged")
ap.add_argument("--reference", default="random")
ap.add_argument("--baseline", default=None, help="second comparison arm, e.g. the upstream-schedule arm")
ap.add_argument("--resamples", type=int, default=10000)
ap.add_argument("--out", default=None)
args = ap.parse_args()

data = None
arms = {}
for path in args.paths:
    part = json.load(open(path))
    if data is None:
        data = part
    else:
        assert part["seedsStartHex"] == data["seedsStartHex"] and part["moveCap"] == data["moveCap"], "artifacts must share the cohort"
    for a in part["arms"]:
        assert a["name"] not in arms, f"duplicate arm {a['name']}"
        arms[a["name"]] = a


def quantile(sorted_xs, q):
    if not sorted_xs:
        return None
    pos = (len(sorted_xs) - 1) * q
    lo, hi = math.floor(pos), math.ceil(pos)
    return sorted_xs[lo] + (sorted_xs[hi] - sorted_xs[lo]) * (pos - lo)


def describe(rows):
    scores = sorted(r["score"] for r in rows)
    moves = sorted(r["moves"] for r in rows)
    total_moves = max(1, sum(moves))
    return {
        "games": len(rows),
        "score": {"mean": statistics.mean(scores), "sd": statistics.stdev(scores) if len(scores) > 1 else 0.0,
                  "median": quantile(scores, 0.5), "q25": quantile(scores, 0.25), "min": scores[0], "max": scores[-1]},
        "moves": {"mean": statistics.mean(moves), "median": quantile(moves, 0.5), "q25": quantile(moves, 0.25),
                  "min": moves[0], "max": moves[-1]},
        "censoredGames": sum(1 for r in rows if r["censored"]),
        "moveCap": data["moveCap"],
        "numberedClearsPerMove": sum(r["numberedClears"] for r in rows) / total_moves,
        "coveredRevealsPerMove": sum(r["coveredReveals"] for r in rows) / total_moves,
        "meanChainDepth": statistics.mean(r["meanChainDepth"] for r in rows),
        "maximumChainDepth": max(r["maximumChainDepth"] for r in rows),
        "illegalDecisions": sum(r["illegalDecisions"] for r in rows),
        "incompleteDecisions": sum(r["incompleteDecisions"] for r in rows),
        "logicalWorkPerMove": sum(r["logicalWork"] for r in rows) / total_moves,
        "wallSecondsPerGame": statistics.mean(r["wallSeconds"] for r in rows),
    }


def paired(cand, ref, key):
    by_ord = {r["cohortOrdinal"]: r for r in ref["rows"]}
    deltas = [r[key] - by_ord[r["cohortOrdinal"]][key] for r in cand["rows"] if r["cohortOrdinal"] in by_ord]
    n = len(deltas)
    if n < 2:
        return None
    mean = statistics.mean(deltas)
    sd = statistics.stdev(deltas)
    rng = random.Random(0x6B660001)
    means = []
    for _ in range(args.resamples):
        s = 0.0
        for _ in range(n):
            s += deltas[rng.randrange(n)]
        means.append(s / n)
    means.sort()
    lb = means[int(0.05 * args.resamples)]
    wins = sum(1 for d in deltas if d > 0)
    ties = sum(1 for d in deltas if d == 0)
    return {"n": n, "meanDelta": mean, "sdDelta": sd, "lowerBound95OneSided": lb,
            "detectionFloor": 1.645 * sd / math.sqrt(n), "wins": wins, "ties": ties, "losses": n - wins - ties,
            "bootstrap": {"resamples": args.resamples, "rngSeedHex": "0x6b660001", "method": "percentile"}}


summary = {"source": args.paths, "seedsStartHex": data["seedsStartHex"], "games": data["games"], "moveCap": data["moveCap"],
           "arms": {}, "pairedVsReference": {}, "pairedVsBaseline": {}}
for name, arm in arms.items():
    summary["arms"][name] = describe(arm["rows"])
ref = arms.get(args.reference)
base = arms.get(args.baseline) if args.baseline else None
for name, arm in arms.items():
    if ref and name != args.reference:
        summary["pairedVsReference"][name] = {"score": paired(arm, ref, "score"), "moves": paired(arm, ref, "moves")}
    if base and name != args.baseline:
        summary["pairedVsBaseline"][name] = {"score": paired(arm, base, "score"), "moves": paired(arm, base, "moves")}

print(f"{'arm':28s} {'n':>4s} {'mean score':>11s} {'median':>9s} {'q25':>9s} {'mean moves':>10s} {'cens':>4s} {'clr/mv':>6s} {'ill':>3s}")
for name, d in summary["arms"].items():
    print(f"{name:28s} {d['games']:4d} {d['score']['mean']:11.0f} {d['score']['median']:9.0f} {d['score']['q25']:9.0f} {d['moves']['mean']:10.2f} {d['censoredGames']:4d} {d['numberedClearsPerMove']:6.3f} {d['illegalDecisions']:3d}")
for label, table in (("vs " + args.reference, summary["pairedVsReference"]), ("vs " + str(args.baseline), summary["pairedVsBaseline"])):
    if not table:
        continue
    print(f"\npaired {label}: mean delta [one-sided 95% LB] (detection floor) W/T/L")
    for name, p in table.items():
        s, m = p["score"], p["moves"]
        if s is None:
            continue
        print(f"{name:28s} score {s['meanDelta']:+10.0f} [{s['lowerBound95OneSided']:+10.0f}] ({s['detectionFloor']:8.0f})  moves {m['meanDelta']:+7.2f} [{m['lowerBound95OneSided']:+7.2f}] ({m['detectionFloor']:5.2f})  {s['wins']}/{s['ties']}/{s['losses']}")
if args.out:
    json.dump(summary, open(args.out, "w"), indent=2)
