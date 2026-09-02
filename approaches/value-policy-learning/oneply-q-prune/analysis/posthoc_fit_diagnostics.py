#!/usr/bin/env python3
"""POST-HOC diagnostics for the linear Q fit (read after the held-out metrics
of EX-20260902-oneply-q-fit-and-pruned-search-panel-46c75cdf; never evidence).

1. Bug check: rebuild the exact d1 value from the 32 features with the
   frozen leaf constants and the terminal utility; report the worst absolute
   difference against the sibling's recorded d1.
2. Objective check: held-out MSE of an affine map of d1 versus the ridge fit,
   and the ridge fit's in-sample ranking on the fit games.
3. Clipped-target refit: the same ridge on per-root-centred targets after
   clipping v4 at --clip (default -50,000) so that deaths stop dominating.
"""
import argparse
import json
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from fit_q import (load_panel, feature_names, gram_of, ridge_from, dot, evaluate_ranker,
                   LAMBDAS, COUNT, BIAS)  # noqa: E402

FROZEN = [180.0, -20.0, -620.0, -220.0, -18.0, -90.0, 1600.0, 700.0, 100.0, 40.0, -550.0,
          -750.0, -120.0, -95.0, -85.0, -1250.0, -35.0, 220.0]
TERMINAL = -1_000_000.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--panel", required=True)
    ap.add_argument("--fit", required=True, help="fit.json from fit_q.py")
    ap.add_argument("--train-games", type=int, default=32)
    ap.add_argument("--clip", type=float, default=-50000.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--weights-out", required=True)
    args = ap.parse_args()
    names = feature_names()
    roots = load_panel(args.panel)
    fit = json.load(open(args.fit))
    w_raw = [fit["weightsRaw"][n] for n in names]
    w_c = [fit["weightsCentred"][n] for n in names]
    train = [r for r in roots if r["game"] < args.train_games]
    held = [r for r in roots if r["game"] >= args.train_games]

    # 1. rebuild d1 from the features
    w_d1 = [0.0] * COUNT
    for j in range(18):
        w_d1[j] = FROZEN[j]
    w_d1[18] = 1.0
    w_d1[19] = TERMINAL
    worst = 0.0
    for r in roots:
        for s in r["siblings"]:
            worst = max(worst, abs(dot(w_d1, s["f"]) - s["d1"]))

    # 2. objective check
    def rows(subset):
        X, y = [], []
        for r in subset:
            for s, v in zip(r["siblings"], r["v4"]):
                X.append(s["f"]); y.append(v)
        return X, y
    Xh, yh = rows(held)
    Xt, yt = rows(train)
    d1h = [dot(w_d1, x) for x in Xh]
    d1t = [dot(w_d1, x) for x in Xt]
    # affine map of d1 fitted on train
    n = len(d1t); mx = sum(d1t) / n; my = sum(yt) / n
    cov = sum((a - mx) * (b - my) for a, b in zip(d1t, yt)); var = sum((a - mx) ** 2 for a in d1t)
    b = cov / var; a = my - b * mx
    mse_d1 = sum((v - (a + b * d)) ** 2 for v, d in zip(yh, d1h)) / len(yh)
    mse_lq = sum((v - dot(w_raw, x)) ** 2 for v, x in zip(yh, Xh)) / len(yh)
    lq_train, _ = evaluate_ranker("lq-raw-insample", train, lambda root: [dot(w_raw, s["f"]) for s in root["siblings"]])
    d1_train, _ = evaluate_ranker("d1-insample", train, lambda root: [s["d1"] for s in root["siblings"]])

    # 3. clipped-target centred refit (post hoc)
    Xc, yc = [], []
    for r in train:
        vals = [max(v, args.clip) for v in r["v4"]]
        m = len(vals)
        fmean = [sum(s["f"][j] for s in r["siblings"]) / m for j in range(COUNT)]
        ymean = sum(vals) / m
        for s, v in zip(r["siblings"], vals):
            Xc.append([s["f"][j] - fmean[j] for j in range(COUNT)]); yc.append(v - ymean)
    # standardise on the centred design (bias column is zero after centring)
    p = COUNT
    sd = []
    for j in range(p):
        var = sum(x[j] ** 2 for x in Xc) / len(Xc)
        sd.append(var ** 0.5 if var > 0 else 0.0)
    active = [j for j in range(p) if sd[j] > 0]
    Xs = [[x[j] / sd[j] for j in active] for x in Xc]
    gids = [r["game"] for r in train for _ in r["siblings"]]
    folds = {}
    for i, g in enumerate(gids):
        folds.setdefault(g % 4, []).append(i)
    grams = {}
    for f, members in folds.items():
        hs = set(members)
        grams[f] = gram_of([Xs[i] for i in range(len(Xs)) if i not in hs], [yc[i] for i in range(len(Xs)) if i not in hs])
    cv = {}
    for lam in LAMBDAS:
        sse = cnt = 0
        for f, members in folds.items():
            g, xty, nf = grams[f]
            w = ridge_from(g, xty, nf, lam)
            for i in members:
                sse += (yc[i] - dot(w, Xs[i])) ** 2; cnt += 1
        cv[str(lam)] = sse / cnt
    lam = min(LAMBDAS, key=lambda l: cv[str(l)])
    g, xty, nf = gram_of(Xs, yc)
    ws = ridge_from(g, xty, nf, lam)
    w_clip = [0.0] * COUNT
    for k, j in enumerate(active):
        w_clip[j] = ws[k] / sd[j]
    with open(args.weights_out, "w") as h:
        for nme, v in zip(names, w_clip):
            h.write("%s %r\n" % (nme, v))
    clip_held, _ = evaluate_ranker("lq-clipped-posthoc", held, lambda root: [dot(w_clip, s["f"]) for s in root["siblings"]])
    clip_train, _ = evaluate_ranker("lq-clipped-posthoc-insample", train, lambda root: [dot(w_clip, s["f"]) for s in root["siblings"]])
    out = {
        "format": "drop7-oneply-q-posthoc-fit-v1",
        "label": "POST HOC: read after the preregistered held-out metrics; diagnostic only",
        "d1RebuildWorstAbsDiff": worst,
        "heldOutMse": {"affineOfD1": mse_d1, "ridgeRaw": mse_lq},
        "inSample": {"lqRaw": lq_train, "d1": d1_train, "lqClipped": clip_train},
        "clip": args.clip, "clipLambda": lam, "clipCv": cv,
        "heldOut": {"lqClipped": clip_held},
        "weightsClipped": dict(zip(names, w_clip)),
    }
    json.dump(out, open(args.out, "w"), indent=2)
    print("d1 rebuild worst |diff| %.6g" % worst)
    print("held-out MSE: affine(d1) %.4g  ridge-raw %.4g" % (mse_d1, mse_lq))
    print("in-sample top1: lq-raw %.3f  d1 %.3f  lq-clipped %.3f" % (lq_train["top1"], d1_train["top1"], clip_train["top1"]))
    print("held-out lq-clipped (post hoc): top1 %.3f r@2 %.3f r@3 %.3f r@4 %.3f regret %.4f lambda %g" % (
        clip_held["top1"], clip_held["recall2"], clip_held["recall3"], clip_held["recall4"], clip_held["meanNormalisedRegret"], lam))


if __name__ == "__main__":
    main()
