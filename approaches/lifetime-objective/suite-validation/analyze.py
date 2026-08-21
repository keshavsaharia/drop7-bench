#!/usr/bin/env python3
"""Check 1 and the discriminating-power analysis for the scenario suite.

Reads the JSONL that `build/suite-validation/posmode` streams, one row per
(position, tape, policy), and reports:

  * each policy's position-mode metrics, pooled over the completed positions;
  * the Spearman rank correlation between the suite metric and the known
    whole-game means, over all nine policies (S9) and over the six fair arms
    that share one 64-game cohort (S6);
  * discriminating power: between-policy variance against within-policy
    variance, the paired t of the named d3s7-vs-d4s7 comparison under common
    random numbers, and the logical work each side costs; and
  * the same numbers restricted to the development and sealed halves of the
    split manifest, which is the first use of Check 2.

Usage:
  analyze.py <posmode.jsonl> [--split <manifest.json>] [--metric points]
"""

import json
import math
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stats  # noqa: E402

# Whole-game means, taken from the record and from the coordinator's brief.
# The first six share the 64-game cohort 0xa51d1000-0xa51d103f (finding-05 plus
# the coordinator-supplied d2s5 figure).  The last three are from a different
# 64-seed cohort (finding-01) and every table that mixes them says so.
WHOLE_GAME = {
    "d2s5": 249641,
    "d2s7": 265294,
    "d3s5": 305051,
    "d3s7": 312327,
    "d4s5": 297327,
    "d4s7": 398498,
    "center-first": 57233,
    "random-legal": 80778,
    "lowest-column": 100050,
}
SHARED_COHORT = ["d2s5", "d2s7", "d3s5", "d3s7", "d4s5", "d4s7"]

# Logical work per move and mean moves for the six fair arms on that cohort
# (finding-05).  Used to price 64 whole games in the same units the suite is
# priced in.
WHOLE_GAME_COST = {
    "d2s7": (4139, 79.48),
    "d3s5": (54429, 89.84),
    "d3s7": (156834, 92.27),
    "d4s5": (1296034, 87.16),
    "d4s7": (4956614, 114.66),
}


