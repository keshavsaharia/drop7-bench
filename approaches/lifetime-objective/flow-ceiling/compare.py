#!/usr/bin/env python3
"""Side-by-side comparison of `flow-run` cohorts for finding-07.

Prints one row per cohort (flow rates, lifetime, occupancy) and one
occupancy-band table with a column per cohort.  Per-move clear counts are
reconstructed from the recorded occupancy trace by disc conservation, exactly as
in `analyze.py`, and every game is checked against its own recorded total.

Usage: compare.py <label>=<games.jsonl> ...
"""
import json
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from analyze import reconstruct_per_move, load, mean, median  # noqa: E402

BANDS = [(0, 9), (10, 14), (15, 19), (20, 24), (25, 29), (30, 34), (35, 39),
         (40, 44), (45, 49)]
SKIP = 25  # moves excluded as "opening" when reporting the steady-state rate


def cohort(path):
    games = load(path)
    has_reveals = "moveRevealed" in games[0]
    moves = sum(g["moves"] for g in games)
    out = {
        "n": len(games),
        "moves": moves,
        "meanMoves": mean([g["moves"] for g in games]),
        "medMoves": median([g["moves"] for g in games]),
        "censored": sum(1 for g in games if g["censored"]),
        "meanScore": mean([g["score"] for g in games]),
        "clears": sum(g["cleared"] for g in games) / moves,
        "reveals": sum(g["revealed"] for g in games) / moves,
        "slope": mean([slope(g["cycleOccupancy"], 1) for g in games]),
        "occ": mean([o for g in games for o in g["moveOccupancy"]]),
    }
    late_c = late_m = 0
    bands = {b: [0, 0] for b in BANDS}
    for g in games:
        rows = reconstruct_per_move(g)
        assert rows is not None, path
        for i, (before, cleared, _r) in enumerate(rows):
            if cleared is None:
                continue
            if i >= SKIP:
                late_c += cleared
                late_m += 1
            for b in BANDS:
                if b[0] <= before <= b[1]:
                    bands[b][0] += 1
                    bands[b][1] += cleared
                    break
    out["steady"] = late_c / late_m if late_m else 0.0
    out["bands"] = bands

    # Reveals per move by occupancy band, and covered cells available to be
    # revealed, when the run recorded them.  Reveals cannot be reconstructed
    # from occupancy the way clears can: opening a cover changes a cell's kind,
    # not the cell count.
    rev = {b: [0, 0, 0] for b in BANDS}  # [moves, reveals, covered-before]
    if has_reveals:
        for g in games:
            occ = g["moveOccupancy"]
            cov = g["moveCovered"]
            revealed = g["moveRevealed"]
            before_o, before_c = 7, 7
            for i in range(len(occ)):
                for b in BANDS:
                    if b[0] <= before_o <= b[1]:
                        rev[b][0] += 1
                        rev[b][1] += revealed[i]
                        rev[b][2] += before_c
                        break
                before_o, before_c = occ[i], cov[i]
    out["revealBands"] = rev
    out["hasReveals"] = has_reveals
    return out


def slope(values, skip):
    n = len(values) - skip
    if n < 3:
        return 0.0
    xs = list(range(n))
    ys = values[skip:]
    sx, sy = sum(xs), sum(ys)
    sxy = sum(x * y for x, y in zip(xs, ys))
    sxx = sum(x * x for x in xs)
    d = n * sxx - sx * sx
    return (n * sxy - sx * sy) / d if d else 0.0


def main():
    items = []
    for arg in sys.argv[1:]:
        label, path = arg.split("=", 1)
        items.append((label, cohort(path)))
    print(f"{'cohort':<26}{'n':>3}{'moves':>7}{'cens':>5}{'meanMv':>8}"
          f"{'meanScore':>11}{'clr/mv':>8}{'rev/mv':>8}{'steady':>8}"
          f"{'slope':>8}{'meanOcc':>9}")
    for label, c in items:
        print(f"{label:<26}{c['n']:>3}{c['moves']:>7}{c['censored']:>5}"
              f"{c['meanMoves']:>8.2f}{c['meanScore']:>11.0f}{c['clears']:>8.4f}"
              f"{c['reveals']:>8.4f}{c['steady']:>8.4f}{c['slope']:>+8.3f}"
              f"{c['occ']:>9.2f}")
    print("\nclears/move by occupied cells before the move  (n moves in italics)")
    header = f"{'band':<8}" + "".join(f"{lab:>22}" for lab, _ in items)
    print(header)
    for b in BANDS:
        row = f"{b[0]}-{b[1]:<4}"
        for _lab, c in items:
            count, total = c["bands"][b]
            row += (f"{total / count:>15.2f} (n={count:<4})" if count
                    else f"{'—':>22}")
        print(row)

    if any(c["hasReveals"] for _l, c in items):
        print("\nreveals/move by occupied cells before the move, and mean "
              "covered cells available")
        print(f"{'band':<8}" + "".join(f"{lab:>26}" for lab, c in items
                                       if c["hasReveals"]))
        for b in BANDS:
            row = f"{b[0]}-{b[1]:<4}"
            any_row = False
            for _lab, c in items:
                if not c["hasReveals"]:
                    continue
                moves, reveals, covered = c["revealBands"][b]
                if moves:
                    any_row = True
                    row += (f"{reveals / moves:>12.2f} /cov"
                            f"{covered / moves:>6.1f} (n={moves:<4})")
                else:
                    row += f"{'—':>26}"
            if any_row:
                print(row)


if __name__ == "__main__":
    main()
