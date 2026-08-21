#!/usr/bin/env python3
"""Is the frozen leaf missing features, or missing weights?

Fits the frozen leaf's own 19 features to the exactly-achievable clear rate on
the training split and compares the fitted direction with the frozen weight
vector from approaches/fair-expectimax/reference/fair-only-horizon.cpp:53-71.

Both vectors are put on the same footing by scaling each weight by the feature's
standard deviation over the corpus - that is the change in leaf units produced by
a one-sigma change in the feature, which is the only sense in which a
+180-per-open-column and a -1250-per-danger-unit weight are comparable.  Each
vector is then normalised to unit L2 norm, so what is compared is the DIRECTION
the leaf points in, not its arbitrary overall scale.

Usage: leaf_weights.py <structure.csv> [--target achievableClears]
"""

import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stats  # noqa: E402

# fair-only-horizon.cpp:53-71, in the order fairLeaf accumulates them.
FROZEN = [
    ("leaf_open_columns", 180.0),
    ("leaf_height_load", -20.0),
    ("leaf_solid_cells", -620.0),
    ("leaf_cracked_cells", -220.0),
    ("leaf_numbered_cells", -18.0),
    ("leaf_high_low_numbers", -90.0),
    ("leaf_direct_potential", 1600.0),
    ("leaf_latent_chain_potential", 700.0),
    ("leaf_cracked_exposure", 100.0),
    ("leaf_solid_exposure", 40.0),
    ("leaf_adjacent_ones", -550.0),
    ("leaf_triple_twos", -750.0),
    ("leaf_dead_low_numbers", -120.0),
    ("leaf_covered_height_risk", -95.0),
    ("leaf_low_number_height_risk", -85.0),
    ("leaf_danger_height_squared", -1250.0),
    ("leaf_roughness", 0.0),
    ("leaf_rise_pressure", -35.0),
    ("leaf_next_disc_vertical_options", 220.0),
]


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for byte in data:
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    path = sys.argv[1]
    target = "achievableClears"
    if "--target" in sys.argv:
        target = sys.argv[sys.argv.index("--target") + 1]
    with open(path) as handle:
        rows = [r for r in csv.DictReader(handle) if int(r["completionsSolved"]) > 0]
    train = [r for r in rows
             if fnv1a64(("structure-holdout-v1:" + r["id"]).encode()) % 1000 < 600]

    names = [n for n, _ in FROZEN]
    columns = [[float(r[n]) for r in train] for n in names]
    sds = [stats.stdev(c) or 1.0 for c in columns]
    means = [stats.mean(c) for c in columns]
    design = [[(float(r[n]) - means[i]) / sds[i] for i, n in enumerate(names)]
              for r in train]
    ys = [float(r[target]) for r in train]
    beta = stats.ols(design, ys)
    fitted = beta[1:]

    # the frozen vector, expressed as leaf units per one-sigma of each feature
    frozen_sigma = [w * sds[i] for i, (_, w) in enumerate(FROZEN)]

    def unit(v):
        norm = sum(x * x for x in v) ** 0.5 or 1.0
        return [x / norm for x in v]

    f_u, b_u = unit(frozen_sigma), unit(fitted)
    cosine = sum(a * b for a, b in zip(f_u, b_u))

    all_rows_target = [float(r[target]) for r in rows]
    print(f"# target `{target}`, {len(train)} training positions of {len(rows)}\n")
    print(f"**Cosine similarity between the frozen leaf's weight direction and the "
          f"direction that best predicts the achievable clear rate: "
          f"{cosine:+.4f}.**\n")
    print("| # | leaf feature | frozen weight | frozen, per sigma (normalised) | "
          "fitted, per sigma (normalised) | sign | univariate Spearman |")
    print("| --: | --- | ---: | ---: | ---: | :---: | ---: |")
    scored = []
    for i, (name, weight) in enumerate(FROZEN):
        rho = stats.spearman([float(r[name]) for r in rows], all_rows_target)
        agree = "ok" if (f_u[i] == 0 and b_u[i] == 0) or \
                        (f_u[i] * b_u[i] > 0) else (
            "**zero**" if weight == 0 else "**FLIP**")
        scored.append((abs(b_u[i]), i, name, weight, f_u[i], b_u[i], agree, rho))
    for _, i, name, weight, fu, bu, agree, rho in sorted(scored, reverse=True):
        short = name[len("leaf_"):]
        print(f"| {i + 1} | `{short}` | {weight:+,.0f} | {fu:+.4f} | {bu:+.4f} | "
              f"{agree} | {rho:+.4f} |")

    flips = [s for s in scored if s[6] == "**FLIP**"]
    zeros = [s for s in scored if s[6] == "**zero**"]
    print(f"\nSign disagreements: **{len(flips)} of 19** "
          f"({', '.join('`' + s[2][len('leaf_'):] + '`' for s in flips)}).")
    if zeros:
        print(f"Frozen weight exactly zero while the fit wants it: "
              f"{', '.join('`' + s[2][len('leaf_'):] + '`' for s in zeros)}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
