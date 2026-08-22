#!/usr/bin/env python3
"""Tabulate the sweep's held-out metrics and evaluate the preregistered gate.

Gate (EX-20260822-nnue-leaf-capacity-sweep): let base be the h64-m32-e10-lr3e-3
seed-0 run, spread the largest |Pearson(seed k) - Pearson(base)| over the
seed replicas, and delta the best non-replica configuration's Pearson minus
base.  supported-as-tested (saturated) if delta <= max(0.005, 2*spread);
not-supported (capacity/training matters) if delta >= 0.02 and delta > 3*spread;
inconclusive otherwise.  Nothing here is a gameplay claim.
"""
import json, sys, glob, os

run = sys.argv[1]
rows = []
for path in sorted(glob.glob(os.path.join(run, "models", "*.json"))):
    d = json.load(open(path))
    tag = os.path.basename(path)[:-5]
    ho = d["heldOut"]
    rows.append({
        "tag": tag, "hidden": d["args"]["hidden"], "mid": d["args"]["mid"], "epochs": d["args"]["epochs"],
        "lr": d["args"]["lr"], "seed": d["args"]["seed"], "parameters": d["parameters"],
        "lifetimePearson": ho["lifetimePearson"], "lifetimeMAE": ho["lifetimeMeanAbsoluteErrorMoves"],
        "hazardAccMean": sum(ho["hazardAccuracyByRise"]) / len(ho["hazardAccuracyByRise"]),
        "mirroredPearson": d["heldOutMirrored"]["lifetimePearson"],
        "trainWallSeconds": d["history"][-1]["wallSeconds"], "finalTrainLoss": d["history"][-1]["trainLoss"],
    })
by = {r["tag"]: r for r in rows}
base = by.get("h64-m32-e10-lr3e3-s0")
out = {"format": "drop7-leaf-capacity-sweep-summary-v1", "runs": rows}
if base:
    replicas = [by[t] for t in ("h64-m32-e10-lr3e3-s1", "h64-m32-e10-lr3e3-s2") if t in by]
    spread = max([abs(r["lifetimePearson"] - base["lifetimePearson"]) for r in replicas], default=None)
    others = [r for r in rows if not r["tag"].startswith("h64-m32-e10-lr3e3-s")]
    best = max(others, key=lambda r: r["lifetimePearson"]) if others else None
    delta = best["lifetimePearson"] - base["lifetimePearson"] if best else None
    verdict = None
    if spread is not None and delta is not None:
        if delta <= max(0.005, 2 * spread): verdict = "supported-as-tested"
        elif delta >= 0.02 and delta > 3 * spread: verdict = "not-supported-as-tested"
        else: verdict = "inconclusive"
    out.update({"basePearson": base["lifetimePearson"], "seedSpread": spread, "bestTag": best["tag"] if best else None,
                "bestPearson": best["lifetimePearson"] if best else None, "deltaBestMinusBase": delta, "gateVerdict": verdict})
json.dump(out, open(os.path.join(run, "summary.json"), "w"), indent=2)
print(f"{'tag':24} {'params':>9} {'pearson':>8} {'mirror':>8} {'MAE':>7} {'hazAcc':>7} {'loss':>7} {'wall':>6}")
for r in rows:
    print(f"{r['tag']:24} {r['parameters']:9d} {r['lifetimePearson']:8.4f} {r['mirroredPearson']:8.4f} {r['lifetimeMAE']:7.2f} {r['hazardAccMean']:7.4f} {r['finalTrainLoss']:7.4f} {r['trainWallSeconds']:6.0f}")
if base:
    print(f"base {out['basePearson']:.4f}  seed spread {out['seedSpread']}  best {out['bestTag']} {out['bestPearson']}  delta {out['deltaBestMinusBase']}  verdict {out['gateVerdict']}")
