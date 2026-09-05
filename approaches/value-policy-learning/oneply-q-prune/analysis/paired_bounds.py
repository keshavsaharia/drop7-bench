#!/usr/bin/env python3
"""Two-sided percentile-bootstrap bounds for paired per-game deltas.

  paired_bounds.py EVALUATE_JSON [MORE ...] --reference ARM --out FILE

For every other arm on the seeds it shares with the reference: mean score and
lifetime deltas, one-sided 95% lower AND upper percentile-bootstrap bounds
(10,000 resamples, RNG seed 0x6b660001), the detection floor
1.645 sd/sqrt(n), wins/ties/losses, and the ratio of mean logical work per
move.  Pure standard library.
"""
import argparse
import json
import math
import random
import statistics

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="+")
ap.add_argument("--reference", required=True)
ap.add_argument("--resamples", type=int, default=10000)
ap.add_argument("--seed", type=lambda s: int(s, 0), default=0x6B660001)
ap.add_argument("--out", required=True)
args = ap.parse_args()

arms = {}
for path in args.paths:
    part = json.load(open(path))
    for a in part["arms"]:
        arms[a["name"]] = a
ref = {r["seedHex"]: r for r in arms[args.reference]["rows"]}


def bounds(deltas, rng):
    n = len(deltas)
    means = []
    for _ in range(args.resamples):
        means.append(sum(deltas[rng.randrange(n)] for _ in range(n)) / n)
    means.sort()
    lo = means[int(0.05 * (args.resamples - 1))]
    hi = means[int(0.95 * (args.resamples - 1))]
    sd = statistics.stdev(deltas) if n > 1 else 0.0
    return {"n": n, "meanDelta": sum(deltas) / n, "sdDelta": sd, "lb95": lo, "ub95": hi,
            "detectionFloor": 1.645 * sd / math.sqrt(n),
            "wins": sum(1 for d in deltas if d > 0), "ties": sum(1 for d in deltas if d == 0),
            "losses": sum(1 for d in deltas if d < 0)}


out = {"reference": args.reference, "resamples": args.resamples, "seed": hex(args.seed), "arms": {}}
for name, arm in arms.items():
    if name == args.reference:
        continue
    rows = [(r, ref[r["seedHex"]]) for r in arm["rows"] if r["seedHex"] in ref]
    rng = random.Random(args.seed)
    score = bounds([a["score"] - b["score"] for a, b in rows], rng)
    rng = random.Random(args.seed)
    moves = bounds([a["moves"] - b["moves"] for a, b in rows], rng)
    work_a = sum(a["logicalWork"] for a, _ in rows) / max(1, sum(a["moves"] for a, _ in rows))
    work_b = sum(b["logicalWork"] for _, b in rows) / max(1, sum(b["moves"] for _, b in rows))
    out["arms"][name] = {"score": score, "moves": moves, "workPerMoveRatio": work_a / work_b if work_b else None,
                         "meanScore": sum(a["score"] for a, _ in rows) / len(rows),
                         "referenceMeanScore": sum(b["score"] for _, b in rows) / len(rows)}
    print("%s vs %s (n=%d): score %+.0f [LB %+.0f, UB %+.0f] floor %.0f W/T/L %d/%d/%d; moves %+.2f [LB %+.2f, UB %+.2f] floor %.2f; work ratio %.3f" % (
        name, args.reference, score["n"], score["meanDelta"], score["lb95"], score["ub95"], score["detectionFloor"],
        score["wins"], score["ties"], score["losses"], moves["meanDelta"], moves["lb95"], moves["ub95"], moves["detectionFloor"],
        out["arms"][name]["workPerMoveRatio"]))
json.dump(out, open(args.out, "w"), indent=2)
