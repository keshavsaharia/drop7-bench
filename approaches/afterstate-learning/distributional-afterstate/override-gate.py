#!/usr/bin/env python3
"""Offline gate for the calibrated top-two near-tie override of fair D4.

Experiment EX-20260820-d4-toptwo-override-gate-0bdb39a1. Reuses the frozen
iteration-3 afterstate model. At roots where D4's top-two Q gap is <= 500,
override to D4's second action iff the model's paired 256-scenario advantage
has a positive 95% bootstrap lower bound. Everything here is a deterministic
function of public corpus rows, the frozen model bytes, and the frozen D4
comparator labels. No game seed is read by the policy logic.
"""

import argparse
import json
import os
import time

import numpy as np
import torch

from train import (AfterstateNet, batched_values, mix32, root_uid, spearman)

NEAR_TIE_EPSILON = 500.0     # fixed from already-read development D4 labels
BOOTSTRAP_RESAMPLES = 1000
BOOTSTRAP_DOMAIN = 0x4F565252  # "OVRR"
MIN_OVERRIDE_RATE = 0.05
REGRET_MARGIN = 0.01


def bootstrap_lower_bound(diffs, root_hash):
    """One-sided 95% paired bootstrap lower bound on the mean difference."""
    rng = np.random.RandomState(mix32(root_hash ^ BOOTSTRAP_DOMAIN))
    n = len(diffs)
    idx = rng.randint(0, n, size=(BOOTSTRAP_RESAMPLES, n))
    means = diffs[idx].mean(axis=1)
    return float(np.percentile(means, 5.0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--heldout-corpus", required=True)
    parser.add_argument("--comparator-labels", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False
    torch.manual_seed(20260820)
    np.random.seed(20260820)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    started = time.time()

    ckpt = torch.load(args.model, map_location="cpu", weights_only=True)
    model = AfterstateNet(ckpt["channels"], ckpt["blocks"])
    model.load_state_dict(ckpt["model"])
    model = model.to(device).eval()

    with open(args.heldout_corpus) as handle:
        rows = [json.loads(line) for line in handle]
    comparator = {}
    with open(args.comparator_labels) as handle:
        handle.readline()
        for line in handle:
            uid, fold, action, d4, d1 = line.rstrip("\n").split("\t")
            comparator.setdefault(uid, {"d4": {}, "d1": {}})
            comparator[uid]["d4"][int(action)] = float(d4)
            comparator[uid]["d1"][int(action)] = float(d1)

    by_root = {}
    for r in rows:
        by_root.setdefault(root_uid(r), []).append(r)

    values, lo, hi = batched_values(model, rows, device)
    for r, v, l, h in zip(rows, values, lo, hi):
        r["_v"] = float(v)
        r["_lo"] = float(l)
        r["_hi"] = float(h)
    coverage = float(np.mean([
        r["_lo"] <= r["scoreGained"] / 10_000.0 <= r["_hi"] for r in rows]))

    eligible = []
    all_roots = []
    for uid, group in by_root.items():
        if uid not in comparator:
            continue
        d4 = comparator[uid]["d4"]
        finite = {a: v for a, v in d4.items() if np.isfinite(v)}
        if len(finite) < 2:
            continue
        ordered = sorted(finite, key=lambda a: -finite[a])
        a1, a2 = ordered[0], ordered[1]
        gap = finite[a1] - finite[a2]

        model_actions = {}
        target_actions = {}
        half_targets = {"a": {}, "b": {}}
        scenario_values = {}
        max_scenario = max(r["scenario"] for r in group) + 1
        for r in group:
            model_actions.setdefault(r["action"], []).append(r["_v"])
            target_actions.setdefault(r["action"], []).append(r["scoreGained"])
            scenario_values[(r["action"], r["scenario"])] = r["_v"]
            half = "a" if r["scenario"] < max_scenario // 2 else "b"
            half_targets[half].setdefault(r["action"], []).append(
                r["scoreGained"])
        target_mean = {a: float(np.mean(v)) for a, v in target_actions.items()}
        spread = max(target_mean.values()) - min(target_mean.values())

        record = {
            "uid": uid,
            "half": mix32(int(group[0]["originSeed"], 16)) & 1,
            "a1": a1,
            "a2": a2,
            "gap": gap,
            "eligible": gap <= NEAR_TIE_EPSILON,
            "target_mean": target_mean,
            "spread": spread,
            "decisive": spread > 20_000,
        }
        if record["eligible"]:
            diffs = np.array([
                scenario_values[(a2, k)] - scenario_values[(a1, k)]
                for k in range(max_scenario)
                if (a1, k) in scenario_values and (a2, k) in scenario_values])
            root_hash = mix32(int(group[0]["originSeed"], 16) ^ len(group))
            record["advantage_mean"] = float(diffs.mean())
            record["advantage_lb"] = bootstrap_lower_bound(diffs, root_hash)
            record["override"] = record["advantage_lb"] > 0.0
            eligible.append(record)
        all_roots.append(record)

    def policy_regret(rec, choice):
        tm = rec["target_mean"]
        if rec["spread"] <= 0:
            return 0.0
        return (max(tm.values()) - tm[choice]) / rec["spread"]

    def summarize_eligible(subset):
        d4_regrets = [policy_regret(r, r["a1"]) for r in subset]
        ov_regrets = [
            policy_regret(r, r["a2"] if r["override"] else r["a1"])
            for r in subset]
        return {
            "n": len(subset),
            "d4Regret": float(np.mean(d4_regrets)) if subset else None,
            "overrideRegret": float(np.mean(ov_regrets)) if subset else None,
            "overrideRate": float(np.mean([r["override"] for r in subset]))
            if subset else None,
        }

    halves = {0: [r for r in eligible if r["half"] == 0],
              1: [r for r in eligible if r["half"] == 1]}

    # Whole-set comparison: override policy follows D4 except where it fires.
    def whole_set(subset):
        d4_regrets = [policy_regret(r, r["a1"]) for r in subset]
        ov_regrets = [
            policy_regret(r, r["a2"] if r.get("override") else r["a1"])
            for r in subset]
        return {
            "n": len(subset),
            "d4Regret": float(np.mean(d4_regrets)),
            "overrideRegret": float(np.mean(ov_regrets)),
        }

    # Label stability on decisive roots (scenario-half Spearman).
    stability = []
    for uid, group in by_root.items():
        target_actions = {}
        half_targets = {"a": {}, "b": {}}
        max_scenario = max(r["scenario"] for r in group) + 1
        for r in group:
            half = "a" if r["scenario"] < max_scenario // 2 else "b"
            half_targets[half].setdefault(r["action"], []).append(
                r["scoreGained"])
        means = {h: {a: float(np.mean(v)) for a, v in acts.items()}
                 for h, acts in half_targets.items()}
        full = {a: float(np.mean(
            half_targets["a"].get(a, []) + half_targets["b"].get(a, [])))
            for a in means["a"]}
        if len(full) < 2:
            continue
        spread = max(full.values()) - min(full.values())
        if spread <= 20_000 or set(means["a"]) != set(means["b"]):
            continue
        actions = sorted(means["a"])
        rho = spearman([means["a"][a] for a in actions],
                       [means["b"][a] for a in actions])
        if rho is not None:
            stability.append(rho)

    report = {
        "roots": len(all_roots),
        "eligibleRoots": len(eligible),
        "nearTieRate": len(eligible) / max(len(all_roots), 1),
        "eligiblePooled": summarize_eligible(eligible),
        "eligibleHalf1": summarize_eligible(halves[0]),
        "eligibleHalf2": summarize_eligible(halves[1]),
        "eligibleDecisive": summarize_eligible(
            [r for r in eligible if r["decisive"]]),
        "wholeSet": whole_set(all_roots),
        "labelStabilityDecisiveSpearman": (
            float(np.mean(stability)) if stability else None),
        "quantileIntervalCoverage": coverage,
        "nearTieEpsilon": NEAR_TIE_EPSILON,
        "bootstrapResamples": BOOTSTRAP_RESAMPLES,
        "device": torch.cuda.get_device_name(0) if device == "cuda" else "cpu",
        "wallSeconds": time.time() - started,
    }
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "override-gate-report.json")
    with open(path, "w") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps(report, indent=1, sort_keys=True))


if __name__ == "__main__":
    main()
