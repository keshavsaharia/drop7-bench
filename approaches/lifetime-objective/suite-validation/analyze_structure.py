#!/usr/bin/env python3
"""What distinguishes a board where three numbered clears per move are available
from one where only two are?

Reads the CSV that `build/suite-validation/structure` writes and reports, on a
whole-position held-out split:

  1. the univariate association of every candidate structural property with the
     exactly-achievable clear rate;
  2. the incremental R^2 of each property over an occupancy-only model, which is
     the question the brief actually asks — `finding-06` and `finding-07` show
     that occupancy does not explain the achievable rate;
  3. the incremental R^2 of each property over the **frozen leaf's own 19-feature
     span**, which is how "is this property already in the leaf?" is answered
     without arguing about weights; and
  4. the same ranking against the achievable-minus-achieved gap, which is the
     quantity a better leaf would have to close.

Usage:
  analyze_structure.py <structure.csv> [--target achievableClears]
"""

import csv
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stats  # noqa: E402

LABELS = ["clearGap", "achievableClears", "achievablePoints", "achievableReveals",
          "d2Clears", "d3Clears", "d2Deaths", "d3Deaths", "completionsSolved"]
IDENT = ["id", "origin"]

LEAF_PREFIX = "leaf_"
OCCUPANCY_ONLY = ["occupied"]

# How each candidate property stands relative to the frozen leaf.  "present"
# means the leaf computes an equivalent quantity AND gives it a non-zero weight;
# "computed, weight 0" means the leaf computes it and multiplies it by zero, so
# the frozen search is blind to it; "related" means the leaf carries something
# in the same family but not the same quantity; "absent" means nothing in the
# leaf's 19 features expresses it.  Weights are
# approaches/fair-expectimax/reference/fair-only-horizon.cpp:51-71 and the
# semantics are enumerated in docs/exploratory/audit-02-fair-d4.md section 3.
LEAF_STATUS = {
    "occupied": "related (`height_load`, `numbered_cells`, `solid_cells`)",
    "covered": "present (`solid_cells` + `cracked_cells`)",
    "solid": "present (`solid_cells`, -620)",
    "cracked": "present (`cracked_cells`, -220)",
    "numbered": "present (`numbered_cells`, -18)",
    "movesRemaining": "present (divisor of `rise_pressure`)",
    "nextDisc": "related (`next_disc_vertical_options` only)",
    "openColumns": "present (`open_columns`, +180)",
    "maxHeight": "present (`danger_height_squared`, -1250)",
    "meanHeight": "related (`height_load`, `rise_pressure`)",
    "roughness": "**computed, weight 0** (`kRoughnessWeight = 0.0`)",
    "heightVariance": "absent",
    "heightRange": "absent",
    "lowDiscs": "related (`high_low_numbers`, `low_number_height_risk`)",
    "highDiscs": "absent",
    "meanValue": "absent",
    "overLengthLow": "related (`dead_low_numbers`, -120, value <= 2 only)",
    "overLengthHigh": "**absent** (the leaf's clog term stops at value 2)",
    "overLengthDiscs": "partly (`dead_low_numbers` covers value <= 2 only)",
    "oneAwayPairs": "related (`direct_potential` is a cost-weighted version)",
    "twoAwayPairs": "related (`direct_potential` is a cost-weighted version)",
    "coverFrontier": "related (`solid_exposure`, `cracked_exposure`)",
    "coverTwoSided": "related (`solid_exposure` needs two hits)",
    "coversInWave": "absent",
    "crackedInWave": "absent",
    "coversInWaveShare": "absent",
    "coverBuried": "absent",
    "coverNeighbourMean": "absent",
    "emptyComponents": "absent",
    "largestEmptyComponent": "absent",
    "emptyFragmentation": "absent",
    "expBestFirstWave": "**absent**",
    "expTriggerColumns": "**absent**",
    "probAnyTrigger": "**absent**",
    "triggerPairs": "**absent**",
    "triggerValues": "**absent**",
    "bestFirstWaveNow": "**absent**",
    "triggerColumnsNow": "related (`next_disc_vertical_options`, vertical only)",
    "landingNextToNumber": "absent",
    "landingOnCover": "absent",
    "runsHorizontal": "absent",
    "runsVertical": "absent",
    "meanRunHorizontal": "absent",
    "meanRunVertical": "related (mean column height)",
    "maxRun": "absent",
    "runsOfOne": "absent",
    "runsOfTwo": "absent",
    "runsThreePlus": "absent",
    "underLengthDiscs": "absent",
    "growthNeeded": "absent",
    "minDistanceSum": "related (`direct_potential` reweights the same distance)",
    "minDistancePerDisc": "related (`direct_potential`, per-disc)",
    "valueRoomMismatch": "absent",
}
OCCUPANCY_PLUS = ["occupied", "covered", "numbered", "movesRemaining"]


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for byte in data:
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def load(path):
    with open(path) as handle:
        rows = list(csv.DictReader(handle))
    features = [name for name in rows[0]
                if name not in LABELS and name not in IDENT
                and not name.startswith(LEAF_PREFIX)]
    # `leaf_value` is the weighted sum of the other 19 and spans no new
    # direction, so it is held out of the span and reported on its own: the
    # scalar is what the frozen search actually sees at a leaf, the 19-feature
    # span is what a reweighting of the existing features could recover.
    leaf = [name for name in rows[0]
            if name.startswith(LEAF_PREFIX) and name != "leaf_value"]
    return rows, features, leaf


