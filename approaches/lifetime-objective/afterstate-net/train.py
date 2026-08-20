"""Trains a public-state survival evaluator for Drop7 Hardcore.

WHAT THIS PREDICTS, AND WHY

Score in five-move Hardcore is ~94% flat 17,000-point row-rise bonus and
correlates with lifetime at r = 0.9995 over 64 fair-D4 games
(docs/exploratory/finding-01-score-is-survival.md).  Predicting score is
therefore predicting survival through a 17,000-point quantiser with a heavy
right tail.  This model predicts survival directly:

  * a HAZARD head: P(the game survives k more row rises), k = 1..12.  This is a
    calibrated, bounded, per-move target with an exact label from any completed
    game, and it is the quantity a policy actually needs.
  * a LIFETIME head: log1p(moves remaining), as a scalar summary.
  * two FLOW heads: numbered clears and covered reveals produced by the move.
    Steady-state survival requires >= 2.400 clears and >= 1.400 reveals per move
    (12 discs and 7 covered discs enter per five-move cycle onto 49 cells); fair
    D4 sustains 1.973 and 1.090.  These heads give the network the dense,
    mechanistic signal that explains why a position dies.

WHAT THIS IS NOT

It is not a policy head over columns.  Every learned ranker in this repository
that conditioned on action identity failed to rank unplayed legal siblings, the
documented dominant failure mode.  A state-only evaluator applied to each legal
successor cannot use action identity as a shortcut, because it never sees one.
Deployment scores every legal column with the same function, inside the existing
audited chance-averaging search.
"""

from __future__ import annotations

# OpenBLAS 0.3.34 on this Zen 5 host dispatches an Intel SkylakeX SGEMM kernel and
# silently corrupts float32 matmul with >= 4 threads: ~0.1-1% of elements wrong by
# O(10), differently on each run.  See docs/exploratory/gpu-02-openblas-sgemm-race.md.
# Pinned before numpy is imported, because the thread count is read at load time.
import os
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

import dataset as ds


def norm(channels: int) -> nn.Module:
    """GroupNorm, not BatchNorm.

    The bundled MIOpen in torch 2.13.0+rocm7.1 selects a GFX9-only solver for the
    BatchNorm *training* kernel and emits inline asm the gfx1151 assembler
    rejects, so any training step through a BatchNorm layer fails. Inference is
    unaffected, which makes it easy to miss. GroupNorm is a native PyTorch kernel
    and never enters MIOpen. See docs/exploratory/gpu-01-rocm-enablement.md.

    GroupNorm is also the better choice here on its own merits: it is
    batch-size independent, so the evaluator behaves identically when it is
    later called on the 7 legal successors of a single root inside the search.
    """
    return nn.GroupNorm(min(32, channels), channels)


class ResidualBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.a = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.na = norm(channels)
        self.b = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.nb = norm(channels)

    def forward(self, x):
        y = F.relu(self.na(self.a(x)))
        y = self.nb(self.b(y))
        return F.relu(x + y)


class SurvivalNet(nn.Module):
    """A small residual CNN over the 7x7 public board.

    The board is tiny, so depth is cheap and width is the useful axis.  Padding
    is zero rather than circular: the wall is a real feature in Drop7, since a
    column at the edge has one fewer neighbour for horizontal runs.
    """

    def __init__(self, channels: int = 128, blocks: int = 6):
        super().__init__()
        self.stem = nn.Sequential(
            nn.Conv2d(ds.PLANES, channels, 3, padding=1, bias=False),
            norm(channels),
            nn.ReLU(inplace=True),
        )
        self.tower = nn.Sequential(*[ResidualBlock(channels) for _ in range(blocks)])
        self.pool = nn.Sequential(
            nn.Conv2d(channels, 32, 1, bias=False),
            norm(32),
            nn.ReLU(inplace=True),
            nn.Flatten(),
        )
        body = 32 * ds.CELL_COUNT
        self.hazard = nn.Sequential(nn.Linear(body, 256), nn.ReLU(inplace=True),
                                    nn.Linear(256, ds.HAZARD_HORIZON))
        self.lifetime = nn.Sequential(nn.Linear(body, 256), nn.ReLU(inplace=True),
                                      nn.Linear(256, 1))
        self.flow = nn.Sequential(nn.Linear(body, 256), nn.ReLU(inplace=True),
                                  nn.Linear(256, 2))

    def forward(self, x):
        h = self.pool(self.tower(self.stem(x)))
        return self.hazard(h), self.lifetime(h).squeeze(-1), self.flow(h)


