"""Pure-Python statistics used by the suite-validation analyses.

This checkout's default interpreter has no numpy, and the GPU virtualenv is
another contributor's; nothing here needs a matrix library at the sizes
involved, so everything is written out explicitly rather than adding a
dependency to a research run.
"""

import math
import random


def mean(values):
    return sum(values) / len(values) if values else 0.0


def variance(values, ddof=1):
    n = len(values)
    if n - ddof <= 0:
        return 0.0
    m = mean(values)
    return sum((v - m) ** 2 for v in values) / (n - ddof)


def stdev(values, ddof=1):
    return math.sqrt(variance(values, ddof))


def ranks(values):
    """Average ranks, ties shared."""
    order = sorted(range(len(values)), key=lambda i: values[i])
    result = [0.0] * len(values)
    index = 0
    while index < len(order):
        stop = index
        while stop + 1 < len(order) and values[order[stop + 1]] == values[order[index]]:
            stop += 1
        shared = (index + stop) / 2.0 + 1.0
        for position in range(index, stop + 1):
            result[order[position]] = shared
        index = stop + 1
    return result


def pearson(xs, ys):
    n = len(xs)
    if n < 2:
        return 0.0
    mx, my = mean(xs), mean(ys)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    dy = math.sqrt(sum((y - my) ** 2 for y in ys))
    if dx == 0 or dy == 0:
        return 0.0
    return num / (dx * dy)


def spearman(xs, ys):
    return pearson(ranks(xs), ranks(ys))


def spearman_permutation_p(xs, ys, trials=200000, seed=0x5EED):
    """One-sided p for rho >= observed, by exact-in-the-limit permutation.

    n is tiny here (6 or 9 policies), so the permutation distribution is the
    honest null: it makes no normality assumption and no large-sample claim.
    """
    observed = spearman(xs, ys)
    rng = random.Random(seed)
    shuffled = list(ys)
    hits = 0
    for _ in range(trials):
        rng.shuffle(shuffled)
        if spearman(xs, shuffled) >= observed - 1e-12:
            hits += 1
    return observed, (hits + 1) / (trials + 1)


def paired_t(deltas):
    """Mean, standard error and t of a paired difference."""
    n = len(deltas)
    if n < 2:
        return 0.0, 0.0, 0.0, n
    m = mean(deltas)
    se = stdev(deltas) / math.sqrt(n)
    return m, se, (m / se if se > 0 else 0.0), n


def solve(matrix, rhs):
    """Gaussian elimination with partial pivoting.  Returns None if singular."""
    n = len(rhs)
    a = [row[:] + [rhs[i]] for i, row in enumerate(matrix)]
    for column in range(n):
        pivot = max(range(column, n), key=lambda r: abs(a[r][column]))
        if abs(a[pivot][column]) < 1e-12:
            return None
        a[column], a[pivot] = a[pivot], a[column]
        inverse = 1.0 / a[column][column]
        for row in range(column + 1, n):
            factor = a[row][column] * inverse
            if factor == 0.0:
                continue
            for k in range(column, n + 1):
                a[row][k] -= factor * a[column][k]
    x = [0.0] * n
    for column in range(n - 1, -1, -1):
        total = a[column][n] - sum(a[column][k] * x[k] for k in range(column + 1, n))
        x[column] = total / a[column][column]
    return x


def ols(rows, targets, ridge=1e-8):
    """Least squares with an intercept.  `rows` are feature vectors."""
    p = len(rows[0])
    design = [[1.0] + list(row) for row in rows]
    n = p + 1
    xtx = [[0.0] * n for _ in range(n)]
    xty = [0.0] * n
    for row, y in zip(design, targets):
        for i in range(n):
            ri = row[i]
            if ri == 0.0:
                continue
            for j in range(i, n):
                xtx[i][j] += ri * row[j]
            xty[i] += ri * y
    for i in range(n):
        for j in range(i):
            xtx[i][j] = xtx[j][i]
        xtx[i][i] += ridge
    return solve(xtx, xty)


def predict(beta, row):
    return beta[0] + sum(b * v for b, v in zip(beta[1:], row))


def r_squared(beta, rows, targets):
    if beta is None:
        return float("nan")
    my = mean(targets)
    ss_tot = sum((y - my) ** 2 for y in targets)
    ss_res = sum((y - predict(beta, row)) ** 2 for row, y in zip(rows, targets))
    if ss_tot == 0:
        return float("nan")
    return 1.0 - ss_res / ss_tot


def standardize(columns):
    """Return (standardized columns, means, stdevs) for a list of columns."""
    out, means, sds = [], [], []
    for column in columns:
        m = mean(column)
        s = stdev(column) or 1.0
        out.append([(v - m) / s for v in column])
        means.append(m)
        sds.append(s)
    return out, means, sds


def quantile(sorted_values, fraction):
    if not sorted_values:
        return 0.0
    index = min(len(sorted_values) - 1,
                max(0, int(round(fraction * (len(sorted_values) - 1)))))
    return sorted_values[index]
