#!/usr/bin/env python3
"""Tabulate a directory of leaf-reweight cohort artifacts against `frozen`.

Every arm in the directory played the same ordered cohort, so the delta column
is a paired whole-game statistic and the bound is a one-sided 95% percentile
bootstrap over whole games.

Usage: sweeptable.py <directory> [--sort]
"""

import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "suite-validation")))
import stats  # noqa: E402
from compare import bootstrap_lower, load, quant  # noqa: E402


def main():
    directory = sys.argv[1]
    do_sort = "--sort" in sys.argv
    base_data, base_games = load(os.path.join(directory, "frozen.json"))
    order = [g["seedHex"] for g in base_data["gamesDetail"]]
    base_scores = [base_games[s]["score"] for s in order]

    rows = []
    for path in sorted(glob.glob(os.path.join(directory, "*.json"))):
        data, games = load(path)
        arm = os.path.basename(path)[:-5]
        if any(s not in games for s in order):
            print(f"# skipping incomplete {arm}", file=sys.stderr)
            continue
        scores = [games[s]["score"] for s in order]
        moves = [games[s]["moves"] for s in order]
        d = [a - b for a, b in zip(scores, base_scores)]
        cleared = sum(games[s]["numberedCleared"] for s in order)
        revealed = sum(games[s]["coversRevealed"] for s in order)
        mt = sum(moves)
        rows.append(dict(
            arm=arm, mean=stats.mean(scores), median=quant(scores, 0.5),
            q25=quant(scores, 0.25), lo=min(scores), hi=max(scores),
            sd=stats.stdev(scores), moves=stats.mean(moves),
            clears=cleared / mt, reveals=revealed / mt,
            occupied=stats.mean([games[s]["meanOccupiedCells"] for s in order]),
            delta=stats.mean(d),
            bound=bootstrap_lower(d, 0.05, 20000) if arm != "frozen" else 0.0,
            wtl=(sum(1 for x in d if x > 0), sum(1 for x in d if x == 0),
                 sum(1 for x in d if x < 0)),
            wall=data["wallSeconds"]))

    if do_sort:
        rows.sort(key=lambda r: -r["delta"])
    print(f"cohort {base_data['seedStartHex']}, {len(order)} paired games, "
          f"config {json.dumps(base_data['config'])[:120]}\n")
    print("| arm | mean | median | Q25 | min | max | sd | moves | clears/mv | "
          "reveals/mv | cells | delta | 95% lo | W-T-L |")
    print("| --- |" + " ---: |" * 13)
    for r in rows:
        w, t, l = r["wtl"]
        print(f"| {r['arm']} | {r['mean']:,.0f} | {r['median']:,.0f} | "
              f"{r['q25']:,.0f} | {r['lo']:,.0f} | {r['hi']:,.0f} | {r['sd']:,.0f} | "
              f"{r['moves']:.2f} | {r['clears']:.4f} | {r['reveals']:.4f} | "
              f"{r['occupied']:.2f} | {r['delta']:+,.0f} | {r['bound']:+,.0f} | "
              f"{w}-{t}-{l} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
