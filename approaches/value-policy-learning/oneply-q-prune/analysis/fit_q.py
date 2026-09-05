#!/usr/bin/env python3
"""Fit linear action values to exact depth-4 sibling values and score rankers.

Pure standard library.  Reads the panel NDJSON written by the `panel` binary,
fits ridge regression on the fit games (cohort ordinals below --train-games),
selects lambda by 4-fold game-grouped cross-validation, writes the fitted
weights in the `name value` format the `lq=` prior reads, and reports ranking
metrics of every ranker against the exact depth-4 values on the held-out
games.

  fit_q.py --panel panel.ndjson --train-games 32 --cem-weights weights-cem.txt
           --out fit.json --weights-out weights-lq.txt --weights-raw-out weights-lq-raw.txt
"""
import argparse
import json
import math
import random
import sys

COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6]
LAMBDAS = [0.001, 0.01, 0.1, 1.0, 10.0]
KF_OFFSET = 20
KF_COUNT = 6
BIAS = 31
COUNT = 32
KF_NAMES = ["min_eq_elem_True", "row_dets", "col_dets", "max_eq_elem", "1_dets", "elem_det"]


def feature_names():
    leaf = ["open_columns", "height_load", "solid_cells", "cracked_cells", "numbered_cells",
            "high_low_numbers", "direct_potential", "latent_chain_potential", "cracked_exposure",
            "solid_exposure", "adjacent_ones", "triple_twos", "dead_low_numbers",
            "covered_height_risk", "low_number_height_risk", "danger_height_squared",
            "rise_pressure", "next_disc_vertical_options"]
    names = ["leaf_" + n for n in leaf] + ["delta_mean", "terminal_frac"]
    names += ["kf_" + n for n in KF_NAMES] + ["rise_%d" % m for m in range(1, 6)] + ["bias"]
    assert len(names) == COUNT
    return names


def load_panel(path):
    roots = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                roots.append(json.loads(line))
    return roots


def load_kf_weights(path):
    weights = {}
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 2:
                weights[parts[0]] = float(parts[1])
    return [weights.get(n, 0.0) for n in KF_NAMES]


