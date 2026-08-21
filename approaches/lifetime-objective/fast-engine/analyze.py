#!/usr/bin/env python3
"""Paired whole-game analysis of drop7-lifetime-cohort-v1 artifacts.

The independent unit is a whole game.  Arms are paired by seed and compared
with a one-sided percentile bootstrap over whole games, using the same
resampler, resample count, alpha and RNG domain as
approaches/lifetime-objective/common/harness.hpp so that the numbers here are
directly comparable with finding-05's.

Usage:
  analyze.py summary  <artifact.json> [...]
  analyze.py paired   <candidate.json> <comparator.json> [...]
"""
import json
import math
import sys

MASK = 0xFFFFFFFF


class Mulberry32:
    """Bit-identical to drop7::Mulberry32 in src/core/native/engine.hpp."""

    def __init__(self, seed):
        self.state = seed & MASK

    def next_bits(self):
        self.state = (self.state + 0x6D2B79F5) & MASK
        v = self.state
        v = ((v ^ (v >> 15)) * (v | 1)) & MASK
        v ^= (v + (((v ^ (v >> 7)) * (v | 61)) & MASK)) & MASK
        v &= MASK
        return (v ^ (v >> 14)) & MASK


def quantile(values, q):
    if not values:
        return 0.0
    s = sorted(values)
    pos = q * (len(s) - 1)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    w = pos - lo
    return s[lo] * (1.0 - w) + s[hi] * w


def bootstrap_lower(values, alpha=0.05, resamples=20000, seed=0xB0075EED):
    """One-sided percentile-bootstrap lower bound on the mean, over whole games."""
    n = len(values)
    if n < 2:
        return values[0] if values else 0.0
    rng = Mulberry32(seed)
    means = []
    for _ in range(resamples):
        total = 0.0
        for _ in range(n):
            total += values[(rng.next_bits() * n) >> 32]
        means.append(total / n)
    return quantile(means, alpha)


def load(path):
    d = json.load(open(path))
    d["_games"] = {g["seedHex"]: g for g in d["gamesDetail"]}
    d["_path"] = path
    return d


def sd(values):
    n = len(values)
    if n < 2:
        return 0.0
    m = sum(values) / n
    return math.sqrt(sum((v - m) ** 2 for v in values) / (n - 1))


def summary(path):
    d = load(path)
    g = d["gamesDetail"]
    scores = [x["score"] for x in g]
    moves = [x["moves"] for x in g]
    cfg = d["config"]
    label = "d%s s%s" % (cfg.get("depth"), cfg.get("chanceSamples"))
    print("== %s  (%s)  %s" % (label, path, d.get("policy")))
    print("   games %d  seeds %s  cap %s  censored %d  identityFailures %d"
          % (len(g), d["seedStartHex"], d["maximumMoves"], d["censoredGames"],
             d["scoreIdentityFailures"]))
    print("   score  mean %10.1f  median %9.1f  q25 %9.1f  min %8d  max %9d  sd %10.1f"
          % (sum(scores) / len(scores), quantile(scores, 0.5),
             quantile(scores, 0.25), min(scores), max(scores), sd(scores)))
    print("   moves  mean %10.2f  q25 %9.2f  min %8d  max %9d"
          % (sum(moves) / len(moves), quantile(moves, 0.25), min(moves), max(moves)))
    print("   clears/move %.4f   reveals/move %.4f   occupied %.4f   rises/game %.3f"
          % (d["numberedClearsPerMove"], d["coverRevealsPerMove"],
             d["meanOccupiedCells"], d["risesPerGame"]))
    print("   work/move %.0f   levelShare %.4f  chainShare %.4f  maxChainDepth %d"
          % (d["workPerMove"], d["decomposition"]["levelShare"],
             d["decomposition"]["chainShare"], d["maxChainDepth"]))
    if "incompleteDecisions" in cfg:
        print("   AUDIT decisions %d  incomplete %d  minCompletedDepth %s  "
              "maxWork/decision %d of %d  maxCacheUsed %d of %d"
              % (cfg["decisions"], cfg["incompleteDecisions"],
                 cfg["minimumCompletedDepth"], cfg["maximumWorkPerDecision"],
                 cfg["maximumWork"], cfg["maximumCacheEntriesUsed"],
                 cfg["maximumCacheEntries"]))
    print()
    return d


