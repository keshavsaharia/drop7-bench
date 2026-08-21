#!/usr/bin/env python3
"""Scale-out stage 1: successor-closed fair-D4 search-value distillation.

Experiment EX-20260821-afterstate-d4q-stage1-40136e9e, under theory
TH-20260821-search-guided-self-play-at-scale-299ed02f.

Trains the action-free afterstate evaluator on the exact values the pinned
fair-D4 reference assigns to every legal sibling's resolved afterstate
(label-d4q.cpp output), then measures held-out agreement between the model's
one-ply chance-averaged action ordering and D4's own ordering on fresh roots.

The model input is strictly public: afterstate board, next disc, moves until
rise. The value target is rescaled by a fixed constant (1e4).
"""

import argparse
import json
import os
import time

import numpy as np
import torch

from train import (AfterstateNet, IN_PLANES, MAX_ACTIONS, N_QUANTILES, mix32)

VALUE_SCALE = 10_000.0
STRATA = 5


def load_labels(path):
    rows = []
    with open(path) as handle:
        handle.readline()
        for line in handle:
            (uid, fold, action, stratum, board, next_disc, moves, terminal,
             delta, value) = line.rstrip("\n").split("\t")
            rows.append({
                "uid": uid, "fold": fold, "action": int(action),
                "stratum": int(stratum), "board": board,
                "next": int(next_disc), "moves": int(moves),
                "terminal": terminal == "1", "delta": float(delta),
                "value": float(value),
            })
    return rows