def column(rows, name):
    return [float(row[name]) for row in rows]


def split(rows):
    """Whole-position three-way split by content hash of the position id.

    60% train, 20% validation, 20% held-out.  Coefficients are fitted on train
    only; the greedy block in the last table is *selected* on validation and
    *reported* on held-out, so no number in the held-out column was used to
    choose what goes in the model.  The split is a pure function of the position
    id, which is itself a content hash of the whole scenario record.
    """
    train, validation, test = [], [], []
    for row in rows:
        bucket = fnv1a64(("structure-holdout-v1:" + row["id"]).encode()) % 1000
        if bucket < 600:
            train.append(row)
        elif bucket < 800:
            validation.append(row)
        else:
            test.append(row)
    return train, validation, test


def model_r2(train, test, names, target):
    if not names:
        my = stats.mean(column(train, target))
        ys = column(test, target)
        ss_tot = sum((y - stats.mean(ys)) ** 2 for y in ys)
        ss_res = sum((y - my) ** 2 for y in ys)
        return 1.0 - ss_res / ss_tot if ss_tot else float("nan")
    columns = [column(train, name) for name in names]
    means = [stats.mean(c) for c in columns]
    sds = [stats.stdev(c) or 1.0 for c in columns]
    rows_train = [[(float(row[name]) - means[i]) / sds[i]
                   for i, name in enumerate(names)] for row in train]
    rows_test = [[(float(row[name]) - means[i]) / sds[i]
                  for i, name in enumerate(names)] for row in test]
    beta = stats.ols(rows_train, column(train, target))
    if beta is None:
        return float("nan")
    return stats.r_squared(beta, rows_test, column(test, target))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    target = "achievableClears"
    if "--target" in sys.argv:
        target = sys.argv[sys.argv.index("--target") + 1]
    rows, features, leaf = load(path)
    rows = [r for r in rows if int(r["completionsSolved"]) > 0]
    train, validation, test = split(rows)
    print(f"# positions {len(rows)} (train {len(train)}, validation "
          f"{len(validation)}, held-out {len(test)}), "
          f"candidate features {len(features)}, leaf features {len(leaf)} "
          f"+ the leaf scalar")
    origins = sorted({r["origin"] for r in rows})
    print("# by origin: " + ", ".join(
        f"{o} {sum(1 for r in rows if r['origin'] == o)}" for o in origins))

    ys = column(rows, target)
    ys_sorted = sorted(ys)
    print(f"# target `{target}`: mean {stats.mean(ys):.4f}, sd {stats.stdev(ys):.4f}, "
          f"min {ys_sorted[0]:.4f}, q25 {stats.quantile(ys_sorted, .25):.4f}, "
          f"median {stats.quantile(ys_sorted, .5):.4f}, "
          f"q75 {stats.quantile(ys_sorted, .75):.4f}, max {ys_sorted[-1]:.4f}")

    # -------- occupancy-conditional table, the finding-06 §3 instrument -----
    print(f"\n## `{target}` conditioned on occupancy (the finding-06 table, "
          "on achievable rather than achieved)\n")
    print("| occupied cells | n | achievable | d2s5 achieved | d3s5 achieved | "
          "spread within band (sd) |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    bands = [(0, 9), (10, 14), (15, 19), (20, 24), (25, 29), (30, 34),
             (35, 39), (40, 49)]
    for low, high in bands:
        members = [r for r in rows if low <= float(r["occupied"]) <= high]
        if len(members) < 3:
            continue
        vals = column(members, target)
        print(f"| {low}-{high} | {len(members)} | {stats.mean(vals):.3f} | "
              f"{stats.mean(column(members, 'd2Clears')):.3f} | "
              f"{stats.mean(column(members, 'd3Clears')):.3f} | "
              f"{stats.stdev(vals):.3f} |")

    # -------- baselines ----------------------------------------------------
    base_none = model_r2(train, test, [], target)
    base_occ = model_r2(train, test, OCCUPANCY_ONLY, target)
    base_occ_plus = model_r2(train, test, OCCUPANCY_PLUS, target)
    base_leaf = model_r2(train, test, leaf, target)
    base_leaf_value = model_r2(train, test, ["leaf_value"], target)
    base_leaf_occ = model_r2(train, test, leaf + OCCUPANCY_PLUS, target)
    base_all = model_r2(train, test, features, target)
    print("\n## Held-out R^2 of whole feature sets\n")
    print("| model | held-out R^2 |")
    print("| --- | ---: |")
    print(f"| intercept only | {base_none:.4f} |")
    print(f"| occupancy only | {base_occ:.4f} |")
    print(f"| occupancy + covered + numbered + movesRemaining | {base_occ_plus:.4f} |")
    print(f"| **the frozen leaf's scalar value, `fairLeaf(state)`** | "
          f"**{base_leaf_value:.4f}** |")
    print(f"| **the frozen leaf's 19 features, freely reweighted** | "
          f"**{base_leaf:.4f}** |")
    print(f"| leaf 19 + occupancy block | {base_leaf_occ:.4f} |")
    print(f"| all {len(features)} candidate structural features | {base_all:.4f} |")

    # -------- per-feature ranking -----------------------------------------
    print("\n## Candidate properties, ranked by what they add to the frozen leaf\n")
    print("| property | Spearman with target | univariate R^2 | "
          "+R^2 over occupancy | +R^2 over `fairLeaf` value | "
          "**+R^2 over the leaf's 19** | status in the frozen leaf |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | --- |")
    scored = []
    for name in features:
        rho = stats.spearman(column(rows, name), ys)
        uni = model_r2(train, test, [name], target)
        over_occ = model_r2(train, test, OCCUPANCY_ONLY + [name], target) - base_occ
        over_val = model_r2(train, test, ["leaf_value", name], target) - base_leaf_value
        over_leaf = model_r2(train, test, leaf + [name], target) - base_leaf
        scored.append((over_leaf, over_val, over_occ, uni, rho, name))
    scored.sort(reverse=True)
    for over_leaf, over_val, over_occ, uni, rho, name in scored:
        print(f"| `{name}` | {rho:+.4f} | {uni:.4f} | {over_occ:+.4f} | "
              f"{over_val:+.4f} | {over_leaf:+.4f} | {LEAF_STATUS.get(name, 'absent')} |")

    # -------- greedy forward selection on top of the leaf ------------------
    print("\n## Greedy forward selection on top of the frozen leaf's 19 features\n")
    print("Selected on the validation split, reported on the held-out split; no "
          "held-out number chose a term.\n")
    chosen = []
    current = base_leaf
    print("| step | added property | held-out R^2 | gain |")
    print("| --- | --- | ---: | ---: |")
    current_val = model_r2(train, validation, leaf, target)
    for step in range(8):
        best = None
        for name in features:
            if name in chosen:
                continue
            score = model_r2(train, validation, leaf + chosen + [name], target)
            if best is None or score > best[0]:
                best = (score, name)
        if best is None or not (best[0] > current_val + 1e-5):
            break
        chosen.append(best[1])
        current_val = best[0]
        held = model_r2(train, test, leaf + chosen, target)
        print(f"| {step + 1} | `{best[1]}` | {held:.4f} | {held - current:+.4f} |")
        current = held

    # -------- robustness by origin ----------------------------------------
    print("\n## Robustness: held-out R^2 of the selected block, by origin\n")
    print("| origin | n | leaf only | leaf + selected | gain |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for origin in origins:
        sub_train = [r for r in train if r["origin"] == origin]
        sub_test = [r for r in test + validation if r["origin"] == origin]
        if len(sub_test) < 12 or len(sub_train) < 24:
            continue
        a = model_r2(sub_train, sub_test, leaf, target)
        b = model_r2(sub_train, sub_test, leaf + chosen, target)
        print(f"| {origin} | {len(sub_train) + len(sub_test)} | {a:.4f} | "
              f"{b:.4f} | {b - a:+.4f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