def evaluate(model, loader_arrays, device, batch: int) -> dict:
    encoded, targets = loader_arrays
    model.eval()
    total = len(encoded)
    hazard_correct = np.zeros(ds.HAZARD_HORIZON)
    hazard_count = np.zeros(ds.HAZARD_HORIZON)
    hazard_prob_sum = np.zeros(ds.HAZARD_HORIZON)
    hazard_true_sum = np.zeros(ds.HAZARD_HORIZON)
    abs_error = 0.0
    predictions, actuals = [], []
    with torch.no_grad():
        for start in range(0, total, batch):
            stop = min(start + batch, total)
            x = torch.from_numpy(encoded[start:stop]).to(device, non_blocking=True)
            hz, lt, _ = model(x)
            p = torch.sigmoid(hz).cpu().numpy()
            y = targets.hazard[start:stop]
            m = targets.hazard_mask[start:stop]
            hazard_correct += (((p > 0.5) == (y > 0.5)) * m).sum(axis=0)
            hazard_count += m.sum(axis=0)
            hazard_prob_sum += (p * m).sum(axis=0)
            hazard_true_sum += (y * m).sum(axis=0)
            lt_np = lt.cpu().numpy()
            abs_error += float(np.abs(np.expm1(lt_np) - np.expm1(targets.log_moves[start:stop])).sum())
            predictions.append(lt_np)
            actuals.append(targets.log_moves[start:stop])
    predictions = np.concatenate(predictions)
    actuals = np.concatenate(actuals)
    correlation = float(np.corrcoef(predictions, actuals)[0, 1])
    return {
        "hazardAccuracyByRise": (hazard_correct / np.maximum(hazard_count, 1)).tolist(),
        "hazardCalibrationGap": (np.abs(hazard_prob_sum - hazard_true_sum)
                                 / np.maximum(hazard_count, 1)).tolist(),
        "lifetimeMeanAbsoluteErrorMoves": abs_error / total,
        "lifetimePearson": correlation,
        "examples": int(total),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--states", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--channels", type=int, default=128)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--batch", type=int, default=1024)
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--lr", type=float, default=2e-3)
    parser.add_argument("--seed", type=int, default=0x5EED)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--include-explored", action="store_true",
                        help="keep epsilon-deviation moves (they widen action coverage)")
    parser.add_argument("--order", default="shuffled",
                        choices=["shuffled", "hard-first", "easy-first", "long-first", "short-first"],
                        help="curriculum ordering arm; 'shuffled' is the control")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed & 0xFFFFFFFF)
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")

    records = ds.load_states(args.states)
    if not args.include_explored:
        records = records[np.asarray(records["explored"]) == 0]
    train_mask, val_mask, test_mask = ds.split_by_origin(records)
    print(f"records {len(records)}  train {int(train_mask.sum())} "
          f"val {int(val_mask.sum())} test {int(test_mask.sum())}", flush=True)

    def prepare(mask):
        subset = np.asarray(records[mask])
        return ds.encode(subset), ds.build_targets(subset)

    train_x, train_y = prepare(train_mask)
    val_pair = prepare(val_mask)
    test_pair = prepare(test_mask)

    model = SurvivalNet(args.channels, args.blocks).to(device).to(memory_format=torch.channels_last)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    steps = max(1, (len(train_x) // args.batch)) * args.epochs
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=args.lr, total_steps=steps)

    # Curriculum ordering is an explicit, ablatable arm rather than an implicit
    # choice.  No trainer in this repository has ever varied it, so 'shuffled'
    # is the control and every other value must beat it to mean anything.
    lifetime = np.expm1(train_y.log_moves)
    if args.order == "hard-first":
        order_key = lifetime            # short-lived positions are the hard ones
    elif args.order == "easy-first":
        order_key = -lifetime
    elif args.order == "long-first":
        order_key = -lifetime
    elif args.order == "short-first":
        order_key = lifetime
    else:
        order_key = None

    history = []
    for epoch in range(args.epochs):
        model.train()
        if order_key is None:
            index = np.random.permutation(len(train_x))
        else:
            # Anneal from the ordered curriculum toward uniform shuffling so the
            # final epochs see the true data distribution.
            blend = (epoch + 1) / args.epochs
            noise = np.random.rand(len(train_x)) * blend * (order_key.max() - order_key.min() + 1e-6)
            index = np.argsort(order_key + noise)
        started = time.time()
        running = 0.0
        batches = 0
        for start in range(0, len(index) - args.batch + 1, args.batch):
            rows = index[start:start + args.batch]
            x = torch.from_numpy(train_x[rows]).to(device, non_blocking=True).to(memory_format=torch.channels_last)
            if np.random.rand() < 0.5:                      # reflection augmentation
                x = torch.flip(x, dims=[3])
            hz_target = torch.from_numpy(train_y.hazard[rows]).to(device)
            hz_mask = torch.from_numpy(train_y.hazard_mask[rows]).to(device)
            lt_target = torch.from_numpy(train_y.log_moves[rows]).to(device)
            flow_target = torch.from_numpy(
                np.stack([train_y.clears[rows], train_y.reveals[rows]], axis=1)).to(device)

            hz, lt, flow = model(x)
            hazard_loss = (F.binary_cross_entropy_with_logits(hz, hz_target, reduction="none")
                           * hz_mask).sum() / hz_mask.sum().clamp(min=1.0)
            lifetime_loss = F.smooth_l1_loss(lt, lt_target)
            flow_loss = F.smooth_l1_loss(flow, flow_target)
            loss = hazard_loss + 0.5 * lifetime_loss + 0.25 * flow_loss

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()
            schedule.step()
            running += float(loss.detach())
            batches += 1

        metrics = evaluate(model, val_pair, device, args.batch)
        elapsed = time.time() - started
        row = {"epoch": epoch, "trainLoss": running / max(batches, 1),
               "wallSeconds": elapsed,
               "examplesPerSecond": batches * args.batch / max(elapsed, 1e-9),
               **metrics}
        history.append(row)
        print(json.dumps({k: row[k] for k in
                          ("epoch", "trainLoss", "lifetimePearson",
                           "lifetimeMeanAbsoluteErrorMoves", "examplesPerSecond")}), flush=True)

    final = evaluate(model, test_pair, device, args.batch)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.save({"model": model.state_dict(),
                "channels": args.channels, "blocks": args.blocks,
                "planes": ds.PLANES, "hazardHorizon": ds.HAZARD_HORIZON},
               args.out + ".pt")
    with open(args.out + ".json", "w") as handle:
        json.dump({"args": vars(args), "history": history, "heldOut": final,
                   "device": str(device),
                   "parameters": sum(p.numel() for p in model.parameters())},
                  handle, indent=2)
    print(json.dumps(final, indent=2))


if __name__ == "__main__":
    main()
