"""Trains the leaf-affordable student ("LeafNet") on the same corpus, the same
targets and the same whole-origin split as the residual CNN in
approaches/lifetime-objective/afterstate-net.

This is not an improvement on that CNN and is not expected to be one.  It is
the largest survival model that fits inside a depth-4 expectimax leaf budget on
this host: the CNN costs 4.12 ms per state and the search evaluates 615,090
leaves per decision at five chance strata, so the CNN is ~2,900x over budget and
LeafNet is the model that actually gets to play.

Architecture (see leaf_features.py for the feature space):

    EmbeddingBag(8902, H, sum) + bias -> ReLU -> Linear(H, M) -> ReLU
        -> Linear(M, 12 hazard logits + 1 log-lifetime + 2 flow)

With H = 64 and M = 32 that is 135 gathered rows of 64 floats plus 2,048 plus
480 multiply-adds, about 11k operations per state.
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
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "afterstate-net"))

import dataset as ds            # noqa: E402
import leaf_features as lf      # noqa: E402

OUTPUTS = ds.HAZARD_HORIZON + 3


class LeafNet(nn.Module):
    def __init__(self, hidden: int = 64, mid: int = 32):
        super().__init__()
        self.hidden = hidden
        self.mid = mid
        self.ft = nn.EmbeddingBag(lf.FEATURES, hidden, mode="sum")
        nn.init.normal_(self.ft.weight, 0.0, 0.05)
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.l2 = nn.Linear(hidden, mid)
        self.out = nn.Linear(mid, OUTPUTS)

    def forward(self, index):
        x = F.relu(self.ft(index) + self.ft_bias)
        x = F.relu(self.l2(x))
        y = self.out(x)
        return y[:, :ds.HAZARD_HORIZON], y[:, ds.HAZARD_HORIZON], y[:, ds.HAZARD_HORIZON + 1:]


def features_for(records: np.ndarray, chunk: int = 400_000) -> np.ndarray:
    out = np.empty((len(records), lf.ACTIVE), dtype=np.uint16)
    for start in range(0, len(records), chunk):
        stop = min(start + chunk, len(records))
        block = records[start:stop]
        out[start:stop] = lf.build(np.ascontiguousarray(block["board"]),
                                   np.asarray(block["nextDisc"]),
                                   np.asarray(block["movesRemaining"]))
    return out


def evaluate(model, index, targets, device, batch, mirror=None) -> dict:
    model.eval()
    total = len(index)
    predictions = np.empty(total, dtype=np.float32)
    hazard_correct = np.zeros(ds.HAZARD_HORIZON)
    hazard_count = np.zeros(ds.HAZARD_HORIZON)
    with torch.no_grad():
        for start in range(0, total, batch):
            stop = min(start + batch, total)
            idx = torch.from_numpy(index[start:stop].astype(np.int64)).to(device)
            if mirror is not None:
                idx = mirror[idx]
            hz, lt, _ = model(idx)
            predictions[start:stop] = lt.float().cpu().numpy()
            p = torch.sigmoid(hz).cpu().numpy()
            y = targets.hazard[start:stop]
            m = targets.hazard_mask[start:stop]
            hazard_correct += (((p > 0.5) == (y > 0.5)) * m).sum(axis=0)
            hazard_count += m.sum(axis=0)
    actual = targets.log_moves
    return {
        "lifetimePearson": float(np.corrcoef(predictions, actual)[0, 1]),
        "lifetimeMeanAbsoluteErrorMoves":
            float(np.abs(np.expm1(predictions) - np.expm1(actual)).mean()),
        "hazardAccuracyByRise": (hazard_correct / np.maximum(hazard_count, 1)).tolist(),
        "examples": int(total),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--states", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--mid", type=int, default=32)
    parser.add_argument("--batch", type=int, default=8192)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--lr", type=float, default=3e-3)
    parser.add_argument("--seed", type=int, default=0xA52A)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--include-explored", action="store_true")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed & 0xFFFFFFFF)
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")

    records = ds.load_states(args.states)
    if not args.include_explored:
        records = records[np.asarray(records["explored"]) == 0]
    # Identical split seed to afterstate-net/dataset.py, so the student's
    # held-out set is the CNN's held-out set and the two numbers compare.
    train_mask, val_mask, test_mask = ds.split_by_origin(records)
    print(f"records {len(records)} train {int(train_mask.sum())} "
          f"val {int(val_mask.sum())} test {int(test_mask.sum())}", flush=True)

    def prepare(mask):
        subset = np.asarray(records[mask])
        return features_for(subset), ds.build_targets(subset)

    train_x, train_y = prepare(train_mask)
    val_x, val_y = prepare(val_mask)
    test_x, test_y = prepare(test_mask)

    mirror = torch.from_numpy(lf.mirror_table().astype(np.int64)).to(device)
    model = LeafNet(args.hidden, args.mid).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-5)
    steps = max(1, len(train_x) // args.batch) * args.epochs
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=args.lr, total_steps=steps)

    history = []
    for epoch in range(args.epochs):
        model.train()
        order = np.random.permutation(len(train_x))
        started = time.time()
        running, batches = 0.0, 0
        for start in range(0, len(order) - args.batch + 1, args.batch):
            rows = np.sort(order[start:start + args.batch])
            idx = torch.from_numpy(train_x[rows].astype(np.int64)).to(device)
            if np.random.rand() < 0.5:
                idx = mirror[idx]
            hz_target = torch.from_numpy(train_y.hazard[rows]).to(device)
            hz_mask = torch.from_numpy(train_y.hazard_mask[rows]).to(device)
            lt_target = torch.from_numpy(train_y.log_moves[rows]).to(device)
            flow_target = torch.from_numpy(
                np.stack([train_y.clears[rows], train_y.reveals[rows]], axis=1)).to(device)

            hz, lt, flow = model(idx)
            hazard_loss = (F.binary_cross_entropy_with_logits(hz, hz_target, reduction="none")
                           * hz_mask).sum() / hz_mask.sum().clamp(min=1.0)
            loss = hazard_loss + 0.5 * F.smooth_l1_loss(lt, lt_target) \
                   + 0.25 * F.smooth_l1_loss(flow, flow_target)
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()
            schedule.step()
            running += float(loss.detach())
            batches += 1
        metrics = evaluate(model, val_x, val_y, device, args.batch)
        row = {"epoch": epoch, "trainLoss": running / max(batches, 1),
               "wallSeconds": time.time() - started, **metrics}
        history.append(row)
        print(json.dumps({k: row[k] for k in
                          ("epoch", "trainLoss", "lifetimePearson",
                           "lifetimeMeanAbsoluteErrorMoves", "wallSeconds")}), flush=True)

    final = evaluate(model, test_x, test_y, device, args.batch)
    mirrored = evaluate(model, test_x, test_y, device, args.batch, mirror=mirror)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.save({"model": {k: v.cpu() for k, v in model.state_dict().items()},
                "hidden": args.hidden, "mid": args.mid,
                "features": lf.FEATURES, "active": lf.ACTIVE,
                "hazardHorizon": ds.HAZARD_HORIZON}, args.out + ".pt")
    with open(args.out + ".json", "w") as handle:
        json.dump({"args": vars(args), "history": history, "heldOut": final,
                   "heldOutMirrored": mirrored, "device": str(device),
                   "parameters": sum(p.numel() for p in model.parameters())},
                  handle, indent=2)
    print(json.dumps({"heldOut": final, "heldOutMirrored": mirrored}, indent=2))


if __name__ == "__main__":
    main()