def encode_planes(board, next_disc, moves):
    planes = np.zeros((IN_PLANES, 7, 7), dtype=np.uint8)
    for c, ch in enumerate(board):
        planes[int(ch), c // 7, c % 7] = 1
    planes[10 + next_disc - 1] = 1
    planes[17 + moves - 1] = 1
    return planes


def pack_roots(rows):
    """Groups rows by root, padding to [G, MAX_ACTIONS, STRATA]."""
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
        planes = np.zeros((MAX_ACTIONS, STRATA, IN_PLANES, 7, 7),
                          dtype=np.uint8)
        delta = np.zeros((MAX_ACTIONS, STRATA), dtype=np.float64)
        value = np.zeros((MAX_ACTIONS, STRATA), dtype=np.float64)
        mask = np.zeros((MAX_ACTIONS, STRATA), dtype=bool)
        for r in group:
            i = a_index[r["action"]]
            s = r["stratum"]
            if s >= STRATA:
                continue
            planes[i, s] = encode_planes(r["board"], r["next"], r["moves"])
            delta[i, s] = r["delta"] / VALUE_SCALE
            value[i, s] = r["value"] / VALUE_SCALE
            mask[i, s] = True
        # True action values: mean over strata of delta + value.
        q_true = ((delta + value) * mask).sum(axis=1) / mask.sum(axis=1)
        roots.append({
            "uid": uid, "planes": planes, "delta": delta, "value": value,
            "mask": mask, "q_true": q_true[:n_actions], "n_actions": n_actions,
        })
    return roots


def model_q(model, planes_t, delta_t, mask_t):
    """Per-action model value: mean over strata of delta + V(afterstate)."""
    g, a, s = mask_t.shape
    out = model(planes_t.reshape(g * a * s, IN_PLANES, 7, 7))
    v = out["quantiles"].mean(dim=-1).reshape(g, a, s)
    q = ((v + delta_t) * mask_t).sum(dim=2) / mask_t.sum(dim=2).clamp(min=1)
    return q, v


def ranking_metrics_from_q(q_pred, q_true, n_actions):
    p = q_pred[:n_actions]
    t = q_true[:n_actions]
    spread = float(t.max() - t.min())
    best = int(t.argmax())
    pick = int(p.argmax())
    pairs = 0
    correct = 0.0
    for i in range(n_actions):
        for j in range(i + 1, n_actions):
            if t[i] == t[j]:
                continue
            pairs += 1
            correct += float((p[i] - p[j]) * (t[i] - t[j]) > 0)
    return {
        "top1": float(pick == best),
        "top2": float(best in np.argsort(-p)[:2]),
        "pairwise": (correct / pairs) if pairs else None,
        "regret": float((t[best] - t[pick]) / spread) if spread > 0 else 0.0,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--train-labels", required=True)
    parser.add_argument("--heldout-labels", required=True)
    parser.add_argument("--heldout-roots-tsv", default=None,
                        help="roots.tsv carrying origin_seed per root for "
                             "origin-hash half-folds")
    parser.add_argument("--out", required=True)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--channels", type=int, default=96)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--roots-per-batch", type=int, default=64)
    parser.add_argument("--max-train-seconds", type=float, default=3 * 3600)
    parser.add_argument("--seed", type=int, default=20260821)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    started = time.time()

    train_rows = load_labels(args.train_labels)
    heldout_rows = load_labels(args.heldout_labels)
    print(f"train label rows {len(train_rows)} held-out rows {len(heldout_rows)}")

    train_roots = pack_roots(train_rows)
    heldout_roots = pack_roots(heldout_rows)

    # Origin-game half-folds: prefer the origin seed from the gate roots.tsv;
    # fall back to a public-state hash only if it is unavailable.
    origin_of = {}
    if args.heldout_roots_tsv:
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
    print(f"train roots {len(train_roots)} held-out roots {len(heldout_roots)} "
          f"origin-mapped {len(origin_of)}")

    model = AfterstateNet(args.channels, args.blocks).to(device)
    print(f"model parameters {sum(p.numel() for p in model.parameters())} "
          f"device {device}")
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    def batch_tensors(roots, indices):
        planes = torch.from_numpy(
            np.stack([roots[i]["planes"] for i in indices]).astype(np.float32))
        delta = torch.from_numpy(
            np.stack([roots[i]["delta"] for i in indices]).astype(np.float32))
        mask = torch.from_numpy(np.stack([roots[i]["mask"] for i in indices]))
        return planes.to(device), delta.to(device), mask.to(device)

    log = []
    for epoch in range(args.epochs):
        model.train()
        order = np.random.permutation(len(train_roots))
        totals = {"mse": 0.0, "rank": 0.0, "n": 0}
        for start in range(0, len(order), args.roots_per_batch):
            idx = order[start:start + args.roots_per_batch]
            planes, delta, mask = batch_tensors(train_roots, idx)
            q_true = torch.from_numpy(np.stack(
                [np.pad(train_roots[i]["q_true"],
                        (0, MAX_ACTIONS - train_roots[i]["n_actions"]),
                        constant_values=0.0)
                 for i in idx]).astype(np.float32)).to(device)
            n_actions = torch.tensor([train_roots[i]["n_actions"]
                                      for i in idx], device=device)
            q_pred, v = model_q(model, planes, delta, mask)
            # Value regression on every labeled afterstate.
            val_target = torch.from_numpy(np.stack(
                [train_roots[i]["value"] for i in idx]).astype(np.float32)
                ).to(device)
            mse = (((v - val_target) ** 2) * mask).sum() / mask.sum().clamp(min=1)
            # Ranking loss over action means within each root.
            amask = torch.arange(MAX_ACTIONS, device=device).unsqueeze(0) < \
                n_actions.unsqueeze(1)
            dq_p = q_pred.unsqueeze(2) - q_pred.unsqueeze(1)
            dq_t = q_true.unsqueeze(2) - q_true.unsqueeze(1)
            sign = torch.sign(dq_t)
            valid = (sign != 0) & amask.unsqueeze(2) & amask.unsqueeze(1)
            pair = torch.nn.functional.softplus(-dq_p * sign)
            l_rank = (pair * valid).sum() / valid.sum().clamp(min=1)
            loss = mse + l_rank
            opt.zero_grad()
            loss.backward()
            opt.step()
            totals["mse"] += float(mse) * len(idx)
            totals["rank"] += float(l_rank) * len(idx)
            totals["n"] += len(idx)
        sched.step()
        line = {"epoch": epoch,
                "mse": totals["mse"] / totals["n"],
                "rank": totals["rank"] / totals["n"],
                "elapsed": time.time() - started}
        log.append(line)
        print(json.dumps(line))
        if time.time() - started > args.max_train_seconds:
            print("training time budget reached")
            break

    torch.save({"model": model.state_dict(), "channels": args.channels,
                "blocks": args.blocks, "quantiles": N_QUANTILES,
                "valueScale": VALUE_SCALE},
               os.path.join(args.out, "model-d4q.pt"))
    with open(os.path.join(args.out, "training-log.json"), "w") as handle:
        json.dump(log, handle, indent=1)

    # Gate: held-out agreement with D4's ordering, per half-fold by root hash.
    model.eval()
    results = []
    with torch.no_grad():
        for start in range(0, len(heldout_roots), 256):
            idx = np.arange(start, min(start + 256, len(heldout_roots)))
            planes, delta, mask = batch_tensors(heldout_roots, idx)
            q_pred, _ = model_q(model, planes, delta, mask)
            q_pred = q_pred.cpu().numpy()
            for j, i in enumerate(idx):
                root = heldout_roots[i]
                m = ranking_metrics_from_q(q_pred[j], root["q_true"],
                                           root["n_actions"])
                m["half"] = half_of(root["uid"])
                results.append(m)

    def summarize(subset):
        out = {}
        for key in ("top1", "top2", "pairwise", "regret"):
            vals = [m[key] for m in subset if m[key] is not None]
            out[key] = float(np.mean(vals)) if vals else None
        out["n"] = len(subset)
        return out

    half1 = [m for m in results if m["half"] == 0]
    half2 = [m for m in results if m["half"] == 1]
    report = {
        "heldoutRoots": len(results),
        "pooled": summarize(results),
        "half1": summarize(half1),
        "half2": summarize(half2),
        "wallSeconds": time.time() - started,
        "device": torch.cuda.get_device_name(0) if device == "cuda" else "cpu",
        "peakGpuBytes": (int(torch.cuda.max_memory_allocated(0))
                         if device == "cuda" else None),
    }
    with open(os.path.join(args.out, "d4q-gate-report.json"), "w") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps(report, indent=1, sort_keys=True))


if __name__ == "__main__":
    main()