def solve(matrix, rhs):
    """Gaussian elimination with partial pivoting on a small dense system."""
    n = len(rhs)
    a = [row[:] + [rhs[i]] for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-300:
            continue
        a[col], a[pivot] = a[pivot], a[col]
        inv = 1.0 / a[col][col]
        for r in range(n):
            if r != col and a[r][col] != 0.0:
                factor = a[r][col] * inv
                row_r = a[r]
                row_c = a[col]
                for k in range(col, n + 1):
                    row_r[k] -= factor * row_c[k]
    return [a[i][n] / a[i][i] if abs(a[i][i]) >= 1e-300 else 0.0 for i in range(n)]


def gram_of(X, y):
    """(X^T X, X^T y, n) accumulated once so every lambda reuses it."""
    n = len(X)
    p = len(X[0])
    gram = [[0.0] * p for _ in range(p)]
    xty = [0.0] * p
    for row, target in zip(X, y):
        for i in range(p):
            xi = row[i]
            if xi == 0.0:
                continue
            xty[i] += xi * target
            gram_i = gram[i]
            for j in range(p):
                gram_i[j] += xi * row[j]
    return gram, xty, n


def ridge_from(gram, xty, n, lam):
    """Minimise (1/n)||y - Xw||^2 + lam ||w||^2 from a precomputed Gram."""
    p = len(xty)
    a = [[gram[i][j] / n for j in range(p)] for i in range(p)]
    for i in range(p):
        a[i][i] += lam
    return solve(a, [v / n for v in xty])


def ridge(X, y, lam):
    """Minimise (1/n)||y - Xw||^2 + lam ||w||^2 (no intercept; caller centres)."""
    gram, xty, n = gram_of(X, y)
    return ridge_from(gram, xty, n, lam)


def dot(w, x):
    return sum(a * b for a, b in zip(w, x))


def argmax_first(values):
    best = None
    for i, v in enumerate(values):
        if best is None or v > values[best]:
            best = i
    return best


def rank_metrics(scores, truth):
    """scores/truth aligned with the root's legal list (in COLUMN_ORDER)."""
    n = len(truth)
    best_value = max(truth)
    worst_value = min(truth)
    truth_best = [i for i, v in enumerate(truth) if v == best_value]
    order = sorted(range(n), key=lambda i: (-scores[i], i))  # stable: ties keep centre order
    chosen = order[0]
    top1 = 1.0 if chosen in truth_best else 0.0
    recall = {}
    for k in (1, 2, 3, 4):
        recall[k] = 1.0 if any(i in truth_best for i in order[:k]) else 0.0
    spread = best_value - worst_value
    regret = 0.0 if spread <= 0 else (best_value - truth[chosen]) / spread
    raw_regret = best_value - truth[chosen]
    return top1, recall, regret, raw_regret, chosen


def spearman(scores, truth):
    n = len(truth)
    if n < 3:
        return None

    def ranks(values):
        order = sorted(range(n), key=lambda i: values[i])
        result = [0.0] * n
        i = 0
        while i < n:
            j = i
            while j + 1 < n and values[order[j + 1]] == values[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1.0
            for k in range(i, j + 1):
                result[order[k]] = avg
            i = j + 1
        return result

    ra, rb = ranks(scores), ranks(truth)
    ma, mb = sum(ra) / n, sum(rb) / n
    cov = sum((a - ma) * (b - mb) for a, b in zip(ra, rb))
    va = sum((a - ma) ** 2 for a in ra)
    vb = sum((b - mb) ** 2 for b in rb)
    if va == 0 or vb == 0:
        return None
    return cov / math.sqrt(va * vb)


def evaluate_ranker(name, roots, scorer):
    per_game = {}
    rows = []
    for root in roots:
        truth = root["v4"]
        if len(truth) < 2:
            continue
        scores = scorer(root)
        top1, recall, regret, raw, chosen = rank_metrics(scores, truth)
        rho = spearman(scores, truth)
        rows.append((root["game"], top1, recall, regret, raw, rho))
    n = len(rows)
    out = {
        "ranker": name,
        "roots": n,
        "top1": sum(r[1] for r in rows) / n,
        "recall2": sum(r[2][2] for r in rows) / n,
        "recall3": sum(r[2][3] for r in rows) / n,
        "recall4": sum(r[2][4] for r in rows) / n,
        "meanNormalisedRegret": sum(r[3] for r in rows) / n,
        "meanRawRegret": sum(r[4] for r in rows) / n,
        "meanSpearman": (sum(r[5] for r in rows if r[5] is not None)
                         / max(1, sum(1 for r in rows if r[5] is not None))),
    }
    return out, rows


def game_bootstrap(rows_by_game, stat, resamples, rng):
    games = sorted(rows_by_game)
    values = []
    for _ in range(resamples):
        sample = [games[rng.randrange(len(games))] for _ in games]
        pooled = []
        for g in sample:
            pooled.extend(rows_by_game[g])
        values.append(stat(pooled))
    values.sort()
    return values


def percentile(sorted_values, q):
    if not sorted_values:
        return None
    index = min(len(sorted_values) - 1, max(0, int(q * (len(sorted_values) - 1))))
    return sorted_values[index]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--panel", required=True)
    parser.add_argument("--train-games", type=int, default=32)
    parser.add_argument("--cem-weights", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--weights-out", required=True)
    parser.add_argument("--weights-raw-out", required=True)
    parser.add_argument("--resamples", type=int, default=10000)
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0x6B660002)
    args = parser.parse_args()

    names = feature_names()
    roots = load_panel(args.panel)
    train = [r for r in roots if r["game"] < args.train_games]
    held = [r for r in roots if r["game"] >= args.train_games]
    print("panel: %d roots, %d fit, %d held-out" % (len(roots), len(train), len(held)), file=sys.stderr)

    # --- design matrices ---------------------------------------------------
    def rows_of(subset):
        X, y, rid, gid = [], [], [], []
        for k, root in enumerate(subset):
            for sib, v in zip(root["siblings"], root["v4"]):
                X.append(sib["f"])
                y.append(v)
                rid.append(k)
                gid.append(root["game"])
        return X, y, rid, gid

    Xtr, ytr, rtr, gtr = rows_of(train)
    n = len(Xtr)
    mean = [sum(row[j] for row in Xtr) / n for j in range(COUNT)]
    sd = []
    for j in range(COUNT):
        var = sum((row[j] - mean[j]) ** 2 for row in Xtr) / n
        sd.append(math.sqrt(var) if var > 0 else 0.0)
    active = [j for j in range(COUNT) if sd[j] > 0]

    def standardise(row):
        return [(row[j] - mean[j]) / sd[j] for j in active]

    def centred_rows(X, y, rid):
        # subtract each root's sibling mean from features and target
        groups = {}
        for i, r in enumerate(rid):
            groups.setdefault(r, []).append(i)
        Xc, yc = [None] * len(X), [None] * len(y)
        for members in groups.values():
            m = len(members)
            fmean = [sum(X[i][j] for i in members) / m for j in range(len(X[0]))]
            ymean = sum(y[i] for i in members) / m
            for i in members:
                Xc[i] = [X[i][j] - fmean[j] for j in range(len(X[0]))]
                yc[i] = y[i] - ymean
        return Xc, yc

    Xs = [standardise(row) for row in Xtr]
    y_mean = sum(ytr) / n
    y_raw = [v - y_mean for v in ytr]
    Xc, yc = centred_rows(Xs, ytr, rtr)

    # --- lambda by 4-fold game-grouped CV ---------------------------------
    folds = {}
    for i, g in enumerate(gtr):
        folds.setdefault(g % 4, []).append(i)
    cv = {"raw": {}, "centred": {}}
    for kind, (Xk, yk) in (("raw", (Xs, y_raw)), ("centred", (Xc, yc))):
        fold_grams = {}
        for fold, members in folds.items():
            held_set = set(members)
            fold_grams[fold] = gram_of([Xk[i] for i in range(n) if i not in held_set],
                                       [yk[i] for i in range(n) if i not in held_set])
        for lam in LAMBDAS:
            sse, cnt = 0.0, 0
            for fold, members in folds.items():
                g, xty, nf = fold_grams[fold]
                w = ridge_from(g, xty, nf, lam)
                for i in members:
                    sse += (yk[i] - dot(w, Xk[i])) ** 2
                    cnt += 1
            cv[kind][str(lam)] = sse / cnt
    lam_raw = min(LAMBDAS, key=lambda l: cv["raw"][str(l)])
    lam_c = min(LAMBDAS, key=lambda l: cv["centred"][str(l)])
    w_raw_std = ridge(Xs, y_raw, lam_raw)
    w_c_std = ridge(Xc, yc, lam_c)

    def to_raw_units(w_std, intercept):
        w = [0.0] * COUNT
        for k, j in enumerate(active):
            w[j] = w_std[k] / sd[j]
        w[BIAS] = intercept - sum(w[j] * mean[j] for j in active)
        return w

    w_raw = to_raw_units(w_raw_std, y_mean)
    w_c = to_raw_units(w_c_std, 0.0)
    w_c[BIAS] = 0.0  # the centred fit has no intercept; ranking is bias-free
    for path, w in ((args.weights_out, w_c), (args.weights_raw_out, w_raw)):
        with open(path, "w") as handle:
            for name, value in zip(names, w):
                handle.write("%s %r\n" % (name, value))

    # --- rankers on held-out roots ----------------------------------------
    cem = load_kf_weights(args.cem_weights)
    rankers = {
        "center": lambda root: [-i for i in range(len(root["legal"]))],
        "kf-cem": lambda root: [dot(cem, s["f"][KF_OFFSET:KF_OFFSET + KF_COUNT]) for s in root["siblings"]],
        "d1@4": lambda root: [s["d1"] for s in root["siblings"]],
        "v1": lambda root: root["v1"],
        "v2": lambda root: root["v2"],
        "v3": lambda root: root["v3"],
        "lq-centred": lambda root: [dot(w_c, s["f"]) for s in root["siblings"]],
        "lq-raw": lambda root: [dot(w_raw, s["f"]) for s in root["siblings"]],
    }
    results = {}
    rows_by_ranker = {}
    for name, scorer in rankers.items():
        summary, rows = evaluate_ranker(name, held, scorer)
        results[name] = summary
        rows_by_ranker[name] = rows

    # R^2 on held-out rows
    Xh, yh, rh, _ = rows_of(held)
    yh_mean = sum(yh) / len(yh)
    sst = sum((v - yh_mean) ** 2 for v in yh)
    sse = sum((v - dot(w_raw, x)) ** 2 for x, v in zip(Xh, yh))
    r2_raw = 1.0 - sse / sst if sst > 0 else None
    Xhc, yhc = centred_rows(Xh, yh, rh)
    sstc = sum(v * v for v in yhc)
    ssec = sum((v - dot(w_c, x)) ** 2 for x, v in zip(Xhc, yhc))
    r2_c = 1.0 - ssec / sstc if sstc > 0 else None

    # game-clustered bootstrap: lq vs d1@4 paired top-1 and recall@3
    rng = random.Random(args.seed)
    by_game = {}
    lq_rows = rows_by_ranker["lq-centred"]
    d1_rows = rows_by_ranker["d1@4"]
    for a, b in zip(lq_rows, d1_rows):
        by_game.setdefault(a[0], []).append((a[1] - b[1], a[2][3] - b[2][3], a[1], a[2][3]))
    boot_top1 = game_bootstrap(by_game, lambda p: sum(x[0] for x in p) / len(p), args.resamples, rng)
    boot_rec3 = game_bootstrap(by_game, lambda p: sum(x[1] for x in p) / len(p), args.resamples, rng)
    boot_lq_top1 = game_bootstrap(by_game, lambda p: sum(x[2] for x in p) / len(p), args.resamples, rng)
    boot_lq_rec3 = game_bootstrap(by_game, lambda p: sum(x[3] for x in p) / len(p), args.resamples, rng)

    out = {
        "format": "drop7-oneply-q-fit-v1",
        "panelRoots": len(roots),
        "fitGames": args.train_games,
        "fitRoots": len(train),
        "fitRows": n,
        "heldOutRoots": len(held),
        "heldOutRows": len(Xh),
        "heldOutGames": sorted({r["game"] for r in held}),
        "lambdaGrid": LAMBDAS,
        "cvMse": cv,
        "lambdaRaw": lam_raw,
        "lambdaCentred": lam_c,
        "r2HeldOutRaw": r2_raw,
        "r2HeldOutCentredWithinRoot": r2_c,
        "weightsCentred": dict(zip(names, w_c)),
        "weightsRaw": dict(zip(names, w_raw)),
        "featureMean": dict(zip(names, mean)),
        "featureSd": dict(zip(names, sd)),
        "rankers": results,
        "bootstrapOverGames": {
            "resamples": args.resamples,
            "seed": args.seed,
            "lqMinusD1Top1": {"lb95": percentile(boot_top1, 0.05), "median": percentile(boot_top1, 0.5)},
            "lqMinusD1Recall3": {"lb95": percentile(boot_rec3, 0.05), "median": percentile(boot_rec3, 0.5)},
            "lqTop1": {"lb95": percentile(boot_lq_top1, 0.05), "ub95": percentile(boot_lq_top1, 0.95)},
            "lqRecall3": {"lb95": percentile(boot_lq_rec3, 0.05), "ub95": percentile(boot_lq_rec3, 0.95)},
        },
    }
    with open(args.out, "w") as handle:
        json.dump(out, handle, indent=2)
    for name, s in results.items():
        print("%-11s top1 %.3f r@2 %.3f r@3 %.3f r@4 %.3f regret %.4f rho %.3f" % (
            name, s["top1"], s["recall2"], s["recall3"], s["recall4"],
            s["meanNormalisedRegret"], s["meanSpearman"]), file=sys.stderr)
    print("lambda raw %g centred %g; R2 raw %.4f centred %.4f" % (lam_raw, lam_c, r2_raw or 0, r2_c or 0), file=sys.stderr)


if __name__ == "__main__":
    main()
