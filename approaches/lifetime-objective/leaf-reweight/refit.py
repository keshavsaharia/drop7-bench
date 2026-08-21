#!/usr/bin/env python3
"""Turn Addendum A's fitted direction into an actual leaf weight vector.

finding-10 Addendum A reports the fitted direction in "leaf units per one sigma
of each feature, normalised to unit length".  That is a direction, not a leaf: a
search needs raw weights whose scale is commensurate with the -1,000,000
terminal utility and with the 1.0 coefficient on immediate score.  This script
does the rescaling explicitly and writes weight files the search can load.

Rescaling rule (stated so it can be argued with):

  * fit standardised OLS of the target on the leaf's own 19 features, exactly as
    approaches/lifetime-objective/suite-validation/leaf_weights.py does, on the
    same 60% training split by content hash;
  * take the fitted per-sigma vector, normalise it to unit L2 length -> u;
  * interpolate in that per-sigma space between the frozen unit direction f and
    the fitted unit direction u, and re-normalise:
        d(alpha) = unit((1 - alpha) * f + alpha * u)
  * convert back to raw weights, w_i = d_i * S / sigma_i, choosing S so that the
    resulting leaf has the SAME standard deviation over the corpus as the frozen
    leaf value (column `leaf_value`);
  * choose a bias so that the resulting leaf has the same MEAN over the corpus
    as the frozen leaf.

Scale and level are matched rather than free because a leaf's absolute scale is
not a free parameter inside this search: the ranking is effectively
lexicographic (audit-02 section 5), minimising modelled four-ply death against a
-1,000,000 penalty first and maximising the leaf second.  Shrinking or growing
the leaf changes how much modelled death risk the search will accept, which is a
different experiment from changing the direction the leaf points in.  Matching
sigma and mean holds that second axis fixed, so the arms below vary direction
only.

Usage:
  refit.py <structure.csv> [--target achievableClears] [--origin all|fair]
           [--out-dir DIR] [--alphas 0,0.25,0.5,1]
"""

import csv
import os
import sys

SUITE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "suite-validation")
sys.path.insert(0, os.path.abspath(SUITE))
import stats  # noqa: E402

# fair-only-horizon.cpp:53-71, in the order fairLeaf accumulates them.
FROZEN = [
    ("open_columns", 180.0),
    ("height_load", -20.0),
    ("solid_cells", -620.0),
    ("cracked_cells", -220.0),
    ("numbered_cells", -18.0),
    ("high_low_numbers", -90.0),
    ("direct_potential", 1600.0),
    ("latent_chain_potential", 700.0),
    ("cracked_exposure", 100.0),
    ("solid_exposure", 40.0),
    ("adjacent_ones", -550.0),
    ("triple_twos", -750.0),
    ("dead_low_numbers", -120.0),
    ("covered_height_risk", -95.0),
    ("low_number_height_risk", -85.0),
    ("danger_height_squared", -1250.0),
    ("roughness", 0.0),
    ("rise_pressure", -35.0),
    ("next_disc_vertical_options", 220.0),
]
NAMES = [n for n, _ in FROZEN]
COLUMN = ["leaf_" + n for n in NAMES]


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for byte in data:
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def bucket(row):
    return fnv1a64(("structure-holdout-v1:" + row["id"]).encode()) % 1000


def unit(vector):
    norm = sum(x * x for x in vector) ** 0.5 or 1.0
    return [x / norm for x in vector]


def fit(rows, target):
    """Standardised OLS on `rows`; returns (per-sigma beta, means, sds)."""
    columns = [[float(r[c]) for r in rows] for c in COLUMN]
    sds = [stats.stdev(c) or 1.0 for c in columns]
    means = [stats.mean(c) for c in columns]
    design = [[(float(r[c]) - means[i]) / sds[i] for i, c in enumerate(COLUMN)]
              for r in rows]
    ys = [float(r[target]) for r in rows]
    beta = stats.ols(design, ys)
    return beta[1:], means, sds


def _r2(beta, design, ys):
    mu = stats.mean(ys)
    ss_tot = sum((y - mu) ** 2 for y in ys)
    ss_res = 0.0
    for row, y in zip(design, ys):
        pred = sum(b * x for b, x in zip(beta, row))
        ss_res += (y - pred) ** 2
    return 1.0 - ss_res / ss_tot if ss_tot else 0.0


