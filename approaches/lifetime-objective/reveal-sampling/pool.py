#!/usr/bin/env python3
"""Pool sequential seed-block chunks into one cohort artifact.

A 64-game cohort run as four 16-game chunks over consecutive seed blocks plays
exactly the same 64 seeds, once each, with the same policy and the same work
bound.  Whole games are the statistical unit and each game is independent given
its seed, so the pooled cohort is statistically identical to a single 64-game
run; only wall time and thread scheduling differ.

This script recomputes every aggregate with the same formulas the C++ writer
uses (harness.hpp writeArtifact), including the Mulberry32 percentile bootstrap,
so a pooled artifact is interchangeable with a single-run artifact.  Chunk
diagnostics are combined conservatively: counts add, minCompletedDepth takes the
minimum, maxDecisionWork takes the maximum.

Usage: pool.py out.json chunk0.json chunk1.json ...
"""
import json
import math
import sys

CLEAR_BONUS = None  # identity failures are carried from the chunks


class Mulberry32:
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next_bits(self):
        self.state = (self.state + 0x6D2B79F5) & 0xFFFFFFFF
        value = self.state
        value = ((value ^ (value >> 15)) * (value | 1)) & 0xFFFFFFFF
        value ^= (value + ((value ^ (value >> 7)) * (value | 61))) & 0xFFFFFFFF
        value &= 0xFFFFFFFF
        return (value ^ (value >> 14)) & 0xFFFFFFFF


def quantile(values, q):
    values = sorted(values)
    if not values:
        return 0.0
    position = q * (len(values) - 1)
    low = math.floor(position)
    high = math.ceil(position)
    weight = position - low
    return values[low] * (1.0 - weight) + values[high] * weight


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
    return quantile(means, alpha)


def pool(out_path, chunk_paths):
    chunks = []
    for path in chunk_paths:
        with open(path) as handle:
            chunks.append(json.load(handle))
    base = chunks[0]

    for other in chunks[1:]:
        for key in ("depth", "discSamples", "revealSamples", "maximumWork",
                    "terminalUtility"):
            if base["config"][key] != other["config"][key]:
                raise SystemExit(f"chunk config mismatch on {key}")
        if base["maximumMoves"] != other["maximumMoves"]:
            raise SystemExit("chunk move-cap mismatch")

    games = []
    for chunk in chunks:
        games.extend(chunk["gamesDetail"])
    seen = [g["seedHex"] for g in games]
    if len(set(seen)) != len(seen):
        raise SystemExit("duplicate seeds across chunks")
    games.sort(key=lambda g: int(g["seedHex"], 16))

    n = len(games)
    scores = [g["score"] for g in games]
    moves = [g["moves"] for g in games]
    move_total = sum(moves)
    score_total = sum(scores)
    work_total = sum(g["work"] for g in games)
    cleared = sum(g["numberedCleared"] for g in games)
    revealed = sum(g["coversRevealed"] for g in games)

    mean_score = score_total / n
    mean_moves = move_total / n
    score_sd = math.sqrt(sum((s - mean_score) ** 2 for s in scores) / (n - 1))
    move_sd = math.sqrt(sum((m - mean_moves) ** 2 for m in moves) / (n - 1))

    level = sum(g["levelPoints"] for g in games)
    clear = sum(g["clearPoints"] for g in games)
    chain = sum(g["chainPoints"] for g in games)

    rise_values = [g["meanTopOccupiedRowAtRise"] for g in games
                   if g["meanTopOccupiedRowAtRise"] >= 0.0]

    histogram = [0] * len(base["waveDepthHistogram"])
    for chunk in chunks:
        for index, value in enumerate(chunk["waveDepthHistogram"]):
            histogram[index] += value

    config = dict(base["config"])
    for key, combine in (("decisions", sum), ("decisionsBelowTargetDepth", sum),
                         ("workLimitEvents", sum), ("minCompletedDepth", min),
                         ("maxDecisionWork", max)):
        if key in config:
            config[key] = combine(c["config"][key] for c in chunks)
    config["maximumCacheEntries"] = max(c["config"]["maximumCacheEntries"]
                                        for c in chunks)
    config["pooledFromChunks"] = len(chunks)
    config["pooledChunkSeedStarts"] = [c["seedStartHex"] for c in chunks]

    pooled = {
        "format": base["format"],
        "policy": base["policy"],
        "config": config,
        "seedLease": base["seedLease"],
        "dataRole": base["dataRole"],
        "seedStartHex": games[0]["seedHex"],
        "games": n,
        "maximumMoves": base["maximumMoves"],
        "threads": base["threads"],
        "wallSeconds": sum(c["wallSeconds"] for c in chunks),
        "scoreIdentityFailures": sum(c["scoreIdentityFailures"] for c in chunks),
        "score": {
            "mean": mean_score, "median": quantile(scores, 0.5),
            "q25": quantile(scores, 0.25), "min": min(scores),
            "max": max(scores), "sd": score_sd,
            "bootstrapLower95": bootstrap_lower([float(s) for s in scores]),
        },
        "moves": {
            "mean": mean_moves, "median": quantile(moves, 0.5),
            "q25": quantile(moves, 0.25), "min": min(moves),
            "max": max(moves), "sd": move_sd,
        },
        "censoredGames": sum(1 for g in games if g["censored"]),
        "decomposition": {
            "levelPointsTotal": level, "clearPointsTotal": clear,
            "chainPointsTotal": chain, "scoreTotal": score_total,
            "levelShare": level / score_total, "clearShare": clear / score_total,
            "chainShare": chain / score_total,
        },
        "risesPerGame": sum(g["rises"] for g in games) / n,
        "boardClearsPerGame": sum(g["boardClears"] for g in games) / n,
        "numberedClearsPerMove": cleared / move_total,
        "coverRevealsPerMove": revealed / move_total,
        "requiredClearsPerMove": 2.4,
        "requiredRevealsPerMove": 1.4,
        "maxChainDepth": max(g["maxChainDepth"] for g in games),
        "meanTopOccupiedRowAtRise": (sum(rise_values) / len(rise_values)
                                     if rise_values else -1.0),
        "meanOccupiedCells": sum(g["meanOccupiedCells"] for g in games) / n,
        "pointsPerMove": score_total / move_total,
        "workPerMove": work_total / move_total,
        "waveDepthHistogram": histogram,
        "gamesDetail": games,
    }
    with open(out_path, "w") as handle:
        json.dump(pooled, handle, indent=1)
    print(f"pooled {n} games from {len(chunks)} chunks -> {out_path}: "
          f"mean {mean_score:,.0f} moves {mean_moves:.2f} "
          f"censored {pooled['censoredGames']} "
          f"identityFailures {pooled['scoreIdentityFailures']} "
          f"belowTargetDepth {config.get('decisionsBelowTargetDepth')} "
          f"workLimitEvents {config.get('workLimitEvents')}")


if __name__ == "__main__":
    pool(sys.argv[1], sys.argv[2:])
