#!/usr/bin/env python3
"""Summarize `flow-run` game records for the flow-ceiling finding.

Reads one or more `drop7-flow-game-v1` JSONL files written by
`build/flow-ceiling/flow-run --jsonl ...` and prints the tables the finding
document needs: pooled and per-game flow rates, the occupancy trend across
five-move cycles, score composition, the wave-depth histogram, and the
occupancy-conditional clear rate.

The occupancy-conditional table is the decisive one.  Disc conservation is
exact: during move `i` the board gains the placed disc, loses every numbered
disc the cascades clear, and gains seven more if that move ends a five-move
cycle.  So

    cleared_i = occupied_before_i + 1 + 7 * rise_i - occupied_after_i

and the per-move clear count can be reconstructed exactly from the recorded
`moveOccupancy` array without instrumenting the engine further.  The script
checks the reconstruction against the game's own `cleared` total and refuses to
report a game whose arithmetic does not close.

Usage:
    analyze.py <games.jsonl> [<games.jsonl> ...]
"""

import json
import sys
from collections import defaultdict

MOVES_PER_LEVEL = 5
CELL_COUNT = 49
INITIAL_OCCUPIED = 7  # the covered bottom row of `drop7::initialBoard()`
REQUIRED_CLEARS = 12.0 / 5.0
REQUIRED_REVEALS = 7.0 / 5.0


def reconstruct_per_move(game):
    """Returns [(occupied_before, cleared, rise)] or None if it does not close."""
    occupancy = game["moveOccupancy"]
    rows = []
    before = INITIAL_OCCUPIED
    total = 0
    for index, after in enumerate(occupancy):
        move = index + 1
        rise = 1 if move % MOVES_PER_LEVEL == 0 else 0
        cleared = before + 1 + 7 * rise - after
        if cleared < 0:
            # Only possible on the final move, where the rise itself failed and
            # the game ended.  Drop it rather than guess.
            rows.append((before, None, rise))
            before = after
            continue
        rows.append((before, cleared, rise))
        total += cleared
        before = after
    if total != game["cleared"]:
        # A failed rise on the last move removes its seven discs from the
        # arithmetic; allow exactly that one discrepancy.
        if total - 7 == game["cleared"] and rows:
            rows[-1] = (rows[-1][0], rows[-1][1] - 7, 0)
            total -= 7
        if total != game["cleared"]:
            return None
    return rows


def load(path):
    games = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                games.append(json.loads(line))
    return games


def mean(values):
    return sum(values) / len(values) if values else 0.0


def median(values):
    if not values:
        return 0.0
    ordered = sorted(values)
    half = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[half]
    return 0.5 * (ordered[half - 1] + ordered[half])


def summarize(path):
    games = load(path)
    if not games:
        print(f"{path}: no games")
        return
    policy = games[0]["policy"]
    print(f"\n################ {path}  ({policy}, {len(games)} games)")

    moves = sum(g["moves"] for g in games)
    cleared = sum(g["cleared"] for g in games)
    revealed = sum(g["revealed"] for g in games)
    score = sum(g["score"] for g in games)
    rise_points = sum(g["risePoints"] for g in games)
    clear_points = sum(g["clearPoints"] for g in games)
    chain_points = sum(g["chainPoints"] for g in games)
    censored = sum(1 for g in games if g["censored"])

    print(f"moves      mean {mean([g['moves'] for g in games]):8.2f}  "
          f"median {median([g['moves'] for g in games]):7.1f}  "
          f"min {min(g['moves'] for g in games)}  "
          f"max {max(g['moves'] for g in games)}  censored {censored}")
    print(f"score      mean {mean([g['score'] for g in games]):10.1f}  "
          f"median {median([g['score'] for g in games]):10.1f}")
    print(f"flow       clears/move {cleared / moves:.4f} "
          f"({100 * (cleared / moves) / REQUIRED_CLEARS:.1f}% of 2.4000)   "
          f"reveals/move {revealed / moves:.4f} "
          f"({100 * (revealed / moves) / REQUIRED_REVEALS:.1f}% of 1.4000)")
    print(f"score      rise {100 * rise_points / score:.2f}%  "
          f"boardClear {100 * clear_points / score:.2f}%  "
          f"chain {100 * chain_points / score:.2f}%")
    print(f"clears     70k awards {sum(g['clearAwards'] for g in games)}  "
          f"double awards {sum(g['doubleClearAwards'] for g in games)}  "
          f"fifth-drop {sum(g['fifthDropClears'] for g in games)}")
    print(f"checks     identity violations "
          f"{sum(g['identityViolations'] for g in games)}  "
          f"incomplete windows {sum(g['incompleteWindows'] for g in games)}  "
          f"pv mismatches {sum(g['pvMismatches'] for g in games)}")

    waves = defaultdict(int)
    wave_cleared = defaultdict(int)
    for game in games:
        for depth, count in game["waveDepthCount"].items():
            waves[int(depth)] += count
        for depth, count in game["waveDepthCleared"].items():
            wave_cleared[int(depth)] += count
    total_waves = sum(waves.values())
    print(f"\nwave depth histogram ({total_waves} waves, deepest "
          f"{max(waves) if waves else 0})")
    print("depth   waves    share    discs")
    for depth in sorted(waves):
        print(f"{depth:5d}  {waves[depth]:6d}  "
              f"{100 * waves[depth] / total_waves:6.2f}%  "
              f"{wave_cleared[depth]:7d}")

    print("\noccupancy after each five-move cycle")
    print("cycle  games  mean occupied  mean covered")
    max_cycles = max(len(g["cycleOccupancy"]) for g in games)
    for cycle in range(max_cycles):
        occupied = [g["cycleOccupancy"][cycle] for g in games
                    if cycle < len(g["cycleOccupancy"])]
        covered = [g["cycleCovered"][cycle] for g in games
                   if cycle < len(g["cycleCovered"])]
        if not occupied:
            continue
        print(f"{cycle + 1:5d}  {len(occupied):5d}  {mean(occupied):13.2f}  "
              f"{mean(covered):12.2f}")

    # Occupancy-conditional clear rate.
    bins = [(0, 9), (10, 14), (15, 19), (20, 24), (25, 29), (30, 34),
            (35, 39), (40, 44), (45, CELL_COUNT)]
    totals = {b: [0, 0] for b in bins}   # [moves, cleared]
    bad = 0
    for game in games:
        rows = reconstruct_per_move(game)
        if rows is None:
            bad += 1
            continue
        for before, cleared_i, _rise in rows:
            if cleared_i is None:
                continue
            for low, high in bins:
                if low <= before <= high:
                    totals[(low, high)][0] += 1
                    totals[(low, high)][1] += cleared_i
                    break
    print(f"\nclear rate conditional on board occupancy before the move "
          f"({bad} game(s) failed the reconstruction check)")
    print("occupied   moves   clears/move   vs 2.400 required")
    for b in bins:
        count, total = totals[b]
        if count == 0:
            continue
        rate = total / count
        print(f"{b[0]:3d}-{b[1]:<3d}  {count:6d}   {rate:11.4f}   "
              f"{rate - REQUIRED_CLEARS:+.4f}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for path in sys.argv[1:]:
        summarize(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
