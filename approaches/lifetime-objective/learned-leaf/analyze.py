"""Tables for the learned-leaf 2x2.

Whole games are the independent unit throughout (docs/benchmarks.md).  Paired
deltas and the difference-in-differences resample SEEDS, not arms, so every
resample keeps a game's four arm outcomes together.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

BOOTSTRAP_SEED = 0xA52A_1EAF
RESAMPLES = 20000


def load(path: str) -> dict:
    with open(path) as handle:
        blob = json.load(handle)
    games = {g["seedHex"]: g for g in blob["gamesDetail"]}
    blob["bySeed"] = games
    blob["order"] = [g["seedHex"] for g in blob["gamesDetail"]]
    return blob


def summary(blob: dict) -> dict:
    games = blob["gamesDetail"]
    moves = sum(g["moves"] for g in games)
    return {
        "games": len(games),
        "scoreMean": blob["score"]["mean"],
        "scoreMedian": blob["score"]["median"],
        "scoreQ25": blob["score"]["q25"],
        "scoreMin": blob["score"]["min"],
        "scoreMax": blob["score"]["max"],
        "scoreSd": blob["score"]["sd"],
        "movesMean": blob["moves"]["mean"],
        "movesMedian": blob["moves"]["median"],
        "clearsPerMove": blob["numberedClearsPerMove"],
        "revealsPerMove": blob["coverRevealsPerMove"],
        "meanOccupiedCells": blob["meanOccupiedCells"],
        "pointsPerMove": blob["pointsPerMove"],
        "workPerMove": blob["workPerMove"],
        "censored": blob["censoredGames"],
        "identityFailures": blob["scoreIdentityFailures"],
        "wallSeconds": blob["wallSeconds"],
        "threads": blob["threads"],
        "config": blob["config"],
        "totalMoves": moves,
    }


def aligned(a: dict, b: dict, field: str):
    seeds = a["order"]
    if seeds != b["order"]:
        raise SystemExit("cohorts are not the same ordered seeds")
    return (np.array([a["bySeed"][s][field] for s in seeds], dtype=float),
            np.array([b["bySeed"][s][field] for s in seeds], dtype=float))


def lower_bound(values: np.ndarray, alpha: float = 0.05) -> float:
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    n = len(values)
    picks = rng.integers(0, n, size=(RESAMPLES, n))
    return float(np.quantile(values[picks].mean(axis=1), alpha))


def paired(a: dict, b: dict, field: str = "score") -> dict:
    """b minus a."""
    x, y = aligned(a, b, field)
    d = y - x
    wins = int((d > 0).sum())
    losses = int((d < 0).sum())
    return {"delta": float(d.mean()),
            "lower95": lower_bound(d),
            "median": float(np.median(d)),
            "wins": wins, "ties": len(d) - wins - losses, "losses": losses}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", action="append", default=[],
                        help="name=path, repeatable")
    parser.add_argument("--pair", action="append", default=[],
                        help="baseline:candidate, repeatable")
    parser.add_argument("--did", default="",
                        help="refA:candA:refB:candB -- (candB-refB) - (candA-refA)")
    parser.add_argument("--json", default="")
    args = parser.parse_args()

    arms = {}
    for entry in args.label:
        name, path = entry.split("=", 1)
        if not os.path.exists(path):
            print(f"missing {path}", file=sys.stderr)
            continue
        arms[name] = load(path)

    report = {"arms": {name: summary(blob) for name, blob in arms.items()},
              "pairs": {}, "bootstrapResamples": RESAMPLES,
              "bootstrapSeed": hex(BOOTSTRAP_SEED)}

    print(f"{'arm':<22}{'score mean':>12}{'median':>10}{'moves':>9}"
          f"{'clr/mv':>9}{'rev/mv':>9}{'cells':>8}{'work/mv':>12}{'cens':>6}{'idfail':>7}")
    for name, blob in arms.items():
        s = report["arms"][name]
        print(f"{name:<22}{s['scoreMean']:>12,.0f}{s['scoreMedian']:>10,.0f}"
              f"{s['movesMean']:>9.2f}{s['clearsPerMove']:>9.4f}"
              f"{s['revealsPerMove']:>9.4f}{s['meanOccupiedCells']:>8.2f}"
              f"{s['workPerMove']:>12,.0f}{s['censored']:>6}{s['identityFailures']:>7}")

    for entry in args.pair:
        base, cand = entry.split(":", 1)
        if base not in arms or cand not in arms:
            continue
        block = {field: paired(arms[base], arms[cand], field)
                 for field in ("score", "moves", "clearsPerMove", "revealsPerMove")}
        report["pairs"][f"{cand}-{base}"] = block
        s = block["score"]
        m = block["moves"]
        print(f"\n{cand} - {base}: score {s['delta']:+,.0f} "
              f"(95% lower {s['lower95']:+,.0f})  moves {m['delta']:+.2f}  "
              f"W-T-L {s['wins']}-{s['ties']}-{s['losses']}")

    if args.did:
        ra, ca, rb, cb = args.did.split(":")
        if all(n in arms for n in (ra, ca, rb, cb)):
            xa, ya = aligned(arms[ra], arms[ca], "score")
            xb, yb = aligned(arms[rb], arms[cb], "score")
            did = (yb - xb) - (ya - xa)
            report["differenceInDifferences"] = {
                "definition": f"({cb}-{rb}) - ({ca}-{ra})",
                "delta": float(did.mean()),
                "lower95": lower_bound(did),
                "deltaA": float((ya - xa).mean()),
                "deltaB": float((yb - xb).mean()),
                "positiveGames": int((did > 0).sum()),
                "games": int(len(did)),
            }
            d = report["differenceInDifferences"]
            print(f"\nDiD {d['definition']}: {d['delta']:+,.0f} "
                  f"(95% lower {d['lower95']:+,.0f})   "
                  f"dA {d['deltaA']:+,.0f}  dB {d['deltaB']:+,.0f}")

    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=2)


if __name__ == "__main__":
    main()
