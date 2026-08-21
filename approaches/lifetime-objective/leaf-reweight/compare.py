#!/usr/bin/env python3
"""Paired whole-game comparison of leaf-reweight arms against a comparator.

The independent unit is a whole game (benchmarks.md, "Statistics and heavy
tails").  Arms play the same ordered cohort, so every statistic below is paired
by seed; the confidence statement is a one-sided 95% percentile bootstrap lower
bound on the mean paired delta, resampling whole games.

Usage: compare.py <baseline.json> <arm.json> [<arm.json> ...] [--resamples N]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "suite-validation")))
import stats  # noqa: E402


class Mulberry32:
    """Same generator the C++ harness uses, so bounds are reproducible here."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next_bits(self):
        self.state = (self.state + 0x6D2B79F5) & 0xFFFFFFFF
        z = self.state
        z = ((z ^ (z >> 15)) * (z | 1)) & 0xFFFFFFFF
        z = (z ^ (z + ((z ^ (z >> 7)) * (z | 61)) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return (z ^ (z >> 14)) & 0xFFFFFFFF


def bootstrap_lower(values, alpha=0.05, resamples=20000, seed=0xB0075EED):
    if len(values) < 2:
        return values[0] if values else 0.0
    rng = Mulberry32(seed)
    n = len(values)
    means = []
    for _ in range(resamples):
        total = 0.0
        for _ in range(n):
            total += values[(rng.next_bits() * n) >> 32]
        means.append(total / n)
    means.sort()
    position = alpha * (len(means) - 1)
    low = int(position)
    high = min(low + 1, len(means) - 1)
    weight = position - low
    return means[low] * (1 - weight) + means[high] * weight


def load(path):
    with open(path) as handle:
        data = json.load(handle)
    games = {g["seedHex"]: g for g in data["gamesDetail"]}
    return data, games


def quant(values, q):
    values = sorted(values)
    position = q * (len(values) - 1)
    low = int(position)
    high = min(low + 1, len(values) - 1)
    weight = position - low
    return values[low] * (1 - weight) + values[high] * weight


def summarize(label, data, games, order):
    scores = [games[s]["score"] for s in order]
    moves = [games[s]["moves"] for s in order]
    cleared = sum(games[s]["numberedCleared"] for s in order)
    revealed = sum(games[s]["coversRevealed"] for s in order)
    movetotal = sum(moves)
    return {
        "arm": label,
        "mean": stats.mean(scores),
        "median": quant(scores, 0.5),
        "q25": quant(scores, 0.25),
        "min": min(scores),
        "max": max(scores),
        "sd": stats.stdev(scores),
        "meanMoves": stats.mean(moves),
        "q25Moves": quant(moves, 0.25),
        "minMoves": min(moves),
        "censored": sum(1 for s in order if games[s]["censored"]),
        "clearsPerMove": cleared / movetotal,
        "revealsPerMove": revealed / movetotal,
        "occupied": stats.mean([games[s]["meanOccupiedCells"] for s in order]),
        "workPerMove": sum(games[s]["work"] for s in order) / movetotal,
        "scores": scores,
        "moves": moves,
    }


def main():
    argv = [a for a in sys.argv[1:]]
    resamples = 20000
    if "--resamples" in argv:
        i = argv.index("--resamples")
        resamples = int(argv[i + 1])
        del argv[i:i + 2]
    base_path, arm_paths = argv[0], argv[1:]
    base_data, base_games = load(base_path)
    order = [g["seedHex"] for g in base_data["gamesDetail"]]

    rows = [summarize("frozen (comparator)", base_data, base_games, order)]
    deltas = {}
    for path in arm_paths:
        data, games = load(path)
        missing = [s for s in order if s not in games]
        if missing:
            raise SystemExit(f"{path}: missing {len(missing)} cohort seeds")
        label = json.loads(json.dumps(data["config"]))["arm"] \
            if isinstance(data["config"], dict) else path
        row = summarize(label, data, games, order)
        rows.append(row)
        d = [games[s]["score"] - base_games[s]["score"] for s in order]
        dm = [games[s]["moves"] - base_games[s]["moves"] for s in order]
        deltas[label] = (d, dm)

    print("| arm | mean | median | Q25 | min | max | sd | mean moves | Q25 moves "
          "| censored | clears/move | reveals/move | occupied | work/move |")
    print("| --- |" + " ---: |" * 13)
    for row in rows:
        print(f"| {row['arm']} | {row['mean']:,.0f} | {row['median']:,.0f} | "
              f"{row['q25']:,.0f} | {row['min']:,.0f} | {row['max']:,.0f} | "
              f"{row['sd']:,.0f} | {row['meanMoves']:.2f} | {row['q25Moves']:.2f} | "
              f"{row['censored']} | {row['clearsPerMove']:.4f} | "
              f"{row['revealsPerMove']:.4f} | {row['occupied']:.2f} | "
              f"{row['workPerMove']:,.0f} |")

    print("\n| arm | paired delta score | 95% lower bound | delta moves | W-T-L | "
          "delta Q25 | delta min |")
    print("| --- |" + " ---: |" * 6)
    base = rows[0]
    for row in rows[1:]:
        d, dm = deltas[row["arm"]]
        wins = sum(1 for x in d if x > 0)
        losses = sum(1 for x in d if x < 0)
        ties = len(d) - wins - losses
        lower = bootstrap_lower(d, 0.05, resamples)
        print(f"| {row['arm']} | {stats.mean(d):+,.0f} | {lower:+,.0f} | "
              f"{stats.mean(dm):+.2f} | {wins}-{ties}-{losses} | "
              f"{row['q25'] - base['q25']:+,.0f} | "
              f"{row['min'] - base['min']:+,.0f} |")

    print("\n### Lower tail: the worst eight games of the comparator, paired\n")
    worst = sorted(order, key=lambda s: base_games[s]["score"])[:8]
    header = "| seed | frozen |" + "".join(f" {r['arm']} |" for r in rows[1:])
    print(header)
    print("| --- |" + " ---: |" * (len(rows)))
    for seed in worst:
        cells = [f"{base_games[seed]['score']:,}"]
        for path in arm_paths:
            _, games = load(path)
            cells.append(f"{games[seed]['score']:,}")
        print(f"| `{seed}` | " + " | ".join(cells) + " |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
