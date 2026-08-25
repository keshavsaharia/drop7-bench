#!/usr/bin/env python3
"""NNUE-class student on the existing successor-closed D4 labels: ordering probe.

Experiment EX-20260823-nnue-d4q-ordering-probe-0ca09bb1 under theory
TH-20260823-nnue-class-holds-d4-ordering-c7b397a5.

A LeafNet-shaped student (features from learned-leaf/leaf_features.py, imported,
not copied; EmbeddingBag(8902,64,sum)+bias -> ReLU -> Linear(64,32) -> ReLU ->
Linear(32,1)) is trained on the afterstate rows of
runs/RUN-20260821T085042Z-c5cf0e71/d4q-labels/d4q-labels.tsv and judged on
whether its one-ply chance-averaged ordering of a root's legal siblings agrees
with exact fair D4's ordering.  Every metric is computed by the functions of
approaches/afterstate-learning/distributional-afterstate/d4q.py (imported), so
the numbers are directly comparable with RS-20260821T104500Z-77d21e90.

Per-action value (d4q.py pack_roots / model_q semantics): the D4 value of a
(root, action) is the mean over the five chance strata of
score_delta + value(afterstate); the student replaces value(afterstate) with
its own output, in standardised value units, and the delta is rescaled the
same way, so the constant offset cancels inside a root.

Folds.  The d4q label file carries fold "train" (6,551 roots) and
"calibration" (2,088 roots); the prior experiment used "calibration" as its
validation fold and the separately labelled d4q-labels-gate file (3,030 roots,
fold "heldout") as the held-out panel, with origin-seed half-folds from
corpus-gate/roots.tsv via mix32(origin_seed) & 1.  All three are reused
unchanged.  The held-out panel is read exactly once, after seed selection on
the calibration fold, and then reported for every seed.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import sys
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "learned-leaf"))
sys.path.insert(0, os.path.join(ROOT, "approaches", "afterstate-learning",
                                "distributional-afterstate"))

import leaf_features as lf                                  # noqa: E402
from d4q import (STRATA, VALUE_SCALE, load_labels,          # noqa: E402
                 ranking_metrics_from_q)
from train import MAX_ACTIONS, mix32                        # noqa: E402

EXPECTED_ROWS = 291_890
EXPECTED_ROOTS = 8_639
TARGET_TEMPERATURE = 0.18          # d4-q-clone kTargetTemperature
TIE_TOLERANCE = 1.0e-9             # d4-q-clone kTieTolerance
MSE_WEIGHT = 0.1
SEEDS = [0xA52E01, 0xA52E02, 0xA52E03, 0xA52E04, 0xA52E05]
GATE = {"top1": 0.60, "pairwise": 0.78, "regret": 0.13}


class ProbeNet(nn.Module):
    """LeafNet shape (train_leaf.LeafNet) with a single scalar head."""

    def __init__(self, hidden: int = 64, mid: int = 32):
        super().__init__()
        self.hidden = hidden
        self.mid = mid
        self.ft = nn.EmbeddingBag(lf.FEATURES, hidden, mode="sum")
        nn.init.normal_(self.ft.weight, 0.0, 0.05)
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.l2 = nn.Linear(hidden, mid)
        self.out = nn.Linear(mid, 1)

    def forward(self, index):
        x = F.relu(self.ft(index) + self.ft_bias)
        x = F.relu(self.l2(x))
        return self.out(x)[:, 0]


def board_array(board: str) -> np.ndarray:
    return np.frombuffer(board.encode(), dtype=np.uint8) - ord("0")


def pack_roots(rows):
    """d4q.py pack_roots, with sparse features in place of CNN planes."""
    by_root = {}
    for r in rows:
        by_root.setdefault(r["uid"], []).append(r)
    roots = []
    for uid, group in by_root.items():
        actions = sorted({r["action"] for r in group})
        if len(actions) < 2:
            continue
        a_index = {a: i for i, a in enumerate(actions)}
        n_actions = len(actions)
        boards = np.zeros((MAX_ACTIONS, STRATA, lf.CELLS), dtype=np.uint8)
        nxt = np.ones((MAX_ACTIONS, STRATA), dtype=np.int32)
        mov = np.ones((MAX_ACTIONS, STRATA), dtype=np.int32)
        delta = np.zeros((MAX_ACTIONS, STRATA), dtype=np.float64)
        value = np.zeros((MAX_ACTIONS, STRATA), dtype=np.float64)
        mask = np.zeros((MAX_ACTIONS, STRATA), dtype=bool)
        for r in group:
            i = a_index[r["action"]]
            s = r["stratum"]
            if s >= STRATA:
                continue
            boards[i, s] = board_array(r["board"])
            nxt[i, s] = r["next"]
            mov[i, s] = r["moves"]
            delta[i, s] = r["delta"] / VALUE_SCALE
            value[i, s] = r["value"] / VALUE_SCALE
            mask[i, s] = True
        with np.errstate(invalid="ignore"):
            q_true = ((delta + value) * mask).sum(axis=1) / mask.sum(axis=1)
        feats = lf.build(boards.reshape(-1, lf.CELLS), nxt.reshape(-1),
                         mov.reshape(-1)).reshape(MAX_ACTIONS, STRATA, lf.ACTIVE)
        roots.append({
            "uid": uid, "feats": feats, "delta": delta, "value": value,
            "mask": mask, "q_true": q_true[:n_actions], "n_actions": n_actions,
        })
    return roots


def stack(roots, indices, device):
    feats = torch.from_numpy(np.stack([roots[i]["feats"] for i in indices])
                             .astype(np.int64))
    delta = torch.from_numpy(np.stack([roots[i]["delta"] for i in indices])
                             .astype(np.float32))
    value = torch.from_numpy(np.stack([roots[i]["value"] for i in indices])
                             .astype(np.float32))
    mask = torch.from_numpy(np.stack([roots[i]["mask"] for i in indices]))
    q_true = torch.from_numpy(np.stack(
        [np.pad(roots[i]["q_true"], (0, MAX_ACTIONS - roots[i]["n_actions"]),
                constant_values=0.0) for i in indices]).astype(np.float32))
    n_actions = torch.tensor([roots[i]["n_actions"] for i in indices])
    return (feats.to(device), delta.to(device), value.to(device),
            mask.to(device), q_true.to(device), n_actions.to(device))


def model_q(model, feats, delta, mask, mu, sigma):
    """Per-action score: mean over strata of (delta + V) in standardised units.

    V_std is the model output; the true value in points is mu + sigma * V_std,
    so (delta + value) / sigma = delta / sigma + V_std + mu / sigma and the
    constant mu / sigma cancels within a root (d4q.py model_q semantics).
    """
    g, a, s = mask.shape
    v = model(feats.reshape(g * a * s, lf.ACTIVE)).reshape(g, a, s)
    q = ((v + delta / sigma) * mask).sum(dim=2) / mask.sum(dim=2).clamp(min=1)
    return q, v


def root_losses(q_pred, q_true, n_actions):
    """Listwise CE against softmax(normalised/0.18) + gap-weighted pairwise.

    Mirrors d4-q-clone rootObjective: normalised value = (q - min) / range on
    legal siblings; target = softmax(normalised / T); pair weight
    0.25 + 0.75 * |gap|; pairwise term softplus(-(s_better - s_worse))
    averaged by pair weight within the root.  Both terms averaged over roots.
    """
    g = q_pred.shape[0]
    amask = torch.arange(MAX_ACTIONS, device=q_pred.device).unsqueeze(0) < \
        n_actions.unsqueeze(1)
    neg_inf = torch.finfo(q_pred.dtype).min
    t_masked = torch.where(amask, q_true, torch.full_like(q_true, float("inf")))
    t_min = t_masked.min(dim=1, keepdim=True).values
    t_masked = torch.where(amask, q_true, torch.full_like(q_true, float("-inf")))
    t_max = t_masked.max(dim=1, keepdim=True).values
    denom = (t_max - t_min).clamp(min=1.0e-9)
    norm = torch.where(amask, (q_true - t_min) / denom, torch.zeros_like(q_true))

    target_logits = torch.where(amask, norm / TARGET_TEMPERATURE,
                                torch.full_like(norm, neg_inf))
    target = torch.softmax(target_logits, dim=1)
    pred_logits = torch.where(amask, q_pred, torch.full_like(q_pred, neg_inf))
    log_pred = torch.log_softmax(pred_logits, dim=1)
    listwise = -(target * torch.where(amask, log_pred,
                                      torch.zeros_like(log_pred))).sum(dim=1)

    gap = norm.unsqueeze(2) - norm.unsqueeze(1)              # [g, i, j]
    valid = (gap.abs() > TIE_TOLERANCE) & amask.unsqueeze(2) & amask.unsqueeze(1)
    weight = (0.25 + 0.75 * gap.abs()) * valid
    sign = torch.sign(gap)
    margin = (q_pred.unsqueeze(2) - q_pred.unsqueeze(1)) * sign
    pair = F.softplus(-margin) * weight
    # Each unordered pair appears twice in the [i, j] grid; the ratio is unchanged.
    pairwise = pair.sum(dim=(1, 2)) / weight.sum(dim=(1, 2)).clamp(min=1.0e-12)
    has_pairs = weight.sum(dim=(1, 2)) > 0
    return listwise.mean(), (pairwise * has_pairs).sum() / has_pairs.sum().clamp(min=1)


def evaluate(model, roots, device, mu, sigma, half_of=None, batch=256):
    model.eval()
    results = []
    with torch.no_grad():
        for start in range(0, len(roots), batch):
            idx = np.arange(start, min(start + batch, len(roots)))
            feats, delta, _, mask, _, _ = stack(roots, idx, device)
            q_pred, _ = model_q(model, feats, delta, mask, mu, sigma)
            q_pred = q_pred.cpu().numpy()
            for j, i in enumerate(idx):
                root = roots[i]
                m = ranking_metrics_from_q(q_pred[j], root["q_true"],
                                           root["n_actions"])
                m["half"] = half_of(root["uid"]) if half_of else None
                results.append(m)
    return results


def summarize(subset):
    out = {}
    for key in ("top1", "top2", "pairwise", "regret"):
        vals = [m[key] for m in subset if m[key] is not None]
        out[key] = float(np.mean(vals)) if vals else None
    out["n"] = len(subset)
    return out


def split_halves(results):
    return {
        "pooled": summarize(results),
        "half1": summarize([m for m in results if m["half"] == 0]),
        "half2": summarize([m for m in results if m["half"] == 1]),
    }


def train_seed(seed, train_roots, val_roots, args, device, mu, sigma, log):
    torch.manual_seed(seed)
    np.random.seed(seed & 0xFFFFFFFF)
    rng = np.random.RandomState(seed & 0xFFFFFFFF)
    model = ProbeNet(args.hidden, args.mid).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    started = time.time()
    epochs = []
    nan = False
    for epoch in range(args.epochs):
        model.train()
        order = rng.permutation(len(train_roots))
        totals = {"listwise": 0.0, "pairwise": 0.0, "mse": 0.0, "n": 0}
        for start in range(0, len(order), args.batch_roots):
            idx = order[start:start + args.batch_roots]
            feats, delta, value, mask, q_true, n_actions = stack(
                train_roots, idx, device)
            q_pred, v = model_q(model, feats, delta, mask, mu, sigma)
            listwise, pairwise = root_losses(q_pred, q_true, n_actions)
            v_target = (value - mu) / sigma
            mse = (((v - v_target) ** 2) * mask).sum() / mask.sum().clamp(min=1)
            loss = listwise + pairwise + MSE_WEIGHT * mse
            if not torch.isfinite(loss):
                nan = True
                break
            opt.zero_grad()
            loss.backward()
            opt.step()
            totals["listwise"] += float(listwise.detach()) * len(idx)
            totals["pairwise"] += float(pairwise.detach()) * len(idx)
            totals["mse"] += float(mse.detach()) * len(idx)
            totals["n"] += len(idx)
        if nan:
            log(json.dumps({"seed": f"0x{seed:X}", "epoch": epoch,
                            "event": "non-finite loss; seed invalid"}))
            break
        val = summarize(evaluate(model, val_roots, device, mu, sigma))
        line = {"seed": f"0x{seed:X}", "epoch": epoch,
                "listwise": totals["listwise"] / totals["n"],
                "pairwise": totals["pairwise"] / totals["n"],
                "mse": totals["mse"] / totals["n"],
                "validation": val,
                "elapsed": round(time.time() - started, 2)}
        epochs.append(line)
        log(json.dumps(line))
    return model, epochs, nan, time.time() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--labels", default=os.path.join(
        ROOT, "runs/RUN-20260821T085042Z-c5cf0e71/d4q-labels/d4q-labels.tsv"))
    parser.add_argument("--heldout-labels", default=os.path.join(
        ROOT, "runs/RUN-20260821T085042Z-c5cf0e71/d4q-labels-gate/d4q-labels.tsv"))
    parser.add_argument("--heldout-roots-tsv", default=os.path.join(
        ROOT, "runs/RUN-20260821T085042Z-c5cf0e71/corpus-gate/roots.tsv"))
    parser.add_argument("--out", required=True)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--batch-roots", type=int, default=256)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--mid", type=int, default=32)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--wall-budget", type=float, default=5400.0)
    parser.add_argument("--smoke", action="store_true",
                        help="mechanics check only: stop before the held-out "
                             "panel is opened (writes no metrics.json)")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    torch.set_num_threads(args.threads)
    device = args.device
    started = time.time()
    log_handle = open(os.path.join(args.out, "train.log"), "w")

    def log(text):
        print(text)
        log_handle.write(text + "\n")
        log_handle.flush()

    log(json.dumps({"args": vars(args), "torch": torch.__version__,
                    "threads": torch.get_num_threads(),
                    "openblasThreads": os.environ.get("OPENBLAS_NUM_THREADS")}))

    # --- labels: count check is a preregistered stop condition -------------
    rows = load_labels(args.labels)
    n_roots = len({r["uid"] for r in rows})
    log(f"label rows {len(rows)} roots {n_roots}")
    if len(rows) != EXPECTED_ROWS or n_roots != EXPECTED_ROOTS:
        log(f"ABORT: expected {EXPECTED_ROWS} rows / {EXPECTED_ROOTS} roots")
        raise SystemExit(2)
    folds = {}
    for r in rows:
        folds.setdefault(r["fold"], []).append(r)
    log("folds " + json.dumps({k: {"rows": len(v),
                                   "roots": len({r['uid'] for r in v})}
                               for k, v in folds.items()}))
    if set(folds) != {"train", "calibration"}:
        log("ABORT: unexpected fold set")
        raise SystemExit(2)
    train_roots = pack_roots(folds["train"])
    val_roots = pack_roots(folds["calibration"])
    train_values = np.concatenate([r["value"][r["mask"]] for r in train_roots])
    mu = float(train_values.mean())
    sigma = float(train_values.std())
    log(f"train roots {len(train_roots)} validation roots {len(val_roots)} "
        f"value mean {mu:.5f} std {sigma:.5f} (units of {VALUE_SCALE:.0f} points)")

    model = ProbeNet(args.hidden, args.mid)
    n_params = sum(p.numel() for p in model.parameters())
    log(f"model parameters {n_params} device {device}")

    # --- train every seed; validation only --------------------------------
    per_seed = {}
    models = {}
    for seed in SEEDS:
        if time.time() - started > args.wall_budget:
            log(f"wall budget reached before seed 0x{seed:X}; partial")
            per_seed[f"0x{seed:X}"] = {"status": "not-run (wall budget)"}
            continue
        model, epochs, nan, wall = train_seed(
            seed, train_roots, val_roots, args, device, mu, sigma, log)
        entry = {"status": "invalid (non-finite loss)" if nan else "valid",
                 "epochs": epochs, "trainWallSeconds": round(wall, 2)}
        if not nan:
            final_val = evaluate(model, val_roots, device, mu, sigma)
            entry["validation"] = summarize(final_val)
            path = os.path.join(args.out, f"model-seed{seed:X}.pt")
            torch.save({"model": model.state_dict(), "hidden": args.hidden,
                        "mid": args.mid, "hazardHorizon": 0,
                        "features": lf.FEATURES, "active": lf.ACTIVE,
                        "valueMean": mu, "valueStd": sigma,
                        "valueScale": VALUE_SCALE, "seed": seed,
                        "experimentId":
                        "EX-20260823-nnue-d4q-ordering-probe-0ca09bb1"}, path)
            entry["model"] = os.path.relpath(path, ROOT)
            models[seed] = model
        per_seed[f"0x{seed:X}"] = entry
        log(json.dumps({"seed": f"0x{seed:X}", "final": entry.get("validation"),
                        "status": entry["status"]}))

    valid_seeds = [s for s in SEEDS if s in models]
    if not valid_seeds:
        log("no valid seed; nothing to read on the held-out panel")
        raise SystemExit(3)
    selected = max(valid_seeds,
                   key=lambda s: per_seed[f"0x{s:X}"]["validation"]["top1"])
    log(f"selected seed 0x{selected:X} by validation top-1 "
        f"{per_seed[f'0x{selected:X}']['validation']['top1']:.4f}")

    if args.smoke:
        log("smoke run: stopping before the held-out panel is read")
        log_handle.close()
        return

    # --- held-out: read once, after selection -----------------------------
    heldout_rows = load_labels(args.heldout_labels)
    heldout_roots = pack_roots(heldout_rows)
    origin_of = {}
    with open(args.heldout_roots_tsv) as handle:
        handle.readline()
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3:
                origin_of[parts[0]] = int(parts[2], 16)

    def half_of(uid):
        if uid in origin_of:
            return mix32(origin_of[uid]) & 1
        return mix32(int.from_bytes(uid.encode()[:4].ljust(4, b"\0"),
                                    "little")) & 1

    log(f"held-out rows {len(heldout_rows)} roots {len(heldout_roots)} "
        f"origin-mapped {sum(r['uid'] in origin_of for r in heldout_roots)}")
    for seed in valid_seeds:
        res = evaluate(models[seed], heldout_roots, device, mu, sigma, half_of)
        per_seed[f"0x{seed:X}"]["heldout"] = split_halves(res)
        log(json.dumps({"seed": f"0x{seed:X}",
                        "heldout": per_seed[f"0x{seed:X}"]["heldout"]}))

    sel = per_seed[f"0x{selected:X}"]["heldout"]
    checks = []
    for key, threshold in GATE.items():
        better = (lambda v: v <= threshold) if key == "regret" else \
            (lambda v: v >= threshold)
        h1, h2 = sel["half1"][key], sel["half2"][key]
        checks.append({
            "criterion": f"selected seed held-out {key} "
                         f"{'<=' if key == 'regret' else '>='} {threshold} "
                         f"in each half-fold",
            "passed": bool(better(h1) and better(h2)),
            "observed": f"half1 {h1:.4f}, half2 {h2:.4f}",
        })
    n_valid = len(valid_seeds)
    spread = {k: {"min": min(per_seed[f"0x{s:X}"]["heldout"]["pooled"][k]
                             for s in valid_seeds),
                  "max": max(per_seed[f"0x{s:X}"]["heldout"]["pooled"][k]
                             for s in valid_seeds)}
              for k in ("top1", "top2", "pairwise", "regret")}
    wall = time.time() - started
    report = {
        "experimentId": "EX-20260823-nnue-d4q-ordering-probe-0ca09bb1",
        "theoryId": "TH-20260823-nnue-class-holds-d4-ordering-c7b397a5",
        "labels": os.path.relpath(args.labels, ROOT),
        "heldoutLabels": os.path.relpath(args.heldout_labels, ROOT),
        "labelRows": len(rows), "labelRoots": n_roots,
        "trainRoots": len(train_roots), "validationRoots": len(val_roots),
        "heldoutRoots": len(heldout_roots),
        "modelParameters": n_params,
        "valueMean": mu, "valueStd": sigma, "valueScale": VALUE_SCALE,
        "seedsRun": n_valid, "seedsPlanned": len(SEEDS),
        "selectedSeed": f"0x{selected:X}",
        "selectionRule": "best final-epoch validation (calibration fold) top-1",
        "perSeed": per_seed,
        "heldoutSpreadAcrossSeeds": spread,
        "gate": {"criteria": checks,
                 "passed": all(c["passed"] for c in checks)},
        "comparator": {"afterstateCnnTop1Pooled": 0.3752,
                       "exactD1Top1": 0.486, "exactD2Top1": 0.568,
                       "source": "RS-20260821T104500Z-77d21e90"},
        "wallSeconds": round(wall, 2),
        "withinWallBudget": wall <= args.wall_budget,
        "device": device, "threads": torch.get_num_threads(),
        "torch": torch.__version__,
    }
    with open(os.path.join(args.out, "metrics.json"), "w") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write("\n")
    log(json.dumps({"gate": report["gate"], "wallSeconds": report["wallSeconds"]}))
    log_handle.close()


if __name__ == "__main__":
    main()