def load(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    return rows


def complete_positions(rows, policies):
    """Positions for which every policy and every tape finished.

    A run stopped by the resource budget leaves a partial tail; including it
    would compare policies on different position sets and break the common
    random numbers.
    """
    seen = {}
    for row in rows:
        seen.setdefault(row["position"], set()).add((row["policy"], row["tape"]))
    tapes = sorted({row["tape"] for row in rows})
    needed = {(policy, tape) for policy in policies for tape in tapes}
    return {position for position, got in seen.items() if needed <= got}, tapes


def cell_table(rows, policies, positions, metric):
    """metric[policy][position][tape]."""
    table = {policy: {} for policy in policies}
    for row in rows:
        if row["position"] not in positions or row["policy"] not in policies:
            continue
        table[row["policy"]].setdefault(row["position"], {})[row["tape"]] = row
    out = {}
    for policy in policies:
        out[policy] = {
            position: {tape: metric(row) for tape, row in taped.items()}
            for position, taped in table[policy].items()
        }
    return out


def per_position_mean(cells, positions):
    return {position: stats.mean(list(cells[position].values()))
            for position in positions}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    split_path = None
    if "--split" in sys.argv:
        split_path = sys.argv[sys.argv.index("--split") + 1]
    rows = load(path)
    policies = [p for p in WHOLE_GAME if any(r["policy"] == p for r in rows)]
    positions, tapes = complete_positions(rows, policies)
    positions = sorted(positions)
    print(f"# rows {len(rows)}, complete positions {len(positions)}, "
          f"tapes {tapes}, policies {policies}")
    if not positions:
        return 1

    metrics = {
        "points": lambda r: float(r["points"]),
        "clearsPerMove": lambda r: (r["clears"] / r["moves"]) if r["moves"] else 0.0,
        "clears": lambda r: float(r["clears"]),
        "moves": lambda r: float(r["moves"]),
        "survived": lambda r: 0.0 if r["died"] else 1.0,
        "revealsPerMove": lambda r: (r["reveals"] / r["moves"]) if r["moves"] else 0.0,
        # POST HOC, added after the preregistered metrics were read.  Not part
        # of the verdict.  Board occupancy drift per move is the quantity
        # finding-01's conservation law is about and finding-06 §2 shows is the
        # cleanest single diagnostic of whether a policy is losing; a policy
        # that ends the horizon with a fuller board is losing.  Negated so that
        # larger is better, like every other metric in the table.
        "negOccupancyDrift": lambda r: (
            -(r["occupiedEnd"] - r["occupiedStart"]) / r["moves"]
            if r["moves"] else 0.0),
    }

    print("\n## Position-mode results "
          f"({len(positions)} positions x {len(tapes)} tapes, "
          "common random numbers)\n")
    header = ("| policy | whole-game mean | points | clears/move | moves | "
              "survival | reveals/move | -occupancy drift/move | work/cell |")
    print(header)
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    summary = {}
    work_table = cell_table(rows, policies, positions, lambda r: float(r["work"]))
    for policy in policies:
        line = {}
        for name, metric in metrics.items():
            cells = cell_table(rows, [policy], positions, metric)[policy]
            line[name] = stats.mean([v for position in positions
                                     for v in cells[position].values()])
        work = stats.mean([v for position in positions
                           for v in work_table[policy][position].values()])
        line["work"] = work
        summary[policy] = line
        print(f"| `{policy}` | {WHOLE_GAME[policy]:,} | {line['points']:,.0f} | "
              f"{line['clearsPerMove']:.4f} | {line['moves']:.3f} | "
              f"{line['survived']:.4f} | {line['revealsPerMove']:.4f} | "
              f"{line['negOccupancyDrift']:+.4f} | {work:,.0f} |")

    print("\n## Check 1 — Spearman rank correlation with whole-game means\n")
    print("| metric | S9 (all nine, cross-cohort) | p | "
          "S6 (six fair arms, shared cohort) | p |")
    print("| --- | ---: | ---: | ---: | ---: |")
    verdict_metric = None
    for name in ("points", "clearsPerMove", "moves", "survived",
                 "revealsPerMove", "negOccupancyDrift"):
        nine = [p for p in policies]
        six = [p for p in policies if p in SHARED_COHORT]
        r9, p9 = stats.spearman_permutation_p(
            [summary[p][name] for p in nine], [WHOLE_GAME[p] for p in nine])
        if len(six) >= 3:
            r6, p6 = stats.spearman_permutation_p(
                [summary[p][name] for p in six], [WHOLE_GAME[p] for p in six])
        else:
            r6, p6 = float("nan"), float("nan")
        print(f"| {name} | {r9:+.4f} | {p9:.4f} | {r6:+.4f} | {p6:.4f} |")
        if name == "points":
            verdict_metric = (r9, r6)

    if verdict_metric:
        r9, r6 = verdict_metric
        if r9 < 0.70:
            verdict = "NOT A STRENGTH INSTRUMENT (S9 < 0.70)"
        elif r6 >= 0.60:
            verdict = "USABLE AS A STRENGTH PROXY"
        else:
            verdict = "USABLE ONLY AS A COARSE SCREEN"
        print(f"\n**Preregistered verdict on the primary metric (points): "
              f"{verdict}** (S9 = {r9:+.4f}, S6 = {r6:+.4f})")

    # ---------------------------------------------------------------- power
    # ------------------------------------------------- why points behaves so
    # The score identity (audit-02 section 4.1, verified in finding-06 over
    # 19,610 moves) is
    #     delta = 17,000 * rises + 70,000 * board clears + sum of wave points,
    # so a horizon-H scenario score decomposes exactly.  If the rise term is
    # almost constant across policies, the metric cannot see survival.
    print("\n## Why the points metric behaves as it does — the exact score "
          "decomposition\n")
    print("| policy | mean points | rises x 17,000 | board clears x 70,000 | "
          "chain wave points | rise share |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    for policy in policies:
        sub = [r for r in rows if r["policy"] == policy
               and r["position"] in set(positions)]
        pts = stats.mean([r["points"] for r in sub])
        rise = stats.mean([17000.0 * r["rises"] for r in sub])
        clear = stats.mean([70000.0 * (1 if r["clearedBoard"] else 0) for r in sub])
        chain = pts - rise - clear
        print(f"| `{policy}` | {pts:,.0f} | {rise:,.0f} | {clear:,.0f} | "
              f"{chain:,.0f} | {100.0 * rise / pts if pts else 0:.2f}% |")
    fair = [p for p in policies if p in SHARED_COHORT]
    if fair:
        rise_means = []
        chain_means = []
        for policy in fair:
            sub = [r for r in rows if r["policy"] == policy
                   and r["position"] in set(positions)]
            rise_means.append(stats.mean([17000.0 * r["rises"] for r in sub]))
            chain_means.append(stats.mean(
                [r["points"] - 17000.0 * r["rises"]
                 - 70000.0 * (1 if r["clearedBoard"] else 0) for r in sub]))
        print(f"\nAcross the six fair arms, the rise term spans "
              f"{min(rise_means):,.0f}-{max(rise_means):,.0f} "
              f"(range {max(rise_means) - min(rise_means):,.0f}) and the chain "
              f"term spans {min(chain_means):,.0f}-{max(chain_means):,.0f} "
              f"(range {max(chain_means) - min(chain_means):,.0f}).")

    print("\n## Discriminating power\n")
    half_a = [t for i, t in enumerate(tapes) if i % 2 == 0]
    half_b = [t for i, t in enumerate(tapes) if i % 2 == 1]
    for name in ("points", "clearsPerMove", "negOccupancyDrift"):
        cells = cell_table(rows, policies, positions, metrics[name])
        means = [stats.mean([v for position in positions
                             for v in cells[policy][position].values()])
                 for policy in policies]
        between = stats.variance(means)
        withins = []
        for policy in policies:
            a = stats.mean([cells[policy][position][t]
                            for position in positions for t in half_a])
            b = stats.mean([cells[policy][position][t]
                            for position in positions for t in half_b])
            # Variance of a single half-sized evaluation, then the variance of a
            # full-K evaluation is half that.
            withins.append(((a - b) ** 2) / 2.0 / 2.0)
        within = stats.mean(withins)
        print(f"- **{name}**: between-policy variance {between:,.6g}, "
              f"within-policy (re-evaluation) variance {within:,.6g}, "
              f"ratio {between / within if within else float('inf'):,.1f}")

    print("\n### The named pair: d3s7 vs d4s7 under common random numbers\n")
    print("| metric | paired mean delta | SE | t | positions | "
          "suite logical work | 64-game logical work | work ratio |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    if "d3s7" in policies and "d4s7" in policies:
        for name in ("points", "clearsPerMove", "moves", "survived",
                     "negOccupancyDrift"):
            cells = cell_table(rows, ["d3s7", "d4s7"], positions, metrics[name])
            a = per_position_mean(cells["d4s7"], positions)
            b = per_position_mean(cells["d3s7"], positions)
            deltas = [a[position] - b[position] for position in positions]
            m, se, t, n = stats.paired_t(deltas)
            suite_work = sum(
                sum(work_table[policy][position].values())
                for policy in ("d3s7", "d4s7") for position in positions)
            game_work = sum(WHOLE_GAME_COST[policy][0] * WHOLE_GAME_COST[policy][1] * 64
                            for policy in ("d3s7", "d4s7"))
            print(f"| {name} | {m:+,.4f} | {se:,.4f} | {t:+.3f} | {n} | "
                  f"{suite_work:,.0f} | {game_work:,.0f} | "
                  f"{suite_work / game_work:.4f} |")

    print("\n### All pairwise paired t on the primary metric (points)\n")
    cells = cell_table(rows, policies, positions, metrics["points"])
    per_position = {policy: per_position_mean(cells[policy], positions)
                    for policy in policies}
    ordered = sorted(policies, key=lambda p: -WHOLE_GAME[p])
    print("| stronger (whole game) | weaker | delta | SE | t |")
    print("| --- | --- | ---: | ---: | ---: |")
    for i, strong in enumerate(ordered):
        for weak in ordered[i + 1:]:
            deltas = [per_position[strong][p] - per_position[weak][p]
                      for p in positions]
            m, se, t, _ = stats.paired_t(deltas)
            print(f"| `{strong}` | `{weak}` | {m:+,.0f} | {se:,.0f} | {t:+.2f} |")

    # ------------------------------------------------------- origin subsets
    # finding-02 limitation 6: the harvested positions come from LOWEST-COLUMN
    # play, a weak policy that runs a much fuller board than any fair arm does.
    # If off-distribution positions are part of why the suite mis-ranks, the
    # correlation should differ between the two origins.
    origins = sorted({row["origin"] for row in rows})
    if len(origins) > 1:
        print("\n## By position origin\n")
        print("| origin | positions | " +
              " | ".join(f"`{p}`" for p in policies) + " | S9 | S6 |")
        print("| --- | ---: | " + " | ".join("---:" for _ in policies) +
              " | ---: | ---: |")
        origin_of = {row["position"]: row["origin"] for row in rows}
        for origin in origins:
            subset = [p for p in positions if origin_of.get(p) == origin]
            if len(subset) < 4:
                continue
            values = []
            for policy in policies:
                cells = cell_table(rows, [policy], subset, metrics["points"])[policy]
                values.append(stats.mean([v for position in subset
                                          for v in cells[position].values()]))
            six_index = [i for i, p in enumerate(policies) if p in SHARED_COHORT]
            r9 = stats.spearman(values, [WHOLE_GAME[p] for p in policies])
            r6 = stats.spearman([values[i] for i in six_index],
                                [WHOLE_GAME[policies[i]] for i in six_index])
            print(f"| {origin} | {len(subset)} | " +
                  " | ".join(f"{v:,.0f}" for v in values) +
                  f" | {r9:+.4f} | {r6:+.4f} |")

    # ---------------------------------------------------------------- split
    if split_path:
        with open(split_path) as handle:
            manifest = json.load(handle)
        halves = {member["id"]: member["half"] for member in manifest["members"]}
        print("\n## Check 2 in use — the same analysis on each half\n")
        print("| half | positions | " +
              " | ".join(f"`{p}`" for p in policies) + " | S9 | S6 |")
        print("| --- | ---: | " + " | ".join("---:" for _ in policies) +
              " | ---: | ---: |")
        for half in ("development", "sealed"):
            subset = [p for p in positions if halves.get(p) == half]
            if not subset:
                continue
            values = []
            for policy in policies:
                cells = cell_table(rows, [policy], subset, metrics["points"])[policy]
                values.append(stats.mean([v for position in subset
                                          for v in cells[position].values()]))
            six_index = [i for i, p in enumerate(policies) if p in SHARED_COHORT]
            r9 = stats.spearman(values, [WHOLE_GAME[p] for p in policies])
            r6 = stats.spearman([values[i] for i in six_index],
                                [WHOLE_GAME[policies[i]] for i in six_index])
            print(f"| {half} | {len(subset)} | " +
                  " | ".join(f"{v:,.0f}" for v in values) +
                  f" | {r9:+.4f} | {r6:+.4f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
