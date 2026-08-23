# G0 continuation-engine fidelity ladder analysis (P-SOL-v1 section 5).
#
# Input: two or more panel2 files over IDENTICAL roots and CRN tapes (one per
# continuation engine).  For every engine pair this computes the per-root
# Kendall tau-b between the engines' sibling orderings of the KM restricted
# mean lifetime, averages tau over roots (roots with < 2 legal siblings are
# excluded, per the design), and attaches a cluster bootstrap over origin
# games (10,000 resamples, seed 0xb0071eaf per the preregistration).
#
# Reported bounds: lower95 is the one-sided 95% lower bound (5th percentile
# of the bootstrap distribution of the mean tau); the 2.5% / 97.5%
# percentiles are also reported.
#
# Usage: ladder.py --out ladder.json name=path [name=path ...]

import argparse
import itertools
import json
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from panel2_reader import Panel2File  # noqa: E402

BOOTSTRAP_SEED = 0xB0071EAF
BOOTSTRAP_RESAMPLES = 10_000


def kendall_tau_b(a, b):
    """Kendall tau-b for short vectors (<= 7 entries)."""
    n = len(a)
    concordant = discordant = 0
    ties_a = ties_b = 0
    for i in range(n):
        for j in range(i + 1, n):
            da = a[i] - a[j]
            db = b[i] - b[j]
            if da == 0 and db == 0:
                ties_a += 1
                ties_b += 1
            elif da == 0:
                ties_a += 1
            elif db == 0:
                ties_b += 1
            elif (da > 0) == (db > 0):
                concordant += 1
            else:
                discordant += 1
    pairs = n * (n - 1) / 2
    denom = np.sqrt((pairs - ties_a) * (pairs - ties_b))
    if denom == 0:
        return np.nan
    return (concordant - discordant) / denom


def per_root_taus(file_a, file_b):
    if file_a.count != file_b.count:
        raise ValueError("panel2 files have different record counts")
    if not np.array_equal(file_a.origin_seed, file_b.origin_seed) or not np.array_equal(
        file_a.move_index, file_b.move_index
    ) or not np.array_equal(file_a.root_board, file_b.root_board):
        raise ValueError("panel2 files are not over identical roots")
    mean_a = file_a.km_restricted_mean()
    mean_b = file_b.km_restricted_mean()
    taus, origins = [], []
    skipped = 0
    for r in range(file_a.count):
        legal = np.nonzero(file_a.legal[r] == 1)[0]
        if len(legal) < 2:
            skipped += 1
            continue
        tau = kendall_tau_b(mean_a[r, legal], mean_b[r, legal])
        if np.isnan(tau):
            # Both orderings fully tied on every pair: count as agreement 1.0
            # only if the (tied) vectors agree exactly; otherwise skip.
            tau = 1.0 if np.array_equal(mean_a[r, legal], mean_b[r, legal]) else np.nan
        if np.isnan(tau):
            skipped += 1
            continue
        taus.append(tau)
        origins.append(int(file_a.origin_seed[r]))
    return np.asarray(taus), np.asarray(origins), skipped


def cluster_bootstrap(taus, origins, resamples=BOOTSTRAP_RESAMPLES,
                      seed=BOOTSTRAP_SEED):
    rng = np.random.default_rng(seed)
    unique = np.unique(origins)
    by_origin = {o: taus[origins == o] for o in unique}
    means = np.empty(resamples)
    for i in range(resamples):
        pick = rng.choice(unique, size=len(unique), replace=True)
        sample = np.concatenate([by_origin[o] for o in pick])
        means[i] = sample.mean()
    return {
        "resamples": resamples,
        "seedHex": f"0x{seed:08x}",
        "clusters": int(len(unique)),
        "lower95OneSided": float(np.percentile(means, 5)),
        "percentile2p5": float(np.percentile(means, 2.5)),
        "percentile97p5": float(np.percentile(means, 97.5)),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="")
    parser.add_argument("files", nargs="+", help="name=path per engine")
    args = parser.parse_args()

    engines = {}
    for item in args.files:
        name, _, path = item.partition("=")
        if not path:
            raise SystemExit(f"expected name=path, got {item}")
        engines[name] = Panel2File(path)

    result = {"format": "drop7-panel2-ladder-tau-v1", "engines": {}, "pairs": {}}
    for name, f in engines.items():
        result["engines"][name] = {
            "path": f.path,
            "records": f.count,
            "k": f.K,
            "horizon": int(f.horizon[0]) if f.count else 0,
            "engineId": int(f.engine_id[0]) if f.count else None,
        }
    for a, b in itertools.combinations(engines, 2):
        taus, origins, skipped = per_root_taus(engines[a], engines[b])
        entry = {
            "roots": int(len(taus)),
            "skippedRoots": int(skipped),
            "meanTau": float(taus.mean()) if len(taus) else None,
            "medianTau": float(np.median(taus)) if len(taus) else None,
            "bootstrap": cluster_bootstrap(taus, origins) if len(taus) else None,
        }
        result["pairs"][f"{a}-vs-{b}"] = entry
    text = json.dumps(result, indent=2)
    print(text)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
