#!/usr/bin/env python3
"""Fit and extrapolate the fair planner's K series.

The fair (public-information) receding-horizon planner's sustained clear rate
rises with `K`, the number of sampled hidden-board completions it averages over
per decision.  The distillation programme needs the *asymptote* of that series,
because it is an upper bound on what any student distilled from this planner can
reach.

Two models are fitted to the pooled clears-per-move series, indexed by
x = log4(K):

    saturating   y = A - B * rho^x      (0 < rho < 1; asymptote A)
    log-linear   y = a + b * x          (no asymptote; still climbing)

The saturating model is compared with the log-linear one on residual sum of
squares, and a paired bootstrap over the eight master-tape seeds gives an
interval for A.  Pairing is preserved: one bootstrap replicate resamples seeds
and recomputes *every* K arm's pooled rate from the same resampled seed set.

A three-parameter saturating model fitted to five or six points is fragile, so
three guards are applied and all of them are reported:

  * `--min-k` refits on the tail only, where the successive gains are already
    decreasing, since the small-K regime is qualitatively different (there the
    planner is worse than the depth-4 comparator and largely noise-driven);
  * a bootstrap replicate whose best rho is >= 0.95 has *not* identified a
    finite asymptote, and the fraction of such replicates is reported rather
    than being averaged into an interval;
  * a transparent model-free estimate is printed alongside: if successive gains
    decay geometrically with ratio r taken from the last two observed gains, the
    remaining gain is last_gain * r / (1 - r).

Usage:
    extrapolate.py [--steady] [--min-k N] K=path.jsonl [K=path.jsonl ...]
"""
import json
import math
import random
import sys

SKIP = 25  # opening moves excluded when --steady is given


def load(path):
    with open(path) as handle:
        return [json.loads(line) for line in handle if line.strip()]


def per_game_rate(game, steady):
    """(clears, moves) for one game, optionally excluding the sparse opening."""
    if not steady:
        return game["cleared"], game["moves"]
    occupancy = game["moveOccupancy"]
    before = 7
    cleared = moves = 0
    for index, after in enumerate(occupancy):
        rise = 1 if (index + 1) % 5 == 0 else 0
        step = before + 1 + 7 * rise - after
        if step >= 0 and index >= SKIP:
            cleared += step
            moves += 1
        before = after
    return cleared, moves


def pooled(rows, order):
    """Pooled clears/move over a chosen multiset of seeds."""
    cleared = moves = 0
    for seed in order:
        c, m = rows[seed]
        cleared += c
        moves += m
    return cleared / moves if moves else 0.0


def fit_saturating(xs, ys):
    """Grid-search rho, solve A and B by least squares at each rho."""
    best = None
    rho = 0.02
    while rho < 0.999:
        # y = A - B*rho^x  ->  linear in (1, rho^x)
        u = [rho ** x for x in xs]
        n = len(xs)
        su, syy = sum(u), sum(ys)
        suu = sum(v * v for v in u)
        suy = sum(v * y for v, y in zip(u, ys))
        det = n * suu - su * su
        if abs(det) > 1e-12:
            neg_b = (n * suy - su * syy) / det
            a = (syy - neg_b * su) / n
            rss = sum((a + neg_b * v - y) ** 2 for v, y in zip(u, ys))
            if best is None or rss < best[0]:
                best = (rss, a, -neg_b, rho)
        rho += 0.002
    return best  # (rss, A, B, rho)


def fit_loglinear(xs, ys):
    n = len(xs)
    sx, sy = sum(xs), sum(ys)
    sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    det = n * sxx - sx * sx
    b = (n * sxy - sx * sy) / det
    a = (sy - b * sx) / n
    rss = sum((a + b * x - y) ** 2 for x, y in zip(xs, ys))
    return rss, a, b