def main():
    path = sys.argv[1]
    target = "achievableClears"
    origin = "all"
    out_dir = os.path.dirname(os.path.abspath(__file__))
    alphas = [0.25, 0.5, 1.0]
    argv = sys.argv
    if "--target" in argv:
        target = argv[argv.index("--target") + 1]
    if "--origin" in argv:
        origin = argv[argv.index("--origin") + 1]
    if "--out-dir" in argv:
        out_dir = argv[argv.index("--out-dir") + 1]
    if "--alphas" in argv:
        alphas = [float(a) for a in argv[argv.index("--alphas") + 1].split(",")]

    with open(path) as handle:
        allrows = [r for r in csv.DictReader(handle)
                   if int(r["completionsSolved"]) > 0]

    if origin == "fair":
        pool = [r for r in allrows if r["origin"] == "fair-d2"]
    elif origin == "played":
        pool = [r for r in allrows if r["origin"] in ("fair-d2", "lowest-column")]
    else:
        pool = allrows

    train = [r for r in pool if bucket(r) < 600]
    validation = [r for r in pool if 600 <= bucket(r) < 800]
    heldout = [r for r in pool if bucket(r) >= 800]

    beta, means, sds = fit(train, target)
    beta_full = [stats.mean([float(r[target]) for r in train])] + list(beta)

    print(f"# refit: target={target} origin={origin} "
          f"train={len(train)} val={len(validation)} heldout={len(heldout)} "
          f"(pool {len(pool)} of {len(allrows)})")
    for label, subset in (("train", train), ("validation", validation),
                          ("held-out", heldout)):
        design = [[1.0] + [(float(r[c]) - means[i]) / sds[i]
                           for i, c in enumerate(COLUMN)] for r in subset]
        ys = [float(r[target]) for r in subset]
        print(f"#   R2 {label:>9}: {_r2(beta_full, design, ys):+.4f}")

    # The frozen leaf scalar, as a one-feature baseline, on the same splits.
    for label, subset in (("held-out", heldout),):
        xs = [[float(r["leaf_value"])] for r in subset]
        ys = [float(r[target]) for r in subset]
        xtr = [[float(r["leaf_value"])] for r in train]
        ytr = [float(r[target]) for r in train]
        b = stats.ols(xtr, ytr)
        print(f"#   R2 {label:>9} frozen fairLeaf scalar: "
              f"{_r2(b, [[1.0] + x for x in xs], ys):+.4f}")

    frozen_sigma = [w * sds[i] for i, (_, w) in enumerate(FROZEN)]
    f_u = unit(frozen_sigma)
    b_u = unit(beta)
    cosine = sum(a * b for a, b in zip(f_u, b_u))
    print(f"# cosine(frozen, fitted) = {cosine:+.4f}")

    # scale target: the frozen leaf's own mean and sd over the whole pool
    leaf_values = [float(r["leaf_value"]) for r in pool]
    target_mean, target_sd = stats.mean(leaf_values), stats.stdev(leaf_values)
    print(f"# frozen leaf over pool: mean {target_mean:,.1f} sd {target_sd:,.1f}")

    zrows = [[(float(r[c]) - means[i]) / sds[i] for i, c in enumerate(COLUMN)]
             for r in pool]

    for alpha in alphas:
        d = unit([(1.0 - alpha) * a + alpha * b for a, b in zip(f_u, b_u)])
        raw = [sum(di * z for di, z in zip(d, row)) for row in zrows]
        raw_sd = stats.stdev(raw) or 1.0
        scale = target_sd / raw_sd
        weights = [d[i] * scale / sds[i] for i in range(len(NAMES))]
        # bias so the mean level matches the frozen leaf
        produced = [sum(w * float(r[c]) for w, c in zip(weights, COLUMN))
                    for r in pool]
        bias = target_mean - stats.mean(produced)
        tag = f"{origin}-{target}-a{alpha:g}".replace(".", "p")
        out = os.path.join(out_dir, f"weights-refit-{tag}.txt")
        with open(out, "w") as handle:
            handle.write(f"# refit alpha={alpha} target={target} origin={origin}\n")
            handle.write(f"# cosine(frozen,fitted)={cosine:+.4f}\n")
            handle.write("# sigma- and mean-matched to the frozen leaf over "
                         f"{len(pool)} corpus positions\n")
            for name, w in zip(NAMES, weights):
                handle.write(f"{name} {w:.10g}\n")
            handle.write(f"bias {bias:.10g}\n")
        check = [p + bias for p in produced]
        print(f"\n## alpha={alpha:g} -> {os.path.basename(out)}  "
              f"(leaf mean {stats.mean(check):,.1f} sd {stats.stdev(check):,.1f})")
        print("| term | frozen | refit alpha=%g |" % alpha)
        print("| --- | ---: | ---: |")
        for i, name in enumerate(NAMES):
            print(f"| `{name}` | {FROZEN[i][1]:+,.4g} | {weights[i]:+,.4g} |")
        print(f"| `bias` | +0 | {bias:+,.4g} |")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