def paired(candidate_path, comparator_path):
    a = load(candidate_path)
    b = load(comparator_path)
    seeds = sorted(set(a["_games"]) & set(b["_games"]))
    if not seeds:
        print("no shared seeds between %s and %s" % (candidate_path, comparator_path))
        return
    ds = [a["_games"][s]["score"] - b["_games"][s]["score"] for s in seeds]
    dm = [a["_games"][s]["moves"] - b["_games"][s]["moves"] for s in seeds]
    wins = sum(1 for d in ds if d > 0)
    ties = sum(1 for d in ds if d == 0)
    losses = sum(1 for d in ds if d < 0)
    ca = a["config"]
    cb = b["config"]

    def name(d, c):
        return "d%s s%s" % (c.get("depth"), c.get("chanceSamples"))

    lo_s = bootstrap_lower(ds)
    lo_m = bootstrap_lower(dm)
    print("== %s minus %s   (%d paired games)" % (name(a, ca), name(b, cb), len(seeds)))
    print("   delta score  mean %+10.1f   95%% lower bound %+10.1f   %s"
          % (sum(ds) / len(ds), lo_s,
             "SIGNIFICANT" if lo_s > 0 else "not significant"))
    print("   delta moves  mean %+10.2f   95%% lower bound %+10.2f"
          % (sum(dm) / len(dm), lo_m))
    print("   W-T-L %d-%d-%d   median delta %+.1f" % (wins, ties, losses, quantile(ds, 0.5)))
    print("   flow: clears/move %.4f vs %.4f (%+.4f);  reveals/move %.4f vs %.4f (%+.4f)"
          % (a["numberedClearsPerMove"], b["numberedClearsPerMove"],
             a["numberedClearsPerMove"] - b["numberedClearsPerMove"],
             a["coverRevealsPerMove"], b["coverRevealsPerMove"],
             a["coverRevealsPerMove"] - b["coverRevealsPerMove"]))
    print("   occupied %.4f vs %.4f (%+.4f);  work/move %.0f vs %.0f (%.2fx)"
          % (a["meanOccupiedCells"], b["meanOccupiedCells"],
             a["meanOccupiedCells"] - b["meanOccupiedCells"],
             a["workPerMove"], b["workPerMove"],
             a["workPerMove"] / b["workPerMove"] if b["workPerMove"] else 0.0))
    print()


def identical(candidate_path, comparator_path):
    """Whole-game reproduction check: every field of every game must match."""
    a = load(candidate_path)
    b = load(comparator_path)
    seeds = sorted(set(a["_games"]) & set(b["_games"]))
    fields = ["score", "moves", "censored", "rises", "boardClears", "levelPoints",
              "clearPoints", "chainPoints", "numberedCleared", "coversRevealed",
              "maxChainDepth"]
    bad = []
    for s in seeds:
        for f in fields:
            if a["_games"][s][f] != b["_games"][s][f]:
                bad.append((s, f, a["_games"][s][f], b["_games"][s][f]))
    print("== reproduction check: %s vs %s" % (candidate_path, comparator_path))
    print("   %d paired games x %d fields = %d comparisons, %d mismatches"
          % (len(seeds), len(fields), len(seeds) * len(fields), len(bad)))
    for row in bad[:10]:
        print("   MISMATCH seed %s field %s: %s vs %s" % row)
    print("   %s" % ("IDENTICAL" if not bad else "NOT IDENTICAL"))
    print()
    return not bad


if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "summary":
        for p in sys.argv[2:]:
            summary(p)
    elif mode == "paired":
        args = sys.argv[2:]
        for i in range(0, len(args), 2):
            paired(args[i], args[i + 1])
    elif mode == "identical":
        ok = True
        args = sys.argv[2:]
        for i in range(0, len(args), 2):
            ok &= identical(args[i], args[i + 1])
        sys.exit(0 if ok else 1)
    else:
        print(__doc__)
        sys.exit(2)
