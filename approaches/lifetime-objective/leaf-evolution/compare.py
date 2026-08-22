#!/usr/bin/env python3
"""Paired whole-game comparison of two individuals in one population artifact.

Reports, for candidate minus reference on identical seeds: mean delta, paired
sd, one-sided 95% percentile-bootstrap lower bound over whole games (20,000
resamples, seed given), W-T-L, first-half / second-half means, Q25 deltas, and
the detection floor 1.645 * sd / sqrt(n) that docs/research/status.md asks to
be stated next to every null.  Nothing here extrapolates.
"""
from __future__ import annotations

import argparse
import json
import math

import numpy as np

BOOTSTRAP_SEED = 0xB007_1EAF


def t_quantile_95(df: int) -> float:
    """One-sided 95% Student-t quantile by the Cornish-Fisher expansion in the
    normal quantile z = 1.6448536 (accurate to about 1e-4 for df >= 10; the
    adversarial review noted the earlier code used z itself, 1.5% optimistic at
    df = 63).  t(63) = 1.6694, t(31) = 1.6955, t(255) = 1.6510."""
    z = 1.6448536269514722
    g1 = (z ** 3 + z) / 4
    g2 = (5 * z ** 5 + 16 * z ** 3 + 3 * z) / 96
    g3 = (3 * z ** 7 + 19 * z ** 5 + 17 * z ** 3 - 15 * z) / 384
    g4 = (79 * z ** 9 + 776 * z ** 7 + 1482 * z ** 5 - 1920 * z ** 3 - 945 * z) / 92160
    return z + g1 / df + g2 / df ** 2 + g3 / df ** 3 + g4 / df ** 4


def summary(ind: dict) -> dict:
    s = np.array([g["score"] for g in ind["games"]], dtype=float)
    m = np.array([g["moves"] for g in ind["games"]], dtype=float)
    occ = np.array([g["meanOccupiedCells"] for g in ind["games"]], dtype=float)
    return {
        "name": ind["name"], "games": len(s),
        "score": {"mean": s.mean(), "median": float(np.median(s)), "sd": s.std(ddof=1) if len(s) > 1 else 0.0,
                  "q25": float(np.quantile(s, 0.25)), "min": s.min(), "max": s.max()},
        "moves": {"mean": m.mean(), "q25": float(np.quantile(m, 0.25))},
        "numberedClearsPerMove": ind["numberedClearsPerMove"],
        "coverRevealsPerMove": ind["coverRevealsPerMove"],
        "maxChainDepth": max(g["maxChainDepth"] for g in ind["games"]),
        "meanOccupiedCells": occ.mean(),
        "censoredGames": ind["censoredGames"],
        "incompleteDecisions": ind["incompleteDecisions"],
        "illegalDecisions": ind["illegalDecisions"],
        "gamesAtOrAboveMillion": int((s >= 1_000_000).sum()),
    }


def paired(cand: dict, ref: dict, key: str) -> dict:
    a = np.array([g[key] for g in cand["games"]], dtype=float)
    b = np.array([g[key] for g in ref["games"]], dtype=float)
    assert [g["seedHex"] for g in cand["games"]] == [g["seedHex"] for g in ref["games"]], "seed alignment"
    d = a - b
    n = len(d)
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    idx = rng.integers(0, n, size=(20000, n))
    boots = d[idx].mean(axis=1)
    sd = d.std(ddof=1) if n > 1 else 0.0
    half = n // 2
    return {
        "n": n, "meanDelta": d.mean(), "pairedSd": sd,
        "bootstrapLower95": float(np.quantile(boots, 0.05)),
        "bootstrapUpper95": float(np.quantile(boots, 0.95)),
        "studentTLower95": d.mean() - t_quantile_95(n - 1) * sd / math.sqrt(n) if n > 1 else d.mean(),
        "studentTQuantile": t_quantile_95(n - 1) if n > 1 else None,
        "detectionFloor": 1.645 * sd / math.sqrt(n) if n > 1 else 0.0,
        "detectionFloorNote": "1.645 * sd / sqrt(n), the z-based floor defined in docs/research/status.md",
        "wins": int((d > 0).sum()), "ties": int((d == 0).sum()), "losses": int((d < 0).sum()),
        "firstHalfMeanDelta": d[:half].mean(), "secondHalfMeanDelta": d[half:].mean(),
        "q25Delta": float(np.quantile(a, 0.25) - np.quantile(b, 0.25)),
        "largestSwings": sorted(d.tolist(), key=abs, reverse=True)[:5],
        "bootstrapSeedHex": f"0x{BOOTSTRAP_SEED:08x}", "resamples": 20000,
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("artifact")
    p.add_argument("--candidate", required=True)
    p.add_argument("--reference", required=True)
    p.add_argument("--out")
    args = p.parse_args()
    art = json.load(open(args.artifact))
    by = {i["name"]: i for i in art["individuals"]}
    cand, ref = by[args.candidate], by[args.reference]
    report = {
        "format": "drop7-leaf-evolution-paired-v1",
        "artifact": args.artifact, "config": art["config"], "seedStartHex": art["seedStartHex"],
        "candidate": summary(cand), "reference": summary(ref),
        "pairedScore": paired(cand, ref, "score"),
        "pairedMoves": paired(cand, ref, "moves"),
        "pairedNumberedCleared": paired(cand, ref, "numberedCleared"),
        "pairedCoversRevealed": paired(cand, ref, "coversRevealed"),
    }
    text = json.dumps(report, indent=2, default=float)
    if args.out:
        open(args.out, "w").write(text + "\n")
    ps = report["pairedScore"]
    print(f"{args.candidate} - {args.reference} on {ps['n']} paired games (seeds {art['seedStartHex']}+):")
    print(f"  score   {report['candidate']['score']['mean']:10.0f} vs {report['reference']['score']['mean']:10.0f}"
          f"  delta {ps['meanDelta']:+9.0f}  LB95 {ps['bootstrapLower95']:+9.0f}  UB95 {ps['bootstrapUpper95']:+9.0f}"
          f"  W-T-L {ps['wins']}-{ps['ties']}-{ps['losses']}  floor {ps['detectionFloor']:.0f}")
    pm = report["pairedMoves"]
    print(f"  moves   {report['candidate']['moves']['mean']:10.2f} vs {report['reference']['moves']['mean']:10.2f}"
          f"  delta {pm['meanDelta']:+9.2f}  LB95 {pm['bootstrapLower95']:+9.2f}")
    print(f"  halves  {ps['firstHalfMeanDelta']:+9.0f} / {ps['secondHalfMeanDelta']:+9.0f}   q25 delta {ps['q25Delta']:+9.0f}"
          f"   max {report['candidate']['score']['max']:.0f} vs {report['reference']['score']['max']:.0f}")
    print(f"  flow    clears/move {report['candidate']['numberedClearsPerMove']:.4f} vs {report['reference']['numberedClearsPerMove']:.4f}"
          f"  reveals/move {report['candidate']['coverRevealsPerMove']:.4f} vs {report['reference']['coverRevealsPerMove']:.4f}"
          f"  occupancy {report['candidate']['meanOccupiedCells']:.2f} vs {report['reference']['meanOccupiedCells']:.2f}")


if __name__ == "__main__":
    main()
