#!/usr/bin/env python3
"""Pinned paired-analysis adapter for the chain-reveal-leaf screen.

Reads two per-arm drop7-lifetime-cohort-v1 files written by run.cpp
(gamesDetail rows), pairs them by seed, and reports the statistics
preregistered in EX-20260823-reveal-construction-screen-371fd638 and its
successor: per-arm mean / median / Q25 / min / max of score and moves, paired
mean delta (score, moves, numbered clears, cover reveals), one-sided 95%
percentile-bootstrap lower bound over whole games (20,000 resamples, numpy
PCG64 seed 0xB0071EAF -- identical to leaf-evolution/compare.py, whose
resampler and Student-t quantile this file imports), Student-t one-sided 95%
lower bound, W-T-L, detection floor 1.645*sd/sqrt(n), first-half / second-half
deltas in seed order, Q25 delta (marginal, as in leaf-evolution), reveals/move,
clears/move, max chain depth and mean occupancy per arm, and coverage from the
arms index (divergentDecisions / shadowDecisions).

Usage:
  compare.py <candidate.cohort.json> <reference.cohort.json> [--index arms.json]
             [--first N] [--out report.json]

--first N restricts both arms to the first N games in seed order (the manual
futility / rarity checks of the stop conditions).  Written before the lease
opens; it computes nothing the runner did not record.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "leaf-evolution"))
import compare as le  # noqa: E402  (BOOTSTRAP_SEED, t_quantile_95)

BOOTSTRAP_SEED = le.BOOTSTRAP_SEED
assert BOOTSTRAP_SEED == 0xB007_1EAF
RESAMPLES = 20000


def load(path: str, first: int | None):
    art = json.load(open(path))
    if art.get("format") != "drop7-lifetime-cohort-v1":
        raise SystemExit(f"{path}: not a drop7-lifetime-cohort-v1 artifact")
    games = sorted(art["gamesDetail"], key=lambda g: int(g["seedHex"], 16))
    if first is not None:
        games = games[:first]
    return art, games


def arm_summary(art: dict, games: list) -> dict:
    s = np.array([g["score"] for g in games], dtype=float)
    m = np.array([g["moves"] for g in games], dtype=float)
    cleared = sum(g["numberedCleared"] for g in games)
    revealed = sum(g["coversRevealed"] for g in games)
    moves = sum(g["moves"] for g in games)
    return {
        "policy": art["policy"], "config": art["config"], "seedLease": art.get("seedLease"), "dataRole": art.get("dataRole"),
        "games": len(games), "censoredGames": int(sum(1 for g in games if g["censored"])),
        "score": {"mean": s.mean(), "median": float(np.median(s)), "q25": float(np.quantile(s, 0.25)), "min": s.min(), "max": s.max(),
                  "sd": s.std(ddof=1) if len(s) > 1 else 0.0},
        "moves": {"mean": m.mean(), "median": float(np.median(m)), "q25": float(np.quantile(m, 0.25)), "min": m.min(), "max": m.max()},
        "numberedClearsPerMove": cleared / moves if moves else 0.0,
        "coverRevealsPerMove": revealed / moves if moves else 0.0,
        "maxChainDepth": max(g["maxChainDepth"] for g in games),
        "meanOccupiedCells": float(np.mean([g["meanOccupiedCells"] for g in games])),
        "gamesAtOrAboveMillion": int((s >= 1_000_000).sum()),
    }


def paired(cand: list, ref: list, key: str) -> dict:
    a = np.array([g[key] for g in cand], dtype=float)
    b = np.array([g[key] for g in ref], dtype=float)
    d = a - b
    n = len(d)
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    idx = rng.integers(0, n, size=(RESAMPLES, n))
    boots = d[idx].mean(axis=1)
    sd = d.std(ddof=1) if n > 1 else 0.0
    half = n // 2
    return {
        "n": n, "meanDelta": d.mean(), "pairedSd": sd,
        "bootstrapLower95": float(np.quantile(boots, 0.05)), "bootstrapUpper95": float(np.quantile(boots, 0.95)),
        "studentTLower95": d.mean() - le.t_quantile_95(n - 1) * sd / math.sqrt(n) if n > 1 else d.mean(),
        "studentTQuantile": le.t_quantile_95(n - 1) if n > 1 else None,
        "detectionFloor": 1.645 * sd / math.sqrt(n) if n > 1 else 0.0,
        "wins": int((d > 0).sum()), "ties": int((d == 0).sum()), "losses": int((d < 0).sum()),
        "firstHalfMeanDelta": d[:half].mean() if half else None, "secondHalfMeanDelta": d[half:].mean() if half else None,
        "q25Delta": float(np.quantile(a, 0.25) - np.quantile(b, 0.25)),
        "bootstrapSeedHex": f"0x{BOOTSTRAP_SEED:08x}", "resamples": RESAMPLES,
    }


def coverage(index_path: str | None, arm_name: str | None) -> dict | None:
    if not index_path:
        return None
    index = json.load(open(index_path))
    arms = {a["name"]: a for a in index["arms"]}
    if arm_name not in arms:
        return {"error": f"arm {arm_name!r} not in index; arms: {sorted(arms)}"}
    a = arms[arm_name]
    shadow = a.get("shadowDecisions", 0)
    return {"arm": arm_name, "decisions": a["decisions"], "shadowDecisions": shadow, "divergentDecisions": a.get("divergentDecisions", 0),
            "coverage": a.get("divergentDecisions", 0) / shadow if shadow else None,
            "incompleteDecisions": a["incompleteDecisions"], "illegalDecisions": a["illegalDecisions"], "memoHitRate": a.get("memoHitRate"),
            "complete": index.get("complete"), "armArg": a.get("armArg"), "weights": a.get("weights")}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("candidate")
    p.add_argument("reference")
    p.add_argument("--index", help="arms index (drop7-chain-reveal-leaf-arms-v1) for coverage")
    p.add_argument("--arm", help="candidate arm name in the index (default: taken from the candidate config)")
    p.add_argument("--first", type=int, help="restrict to the first N games in seed order")
    p.add_argument("--out")
    args = p.parse_args()
    cart, cg = load(args.candidate, args.first)
    rart, rg = load(args.reference, args.first)
    cs, rs = [g["seedHex"] for g in cg], [g["seedHex"] for g in rg]
    if cs != rs:
        common = sorted(set(cs) & set(rs), key=lambda h: int(h, 16))
        cg = [g for g in cg if g["seedHex"] in common]
        rg = [g for g in rg if g["seedHex"] in common]
        note = f"seed sets differ ({len(cs)} vs {len(rs)}); restricted to {len(common)} common seeds"
    else:
        note = None
    if not cg:
        raise SystemExit("no paired games")
    arm_name = args.arm or cart["config"].get("arm")
    report = {
        "format": "drop7-chain-reveal-paired-v1", "candidate": args.candidate, "reference": args.reference, "first": args.first, "note": note,
        "candidateArm": arm_summary(cart, cg), "referenceArm": arm_summary(rart, rg),
        "pairedScore": paired(cg, rg, "score"), "pairedMoves": paired(cg, rg, "moves"),
        "pairedNumberedCleared": paired(cg, rg, "numberedCleared"), "pairedCoversRevealed": paired(cg, rg, "coversRevealed"),
        "coverage": coverage(args.index, arm_name),
    }
    text = json.dumps(report, indent=2, default=float)
    if args.out:
        open(args.out, "w").write(text + "\n")
    c, r, ps, pm = report["candidateArm"], report["referenceArm"], report["pairedScore"], report["pairedMoves"]
    print(f"{arm_name or 'candidate'} - {rart['config'].get('arm', 'reference')} on {ps['n']} paired games" + (f" (first {args.first})" if args.first else "") + (f"  [{note}]" if note else ""))
    print(f"  lease {c['seedLease']} / {c['dataRole']}; config d{c['config']['depth']} s{c['config']['chanceSamples']} maxWork {c['config']['maximumWork']}")
    print(f"  score   {c['score']['mean']:10.0f} vs {r['score']['mean']:10.0f}  delta {ps['meanDelta']:+9.0f}  bootLB95 {ps['bootstrapLower95']:+9.0f}  tLB95 {ps['studentTLower95']:+9.0f}"
          f"  W-T-L {ps['wins']}-{ps['ties']}-{ps['losses']}  floor {ps['detectionFloor']:.0f}  pairedSd {ps['pairedSd']:.0f}")
    print(f"  median  {c['score']['median']:10.0f} vs {r['score']['median']:10.0f}   q25 {c['score']['q25']:10.0f} vs {r['score']['q25']:10.0f} (delta {ps['q25Delta']:+.0f})"
          f"   min {c['score']['min']:.0f} vs {r['score']['min']:.0f}   max {c['score']['max']:.0f} vs {r['score']['max']:.0f}")
    print(f"  halves  {ps['firstHalfMeanDelta'] if ps['firstHalfMeanDelta'] is not None else float('nan'):+9.0f} / {ps['secondHalfMeanDelta'] if ps['secondHalfMeanDelta'] is not None else float('nan'):+9.0f}")
    print(f"  moves   {c['moves']['mean']:10.2f} vs {r['moves']['mean']:10.2f}  delta {pm['meanDelta']:+9.2f}  bootLB95 {pm['bootstrapLower95']:+9.2f}  tLB95 {pm['studentTLower95']:+9.2f}")
    print(f"  flow    clears/move {c['numberedClearsPerMove']:.4f} vs {r['numberedClearsPerMove']:.4f}  reveals/move {c['coverRevealsPerMove']:.4f} vs {r['coverRevealsPerMove']:.4f}"
          f"  occupancy {c['meanOccupiedCells']:.2f} vs {r['meanOccupiedCells']:.2f}  maxChain {c['maxChainDepth']} vs {r['maxChainDepth']}  censored {c['censoredGames']}/{r['censoredGames']}")
    cov = report["coverage"]
    if cov and "error" not in cov:
        print(f"  coverage {cov['divergentDecisions']}/{cov['shadowDecisions']} = {100 * cov['coverage']:.2f}% of shadowed decisions" if cov["coverage"] is not None else "  coverage n/a (no shadow decisions)",
              f"; incomplete {cov['incompleteDecisions']} illegal {cov['illegalDecisions']}; index complete={cov['complete']}")
    elif cov:
        print("  coverage:", cov["error"])


if __name__ == "__main__":
    main()
