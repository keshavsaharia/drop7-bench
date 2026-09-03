#!/usr/bin/env python3
"""One TreeStrap step for the linear fair leaf.

Fits the eighteen fair-leaf terms (plus intercept), and a variant with the
best immediate six-feature drop value as a nineteenth term, to the exact
depth-4 root value on non-death-dominated fit roots; reports held-out R^2
against the frozen-leaf affine baseline; writes LinearLeaf weight files.

  fit_leaf.py --panel panel.ndjson --terms root-terms.ndjson --cem-weights weights-cem.txt
              --train-games 32 --floor -50000 --out fit-leaf.json
              --weights18-out weights-leaf18.txt --weights19-out weights-leaf19.txt
"""
import argparse
import json
import math
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
from fit_q import LAMBDAS, load_kf_weights, ridge, ridge_from, gram_of, dot  # noqa: E402

LEAF_NAMES = ["open_columns", "height_load", "solid_cells", "cracked_cells", "numbered_cells",
              "high_low_numbers", "direct_potential", "latent_chain_potential", "cracked_exposure",
              "solid_exposure", "adjacent_ones", "triple_twos", "dead_low_numbers",
              "covered_height_risk", "low_number_height_risk", "danger_height_squared",
              "rise_pressure", "next_disc_vertical_options"]
FROZEN = [180.0, -20.0, -620.0, -220.0, -18.0, -90.0, 1600.0, 700.0, 100.0, 40.0, -550.0,
          -750.0, -120.0, -95.0, -85.0, -1250.0, -35.0, 220.0]
KF_NAMES = ["min_eq_elem_True", "row_dets", "col_dets", "max_eq_elem", "1_dets", "elem_det"]


