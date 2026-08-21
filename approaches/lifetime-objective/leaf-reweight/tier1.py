#!/usr/bin/env python3
"""Tier-1 anchors, and the transfer check that risk 3 demands.

Two jobs:

1. For each Tier-1 term (`roughness`, `solid_exposure`, `cracked_exposure`,
   `numbered_cells`) report the raw leaf weight a same-scale fitted leaf would
   use, so that a one-constant arm has a defensible target value rather than an
   arbitrary one, and report what that single substitution does to the leaf's
   standard deviation over the corpus.  The scale used is the frozen vector's
   own per-sigma L2 norm, i.e. "if the leaf pointed in the fitted direction but
   kept its present overall size, this term would be worth this much".

2. Cross-origin transfer.  Addendum A A.6 warns that the added cover-geometry
   block earns its keep off the fair-play manifold.  The same question applies to
   the reweighting itself: fit on one origin, score on another.  If the fitted
   direction is partly an artifact of synthetic position generation it will not
   transfer, and the arms should be built from the fair-play fit instead.

Usage: tier1.py <structure.csv> [--target achievableClears]
"""

import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "suite-validation")))
import stats  # noqa: E402

sys.path.insert(0, HERE)
from refit import COLUMN, FROZEN, NAMES, _r2, bucket, fit, unit  # noqa: E402

TIER1 = ["roughness", "solid_exposure", "cracked_exposure", "numbered_cells"]


def pool_for(rows, origin):
    if origin == "fair":
        return [r for r in rows if r["origin"] == "fair-d2"]
    if origin == "played":
        return [r for r in rows if r["origin"] in ("fair-d2", "lowest-column")]
    if origin == "synthetic":
        return [r for r in rows if r["origin"] == "synthetic"]
    return rows


def main():
    path = sys.argv[1]
    target = "achievableClears"
    if "--target" in sys.argv:
        target = sys.argv[sys.argv.index("--target") + 1]
    with open(path) as handle:
        allrows = [r for r in csv.DictReader(handle)
                   if int(r["completionsSolved"]) > 0]

    origins = ["all", "fair", "played", "synthetic"]
    fits = {}
    for origin in origins:
        pool = pool_for(allrows, origin)
        train = [r for r in pool if bucket(r) < 600]
        beta, means, sds = fit(train, target)
        frozen_sigma = [w * sds[i] for i, (_, w) in enumerate(FROZEN)]
        fits[origin] = dict(beta=beta, means=means, sds=sds,
                            f_u=unit(frozen_sigma), b_u=unit(beta),
                            norm=sum(x * x for x in frozen_sigma) ** 0.5,
                            pool=pool, train=train)

    print("## Cross-origin transfer of the fitted direction\n")
    print("Held-out R2 of a 19-feature leaf fit on the row origin and scored on "
          "the column origin's held-out positions (positions with bucket >= 800, "
          "so no position both trains and scores).\n")
    header = "| fit on \\ score on | " + " | ".join(origins) + " |"
    print(header)
    print("| --- |" + " ---: |" * len(origins))
    for fo in origins:
        cells = []
        f = fits[fo]
        beta_full = [stats.mean([float(r[target]) for r in f["train"]])] + list(f["beta"])
        for so in origins:
            heldout = [r for r in pool_for(allrows, so) if bucket(r) >= 800]
            design = [[1.0] + [(float(r[c]) - f["means"][i]) / f["sds"][i]
                               for i, c in enumerate(COLUMN)] for r in heldout]
            ys = [float(r[target]) for r in heldout]
            cells.append(f"{_r2(beta_full, design, ys):+.3f}")
        print(f"| **{fo}** | " + " | ".join(cells) + " |")

    print("\nFrozen `fairLeaf(state)` scalar, one feature, same held-out sets:\n")
    cells = []
    for so in origins:
        pool = pool_for(allrows, so)
        train = [r for r in pool if bucket(r) < 600]
        heldout = [r for r in pool if bucket(r) >= 800]
        b = stats.ols([[float(r["leaf_value"])] for r in train],
                      [float(r[target]) for r in train])
        cells.append(f"{_r2(b, [[1.0, float(r['leaf_value'])] for r in heldout], [float(r[target]) for r in heldout]):+.3f}")
    print("| | " + " | ".join(origins) + " |")
    print("| --- |" + " ---: |" * len(origins))
    print("| **frozen scalar** | " + " | ".join(cells) + " |")

    print("\n## Cosine between fitted directions across origins\n")
    print("| | " + " | ".join(origins) + " | frozen |")
    print("| --- |" + " ---: |" * (len(origins) + 1))
    for a in origins:
        row = []
        for b in origins:
            row.append(f"{sum(x * y for x, y in zip(fits[a]['b_u'], fits[b]['b_u'])):+.3f}")
        row.append(f"{sum(x * y for x, y in zip(fits[a]['b_u'], fits[a]['f_u'])):+.3f}")
        print(f"| **{a}** | " + " | ".join(row) + " |")

    print("\n## Tier 1 anchors: the raw weight a same-scale fitted leaf would use\n")
    for origin in ("all", "fair"):
        f = fits[origin]
        pool = f["pool"]
        base = [float(r["leaf_value"]) for r in pool]
        base_sd = stats.stdev(base)
        print(f"\n### fitted on `{origin}` positions "
              f"(frozen leaf sd over this pool {base_sd:,.0f})\n")
        print("| term | frozen | fitted per sigma (unit) | implied raw weight | "
              "leaf sd after substituting this one term |")
        print("| --- | ---: | ---: | ---: | ---: |")
        for name in TIER1:
            i = NAMES.index(name)
            implied = f["b_u"][i] * f["norm"] / f["sds"][i]
            column = [float(r[COLUMN[i]]) for r in pool]
            shifted = [b + (implied - FROZEN[i][1]) * x for b, x in zip(base, column)]
            print(f"| `{name}` | {FROZEN[i][1]:+,.0f} | {f['b_u'][i]:+.4f} | "
                  f"{implied:+,.0f} | {stats.stdev(shifted):,.0f} |")
        # all four together
        shifted = list(base)
        for name in TIER1:
            i = NAMES.index(name)
            implied = f["b_u"][i] * f["norm"] / f["sds"][i]
            shifted = [b + (implied - FROZEN[i][1]) * float(r[COLUMN[i]])
                       for b, r in zip(shifted, pool)]
        print(f"| **all four** | | | | {stats.stdev(shifted):,.0f} |")

        print("\nFeature scale on this pool (why the implied weights are large "
              "for the near-inert terms):\n")
        print("| term | mean | sd | max |")
        print("| --- | ---: | ---: | ---: |")
        for name in TIER1 + ["direct_potential", "rise_pressure",
                             "covered_height_risk"]:
            i = NAMES.index(name)
            column = [float(r[COLUMN[i]]) for r in pool]
            print(f"| `{name}` | {stats.mean(column):,.3f} | "
                  f"{stats.stdev(column):,.3f} | {max(column):,.3f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