def main():
    args = sys.argv[1:]
    steady = "--steady" in args
    args = [a for a in args if a != "--steady"]
    min_k = 0
    if "--min-k" in args:
        at = args.index("--min-k")
        min_k = int(args[at + 1])
        del args[at:at + 2]
    arms = []
    for arg in args:
        k, path = arg.split("=", 1)
        games = load(path)
        rows = {g["seed"]: per_game_rate(g, steady) for g in games}
        arms.append((int(k), rows))
    arms.sort()
    seeds = sorted(set.intersection(*[set(rows) for _k, rows in arms]))
    print(f"{len(seeds)} seeds common to all {len(arms)} arms; "
          f"{'steady-state (moves 26+)' if steady else 'whole-game'} rates")

    xs = [math.log(k, 4) for k, _ in arms]
    ys = [pooled(rows, seeds) for _k, rows in arms]
    print("\n   K   log4K   clears/move")
    for (k, _), x, y in zip(arms, xs, ys):
        print(f"{k:5d}  {x:5.2f}   {y:.4f}")
    print("\nsuccessive gains")
    for i in range(1, len(ys)):
        print(f"  K {arms[i-1][0]:>4} -> {arms[i][0]:<4}  {ys[i]-ys[i-1]:+.4f}")

    if min_k > 0:
        keep = [i for i, (k, _) in enumerate(arms) if k >= min_k]
        xs = [xs[i] for i in keep]
        ys = [ys[i] for i in keep]
        arms = [arms[i] for i in keep]
        print(f"\nfitting the tail only: K >= {min_k} ({len(xs)} points)")

    rss_sat, A, B, rho = fit_saturating(xs, ys)
    rss_lin, a, b = fit_loglinear(xs, ys)
    print(f"\nsaturating  y = {A:.4f} - {B:.4f} * {rho:.3f}^log4(K)   RSS {rss_sat:.6f}")
    print(f"log-linear  y = {a:.4f} + {b:.4f} * log4(K)              RSS {rss_lin:.6f}")
    print(f"asymptote A = {A:.4f}   ({100*A/2.4:.1f}% of the 2.400 requirement)")
    verdict = ("saturating fits better -> the series is converging"
               if rss_sat < rss_lin else
               "log-linear fits at least as well -> still climbing, no asymptote"
               " is identified")
    print(f"model comparison: {verdict}")

    # Model-free remaining-gain estimate from the last two observed gains.
    if len(ys) >= 3:
        g1, g2 = ys[-2] - ys[-3], ys[-1] - ys[-2]
        if g1 > 0 and 0 < g2 < g1:
            r = g2 / g1
            remaining = g2 * r / (1 - r)
            print(f"model-free: last two gains {g1:+.4f} then {g2:+.4f}, "
                  f"decay ratio {r:.3f} -> remaining {remaining:+.4f} "
                  f"-> asymptote ~{ys[-1] + remaining:.4f}")
        else:
            print("model-free: gains are not decaying geometrically at the "
                  "tail; no remaining-gain estimate")

    # Paired bootstrap over seeds.  Pairing is preserved: one replicate
    # resamples seeds and recomputes every arm from that same seed multiset.
    random.seed(20260820)
    draws = []
    unidentified = 0
    for _ in range(2000):
        order = [random.choice(seeds) for _ in seeds]
        bys = [pooled(rows, order) for _k, rows in arms]
        _rss, bA, _bB, brho = fit_saturating(xs, bys)
        if brho >= 0.95:
            unidentified += 1
        else:
            draws.append(bA)
    total = len(draws) + unidentified
    print(f"\npaired seed bootstrap (n={total}): "
          f"{100*unidentified/total:.1f}% of replicates do NOT identify a finite "
          f"asymptote (best rho >= 0.95)")
    if draws:
        draws.sort()
        lo = draws[int(0.05 * len(draws))]
        hi = draws[int(0.95 * len(draws)) - 1]
        med = draws[len(draws) // 2]
        print(f"  among the {len(draws)} identified replicates: A median "
              f"{med:.4f}, 90% interval [{lo:.4f}, {hi:.4f}]")
    print(f"\nprojected values from the point fit: "
          + "  ".join(f"K={4**e}:{A - B*rho**e:.4f}" for e in (5, 6, 7, 8)))


if __name__ == "__main__":
    main()
