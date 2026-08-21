"""Paired whole-game analysis with one-sided bootstrap lower bounds.

`docs/methodology.md` fixes the statistical unit: a complete game, never a move,
root or transition.  Both comparisons this work makes are paired at the seed -
the teacher and fair depth 4 play the same master tapes, and any gameplay arm
and its comparator play the same cohort - so the resample draws SEEDS, not arms.

Usage:

    analyze.py flow  a=teacher.jsonl b=fair-d4.jsonl
    analyze.py cohort a=candidate.json b=reference.json
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import json
import sys

import numpy as np

BOOTSTRAP = 20000
BOOTSTRAP_SEED = 0xA526_B007


def load_flow(path: str) -> dict:
    out = {}
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            out[int(row["seed"])] = row
    return out


def load_cohort(path: str) -> dict:
    with open(path) as handle:
        blob = json.load(handle)
    return {int(game["seed"], 16): game for game in blob["games"]}


def summarise(rows: list) -> dict:
    score = np.array([r["score"] for r in rows], dtype=np.float64)
    moves = np.array([r["moves"] for r in rows], dtype=np.float64)
    cleared = np.array([r.get("cleared", r.get("numberedClears", 0))
                        for r in rows], dtype=np.float64)
    revealed = np.array([r.get("revealed", r.get("coveredReveals", 0))
                         for r in rows], dtype=np.float64)
    censored = sum(1 for r in rows if r.get("censored"))
    total_moves = max(moves.sum(), 1.0)
    return {
        "games": len(rows),
        "scoreMean": float(score.mean()),
        "scoreMedian": float(np.median(score)),
        "scoreQ25": float(np.quantile(score, 0.25)),
        "scoreMin": float(score.min()),
        "scoreMax": float(score.max()),
        "scoreStd": float(score.std(ddof=1)) if len(score) > 1 else 0.0,
        "movesMean": float(moves.mean()),
        "movesMedian": float(np.median(moves)),
        "movesQ25": float(np.quantile(moves, 0.25)),
        "censored": censored,
        "clearsPerMovePooled": float(cleared.sum() / total_moves),
        "revealsPerMovePooled": float(revealed.sum() / total_moves),
    }


def paired(candidate: np.ndarray, reference: np.ndarray, label: str) -> dict:
    delta = candidate - reference
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    index = rng.integers(0, len(delta), size=(BOOTSTRAP, len(delta)))
    means = delta[index].mean(axis=1)
    return {
        f"{label}Delta": float(delta.mean()),
        f"{label}DeltaLower95": float(np.quantile(means, 0.05)),
        f"{label}DeltaUpper95": float(np.quantile(means, 0.95)),
        f"{label}Wins": int(np.count_nonzero(delta > 0)),
        f"{label}Ties": int(np.count_nonzero(delta == 0)),
        f"{label}Losses": int(np.count_nonzero(delta < 0)),
    }


def occupancy_bands(rows: list) -> dict:
    """Clears per move conditioned on how full the board was BEFORE the move.

    Disc conservation is exact, so the clears of move i can be reconstructed from
    the occupancy trace alone:

        cleared_i = occupied_before + 1 + 7 * rise_i - occupied_after

    and rises land on fixed move indices (every fifth move from the start),
    independently of which columns were chosen.  `finding-06` section 3 uses the
    same reconstruction and checks it against the engine's own clear total; the
    same check is applied here and reported as `reconstructionError`.
    """
    bands = {}
    total_reconstructed = 0
    total_reported = 0
    slopes = []
    for row in rows:
        occupancy = row.get("moveOccupancy")
        if not occupancy:
            continue
        before = 12  # the repository's opening: a solid covered bottom row plus 5
        before = occupancy[0] - 1 - (7 if 1 % 5 == 0 else 0)
        previous = before
        for index, after in enumerate(occupancy, start=1):
            rise = 1 if index % 5 == 0 else 0
            cleared = previous + 1 + 7 * rise - after
            total_reconstructed += cleared
            key = (previous // 5) * 5
            entry = bands.setdefault(key, [0, 0])
            entry[0] += cleared
            entry[1] += 1
            previous = after
        total_reported += row.get("cleared", row.get("numberedClears", 0))
        cycles = row.get("cycleOccupancy") or []
        if len(cycles) >= 4:
            y = np.array(cycles[1:], dtype=np.float64)
            x = np.arange(len(y), dtype=np.float64)
            slopes.append(float(np.polyfit(x, y, 1)[0]))
    return {
        "bands": {str(k): {"clearsPerMove": v[0] / max(v[1], 1), "moves": v[1]}
                  for k, v in sorted(bands.items())},
        "reconstructedClears": total_reconstructed,
        "reportedClears": total_reported,
        "reconstructionError": total_reconstructed - total_reported,
        "occupancySlopeMean": float(np.mean(slopes)) if slopes else None,
        "occupancySlopeGames": len(slopes),
    }


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mode = sys.argv[1]
    if mode == "bands":
        for argument in sys.argv[2:]:
            name, _, path = argument.partition("=")
            print(json.dumps({name: occupancy_bands(list(load_flow(path).values()))},
                             indent=2))
        return
    loader = load_flow if mode == "flow" else load_cohort
    arms = {}
    for argument in sys.argv[2:]:
        name, _, path = argument.partition("=")
        arms[name] = loader(path)
    names = list(arms)
    if len(names) != 2:
        raise SystemExit("need exactly two arms")
    a, b = names
    shared = sorted(set(arms[a]) & set(arms[b]))
    if not shared:
        raise SystemExit("the two arms share no seeds")

    rows_a = [arms[a][seed] for seed in shared]
    rows_b = [arms[b][seed] for seed in shared]
    out = {"pairedSeeds": len(shared), a: summarise(rows_a), b: summarise(rows_b)}

    for field, label in (("score", "score"), ("moves", "moves")):
        ca = np.array([r[field] for r in rows_a], dtype=np.float64)
        cb = np.array([r[field] for r in rows_b], dtype=np.float64)
        out.update(paired(ca, cb, label))

    def per_move(rows, key_a, key_b):
        return np.array([(r.get(key_a, r.get(key_b, 0)) / max(r["moves"], 1))
                         for r in rows], dtype=np.float64)

    out.update(paired(per_move(rows_a, "cleared", "numberedClears"),
                      per_move(rows_b, "cleared", "numberedClears"),
                      "clearsPerMove"))
    out.update(paired(per_move(rows_a, "revealed", "coveredReveals"),
                      per_move(rows_b, "revealed", "coveredReveals"),
                      "revealsPerMove"))
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
