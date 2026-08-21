#!/usr/bin/env python3
"""Paired whole-game comparison of two `flow-run` cohorts on shared master tapes.

Whole games are the statistical unit (`docs/methodology.md`).  Both cohorts must
have been run on the same master-tape seeds and the same `--max-moves`, since a
master tape is a function of both.  Pairing is preserved through the bootstrap:
one replicate resamples *seeds* and recomputes both arms from that same seed
multiset, so common randomness is never broken.

Reported per metric: the candidate mean, the comparator mean, the paired delta,
and a **one-sided 95% bootstrap lower bound** on that delta.  A lower bound at
or below zero means the cohort does not establish an advantage.

Usage:
    paired.py <candidate.jsonl> <comparator.jsonl> [--label-a A] [--label-b B]
"""
import json
import random
import sys


def load(path):
    with open(path) as handle:
        return {g["seed"]: g for g in
                (json.loads(line) for line in handle if line.strip())}


def slope(values, skip=1):
    n = len(values) - skip
    if n < 3:
        return 0.0
    xs = list(range(n))
    ys = values[skip:]
    sx, sy = sum(xs), sum(ys)
    sxy = sum(x * y for x, y in zip(xs, ys))
    sxx = sum(x * x for x in xs)
    det = n * sxx - sx * sx
    return (n * sxy - sx * sy) / det if det else 0.0


def stats(games, seeds):
    moves = sum(games[s]["moves"] for s in seeds)
    cleared = sum(games[s]["cleared"] for s in seeds)
    revealed = sum(games[s]["revealed"] for s in seeds)
    n = len(seeds)
    return {
        "meanMoves": moves / n,
        "meanScore": sum(games[s]["score"] for s in seeds) / n,
        "clearsPerMove": cleared / moves if moves else 0.0,
        "revealsPerMove": revealed / moves if moves else 0.0,
        "occSlope": sum(slope(games[s]["cycleOccupancy"]) for s in seeds) / n,
        "censored": sum(1 for s in seeds if games[s]["censored"]) / n,
    }


METRICS = [
    ("mean moves", "meanMoves"),
    ("mean score", "meanScore"),
    ("clears/move (pooled)", "clearsPerMove"),
    ("reveals/move (pooled)", "revealsPerMove"),
    ("occupancy slope", "occSlope"),
    ("censored fraction", "censored"),
]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    labels = {}
    argv = sys.argv[1:]
    for flag, key in (("--label-a", "a"), ("--label-b", "b")):
        if flag in argv:
            labels[key] = argv[argv.index(flag) + 1]
    a_path, b_path = args[0], args[1]
    A, B = load(a_path), load(b_path)
    seeds = sorted(set(A) & set(B))
    if not seeds:
        print("no shared master-tape seeds; these cohorts are not paired")
        return 2
    la = labels.get("a", a_path.rsplit("/", 1)[-1])
    lb = labels.get("b", b_path.rsplit("/", 1)[-1])
    print(f"paired on {len(seeds)} shared master tapes "
          f"({len(A)} vs {len(B)} games available)")
    sa, sb = stats(A, seeds), stats(B, seeds)

    random.seed(20260820)
    replicates = 4000
    draws = {key: [] for _n, key in METRICS}
    for _ in range(replicates):
        order = [random.choice(seeds) for _ in seeds]
        ra, rb = stats(A, order), stats(B, order)
        for _n, key in METRICS:
            draws[key].append(ra[key] - rb[key])

    print(f"\n{'metric':<24}{la[:14]:>15}{lb[:14]:>15}{'delta':>13}"
          f"{'95% lower':>13}   verdict")
    for name, key in METRICS:
        d = sorted(draws[key])
        lo = d[int(0.05 * replicates)]
        delta = sa[key] - sb[key]
        if lo > 0:
            verdict = "advantage established"
        elif d[int(0.95 * replicates)] < 0:
            verdict = "DISADVANTAGE established"
        else:
            verdict = "straddles zero"
        print(f"{name:<24}{sa[key]:>15.4f}{sb[key]:>15.4f}{delta:>+13.4f}"
              f"{lo:>+13.4f}   {verdict}")

    wins = sum(1 for s in seeds if A[s]["moves"] > B[s]["moves"])
    ties = sum(1 for s in seeds if A[s]["moves"] == B[s]["moves"])
    print(f"\nper-tape lifetime record: {wins} win / {ties} tie / "
          f"{len(seeds)-wins-ties} loss")
    return 0


if __name__ == "__main__":
    sys.exit(main())