def load_ndjson(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def r2(y, yhat):
    m = sum(y) / len(y)
    sst = sum((v - m) ** 2 for v in y)
    sse = sum((v - w) ** 2 for v, w in zip(y, yhat))
    return 1.0 - sse / sst if sst > 0 else None


def fit_with_cv(X, y, gids):
    n = len(X)
    p = len(X[0])
    mean = [sum(r[j] for r in X) / n for j in range(p)]
    sd = []
    for j in range(p):
        var = sum((r[j] - mean[j]) ** 2 for r in X) / n
        sd.append(math.sqrt(var) if var > 0 else 1.0)
    Xs = [[(r[j] - mean[j]) / sd[j] for j in range(p)] for r in X]
    ym = sum(y) / n
    yc = [v - ym for v in y]
    folds = {}
    for i, g in enumerate(gids):
        folds.setdefault(g % 4, []).append(i)
    cv = {}
    fold_grams = {}
    for fold, members in folds.items():
        held = set(members)
        fold_grams[fold] = gram_of([Xs[i] for i in range(n) if i not in held], [yc[i] for i in range(n) if i not in held])
    for lam in LAMBDAS:
        sse, cnt = 0.0, 0
        for fold, members in folds.items():
            g, xty, nf = fold_grams[fold]
            w = ridge_from(g, xty, nf, lam)
            for i in members:
                sse += (yc[i] - dot(w, Xs[i])) ** 2
                cnt += 1
        cv[str(lam)] = sse / cnt
    lam = min(LAMBDAS, key=lambda l: cv[str(l)])
    w_std = ridge(Xs, yc, lam)
    w = [w_std[j] / sd[j] for j in range(p)]
    bias = ym - sum(w[j] * mean[j] for j in range(p))
    return w, bias, lam, cv


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--panel", required=True)
    parser.add_argument("--terms", required=True)
    parser.add_argument("--cem-weights", required=True)
    parser.add_argument("--train-games", type=int, default=32)
    parser.add_argument("--floor", type=float, default=-50000.0)
    parser.add_argument("--out", required=True)
    parser.add_argument("--weights18-out", required=True)
    parser.add_argument("--weights19-out", required=True)
    args = parser.parse_args()

    panel = {(r["game"], r["move"]): r for r in load_ndjson(args.panel)}
    terms = load_ndjson(args.terms)
    cem = load_kf_weights(args.cem_weights)
    rows = []
    for t in terms:
        root = panel[(t["game"], t["move"])]
        v4 = max(root["v4"])
        rows.append({"game": t["game"], "v4": v4, "terms": t["terms"], "leaf": t["fairLeaf"], "kf": t["kfBest"],
                     "keep": v4 > args.floor})
    fit = [r for r in rows if r["game"] < args.train_games and r["keep"]]
    held = [r for r in rows if r["game"] >= args.train_games and r["keep"]]
    dropped_fit = sum(1 for r in rows if r["game"] < args.train_games and not r["keep"])
    dropped_held = sum(1 for r in rows if r["game"] >= args.train_games and not r["keep"])
    print("fit roots %d (dropped %d), held-out %d (dropped %d)" % (len(fit), dropped_fit, len(held), dropped_held), file=sys.stderr)

    # baseline: affine map of the frozen leaf
    Xb = [[r["leaf"]] for r in fit]
    yb = [r["v4"] for r in fit]
    wb, bb, lamb, cvb = fit_with_cv(Xb, yb, [r["game"] for r in fit])
    r2_base = r2([r["v4"] for r in held], [bb + wb[0] * r["leaf"] for r in held])
    # refit-18
    X18 = [r["terms"] for r in fit]
    w18, b18, lam18, cv18 = fit_with_cv(X18, yb, [r["game"] for r in fit])
    r2_18 = r2([r["v4"] for r in held], [b18 + dot(w18, r["terms"]) for r in held])
    # refit-19
    X19 = [r["terms"] + [r["kf"]] for r in fit]
    w19, b19, lam19, cv19 = fit_with_cv(X19, yb, [r["game"] for r in fit])
    r2_19 = r2([r["v4"] for r in held], [b19 + dot(w19, r["terms"] + [r["kf"]]) for r in held])
    # in-sample for reference
    r2_18_fit = r2(yb, [b18 + dot(w18, r["terms"]) for r in fit])

    def write(path, w, bias, beta):
        with open(path, "w") as handle:
            for name, value in zip(LEAF_NAMES, w):
                handle.write("%s %r\n" % (name, value))
            handle.write("bias %r\n" % bias)
            handle.write("kf_beta %r\n" % beta)
            for name, value in zip(KF_NAMES, cem):
                handle.write("kf_%s %r\n" % (name, value))

    write(args.weights18_out, w18, b18, 0.0)
    write(args.weights19_out, w19[:18], b19, w19[18])
    # frozen-leaf disagreement diagnostics: how far the refit moved each weight
    ratios = {n: (w18[i] / FROZEN[i] if FROZEN[i] else None) for i, n in enumerate(LEAF_NAMES)}
    out = {
        "format": "drop7-oneply-q-fit-leaf-v1",
        "floor": args.floor,
        "fitRoots": len(fit), "fitRootsDropped": dropped_fit,
        "heldOutRoots": len(held), "heldOutRootsDropped": dropped_held,
        "baselineAffine": {"slope": wb[0], "intercept": bb, "lambda": lamb, "cv": cvb, "r2HeldOut": r2_base},
        "refit18": {"weights": dict(zip(LEAF_NAMES, w18)), "bias": b18, "lambda": lam18, "cv": cv18,
                    "r2HeldOut": r2_18, "r2Fit": r2_18_fit, "weightOverFrozen": ratios},
        "refit19": {"weights": dict(zip(LEAF_NAMES, w19[:18])), "kfBeta": w19[18], "bias": b19, "lambda": lam19,
                    "cv": cv19, "r2HeldOut": r2_19},
        "gainOverBaseline": (r2_18 - r2_base) if (r2_18 is not None and r2_base is not None) else None,
        "gain19Over18": (r2_19 - r2_18) if (r2_19 is not None and r2_18 is not None) else None,
    }
    with open(args.out, "w") as handle:
        json.dump(out, handle, indent=2)
    print("R2 held-out: baseline %.4f refit18 %.4f refit19 %.4f (fit18 in-sample %.4f)" % (
        r2_base, r2_18, r2_19, r2_18_fit), file=sys.stderr)


if __name__ == "__main__":
    main()
