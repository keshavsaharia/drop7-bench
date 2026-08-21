"""Trains a state-only afterstate evaluator against the fair planner's
per-sibling values, with a WITHIN-ROOT listwise loss.

WHAT IS BEING FITTED
--------------------
    f(afterstate)  ~=  E[ discs the planner clears over the rest of its window
                          | this public afterstate ]

and the search's score for column c is

    s_c  =  immediate_c  +  f(afterstate_c)

where `immediate_c` is the discs that move itself clears - a quantity the search
observes directly at its chance node, and one no state-only evaluator can
represent, which is why it is supplied rather than learned.

WHY LISTWISE AND NOT REGRESSION
-------------------------------
`docs/exploratory/audit-05-optimistic-curriculum.md` records the repository's
single most repeated lesson: low value error on visited states did not once
imply good root-action ranking.  Experiment 12 (counterfactual-successor NNUE)
had every legal sibling labelled and a global Spearman of 0.839, and still chose
the right column 15.4% of the time.  Experiment 14 (D4-Q clone) had full root-Q
vectors and a listwise+pairwise loss and went from 0.765 train to 0.247 held
out.  So the loss here is a within-root softmax cross-entropy against the
planner's own value vector plus an explicit pairwise term, with the absolute
regression kept only as a weak scale anchor.

WHY A STATE-ONLY EVALUATOR AND NOT A POLICY HEAD
------------------------------------------------
Every learned ranker in this repository that conditioned on action identity
failed to rank unplayed legal siblings.  A function of the successor state
cannot use action identity as a shortcut because it never sees one.

TWO ARCHITECTURES, ON PURPOSE
-----------------------------
  * `--arch leaf`  the NNUE-shaped model of
    `approaches/lifetime-objective/learned-leaf/leaf_features.py`, ~1.3 us per
    state, the only size that fits inside a depth-4 expectimax leaf on this host
    (615,090 leaves per decision at five chance strata, 2,271,280 at seven).
  * `--arch cnn`   the residual CNN of
    `approaches/lifetime-objective/afterstate-net/`, ~4.1 ms per state and
    therefore not deployable at the leaf.  It is trained anyway, because it
    separates two very different negative results: "the target is not learnable
    from public state" from "the target is learnable but not inside the leaf
    budget".
"""

from __future__ import annotations

import os

# OpenBLAS 0.3.34 on this host silently corrupts float32 matmul at >= 4 threads
# (docs/exploratory/gpu-02-openblas-sgemm-race.md).  Pinned before numpy loads.
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
# `learned-leaf` is on the path only for `leaf_features`, which is reused rather
# than reimplemented.  HERE is inserted last so it wins: `afterstate-net` also
# ships a module called `dataset`.
sys.path.insert(0, os.path.join(HERE, "..", "learned-leaf"))
sys.path.insert(0, HERE)

import dataset as pd            # noqa: E402  (planner-distill dataset)
import leaf_features as lf      # noqa: E402  (reused, not reimplemented)

PLANES = 18


# ---------------------------------------------------------------------------
# Encodings
# ---------------------------------------------------------------------------

def leaf_index(board: np.ndarray, next_disc: np.ndarray,
               moves_remaining: np.ndarray, chunk: int = 400_000) -> np.ndarray:
    out = np.empty((len(board), lf.ACTIVE), dtype=np.uint16)
    for start in range(0, len(board), chunk):
        stop = min(start + chunk, len(board))
        out[start:stop] = lf.build(np.ascontiguousarray(board[start:stop]),
                                   next_disc[start:stop],
                                   moves_remaining[start:stop])
    return out


def plane_encode(board: np.ndarray, next_disc: np.ndarray,
                 moves_remaining: np.ndarray) -> np.ndarray:
    """The 18-plane encoding of `afterstate-net/dataset.py`, unchanged."""
    count = len(board)
    grid = np.ascontiguousarray(board).reshape(count, 7, 7)
    out = np.zeros((count, PLANES, 7, 7), dtype=np.float32)
    for value in range(1, 8):
        out[:, value - 1] = grid == value
    out[:, 7] = grid == pd.SOLID
    out[:, 8] = grid == pd.CRACKED
    out[:, 9] = grid == pd.EMPTY
    nxt = np.asarray(next_disc, dtype=np.int64)
    for value in range(1, 8):
        out[:, 9 + value] = (nxt == value)[:, None, None]
    out[:, 17] = ((np.asarray(moves_remaining, dtype=np.float32) - 1.0) / 4.0)[:, None, None]
    return out


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------

class LeafStudent(nn.Module):
    """EmbeddingBag(8902, H, sum) -> ReLU -> Linear(H, M) -> ReLU -> Linear(M, 2).

    Head 0 is the planner residual, head 1 is an auxiliary log remaining-lifetime
    head kept at low weight; it costs nothing at inference and gives the network
    the survival signal that every strong head in this repository has had.
    """

    def __init__(self, hidden: int = 128, mid: int = 64):
        super().__init__()
        self.hidden, self.mid = hidden, mid
        self.ft = nn.EmbeddingBag(lf.FEATURES, hidden, mode="sum")
        nn.init.normal_(self.ft.weight, 0.0, 0.05)
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.l2 = nn.Linear(hidden, mid)
        self.out = nn.Linear(mid, 2)

    def forward(self, index):
        x = F.relu(self.ft(index) + self.ft_bias)
        x = F.relu(self.l2(x))
        y = self.out(x)
        return y[:, 0], y[:, 1]


def group_norm(channels: int) -> nn.Module:
    # GroupNorm, never BatchNorm: the bundled MIOpen emits GFX9-only asm for the
    # BatchNorm *training* kernel on gfx1151 and fails only in training mode.
    return nn.GroupNorm(min(32, channels), channels)


class ResidualBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.a = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.na = group_norm(channels)
        self.b = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.nb = group_norm(channels)

    def forward(self, x):
        y = F.relu(self.na(self.a(x)))
        y = self.nb(self.b(y))
        return F.relu(x + y)


class CnnStudent(nn.Module):
    def __init__(self, channels: int = 128, blocks: int = 6):
        super().__init__()
        self.stem = nn.Sequential(
            nn.Conv2d(PLANES, channels, 3, padding=1, bias=False),
            group_norm(channels), nn.ReLU(inplace=True))
        self.tower = nn.Sequential(*[ResidualBlock(channels) for _ in range(blocks)])
        self.pool = nn.Sequential(
            nn.Conv2d(channels, 32, 1, bias=False), group_norm(32),
            nn.ReLU(inplace=True), nn.Flatten())
        body = 32 * 49
        self.head = nn.Sequential(nn.Linear(body, 256), nn.ReLU(inplace=True),
                                  nn.Linear(256, 2))

    def forward(self, x):
        y = self.head(self.pool(self.tower(self.stem(x))))
        return y[:, 0], y[:, 1]

    def load_survival_backbone(self, path: str) -> int:
        """Warm-starts stem/tower/pool from `runs/RUN-A51D-net/*.pt`.

        That checkpoint predicts remaining lifetime with held-out Pearson 0.865,
        so its trunk already encodes the survival structure of a Drop7 board.
        Only the heads differ.  Reported, never assumed: the number of tensors
        actually adopted is returned and written into the run record.
        """
        blob = torch.load(path, map_location="cpu", weights_only=False)
        source = blob.get("model", blob)
        mine = self.state_dict()
        adopted = {k: v for k, v in source.items()
                   if k in mine and mine[k].shape == v.shape
                   and not k.startswith("head")}
        mine.update(adopted)
        self.load_state_dict(mine)
        return len(adopted)


# ---------------------------------------------------------------------------
# Within-root losses
# ---------------------------------------------------------------------------

def scatter_log_softmax(score: torch.Tensor, root: torch.Tensor,
                        root_count: int) -> torch.Tensor:
    """log softmax of `score` within each root group."""
    peak = torch.full((root_count,), -1e30, device=score.device, dtype=score.dtype)
    peak = peak.scatter_reduce(0, root, score, reduce="amax", include_self=True)
    shifted = score - peak[root]
    total = torch.zeros(root_count, device=score.device, dtype=score.dtype)
    total = total.index_add(0, root, torch.exp(shifted))
    return shifted - torch.log(total[root] + 1e-30)


def listwise_loss(score: torch.Tensor, target: torch.Tensor, root: torch.Tensor,
                  root_count: int, temperature: float) -> torch.Tensor:
    log_p = scatter_log_softmax(score / temperature, root, root_count)
    with torch.no_grad():
        q = torch.exp(scatter_log_softmax(target / temperature, root, root_count))
    per_pair = -(q * log_p)
    total = torch.zeros(root_count, device=score.device, dtype=score.dtype)
    total = total.index_add(0, root, per_pair)
    return total.mean()


def pairwise_loss(score: torch.Tensor, target: torch.Tensor, root: torch.Tensor,
                  offsets: torch.Tensor, pairs: torch.Tensor,
                  margin_scale: float) -> torch.Tensor:
    """Margin ranking over explicitly enumerated within-root pairs.

    The margin is proportional to the teacher's own value gap, so pairs the
    teacher barely separates are barely penalised.  That matters here: the
    within-root spread is small relative to the between-root spread, and a plain
    unweighted pairwise loss spends its capacity on ties.
    """
    left, right = pairs[:, 0], pairs[:, 1]
    gap = target[left] - target[right]
    predicted = score[left] - score[right]
    return F.relu(margin_scale * gap - predicted).mean()


def build_pairs(root: np.ndarray, target: np.ndarray,
                min_gap: float, rng: np.random.Generator,
                max_per_root: int = 12) -> np.ndarray:
    """Ordered (better, worse) index pairs inside each root."""
    order = np.argsort(root, kind="stable")
    sorted_root = root[order]
    bounds = np.flatnonzero(np.diff(sorted_root)) + 1
    blocks = np.split(order, bounds)
    out = []
    for block in blocks:
        if len(block) < 2:
            continue
        values = target[block]
        rows, cols = np.triu_indices(len(block), k=1)
        gap = values[rows] - values[cols]
        keep = np.abs(gap) > min_gap
        rows, cols, gap = rows[keep], cols[keep], gap[keep]
        if len(rows) == 0:
            continue
        better = np.where(gap > 0, block[rows], block[cols])
        worse = np.where(gap > 0, block[cols], block[rows])
        pair = np.stack([better, worse], axis=1)
        if len(pair) > max_per_root:
            pair = pair[rng.choice(len(pair), max_per_root, replace=False)]
        out.append(pair)
    return np.concatenate(out) if out else np.zeros((0, 2), dtype=np.int64)


# ---------------------------------------------------------------------------
# Evaluation (the same statistics the offline gate reports)
# ---------------------------------------------------------------------------

def predict(model, arch, features, device, batch, mirror=None) -> np.ndarray:
    model.eval()
    out = np.empty(len(features[0]) if arch == "cnn" else len(features),
                   dtype=np.float32)
    total = len(out)
    with torch.no_grad():
        for start in range(0, total, batch):
            stop = min(start + batch, total)
            if arch == "leaf":
                idx = torch.from_numpy(features[start:stop].astype(np.int64)).to(device)
                if mirror is not None:
                    idx = mirror[idx]
                value, _ = model(idx)
            else:
                block = features[0][start:stop]
                if mirror is not None:
                    block = block[:, :, :, ::-1].copy()
                x = torch.from_numpy(block).to(device)
                with torch.autocast("cuda", dtype=torch.bfloat16,
                                    enabled=device.type == "cuda"):
                    value, _ = model(x)
                value = value.float()
            out[start:stop] = value.float().cpu().numpy()
    return out


def rank_report(score: np.ndarray, panel, tag: str) -> dict:
    root_count = panel.root_count
    best = pd.argmax_by_root(score, panel.root, panel.column, root_count)
    teacher = pd.argmax_by_root(panel.value, panel.root, panel.column, root_count)
    top2 = pd.topk_by_root(score, panel.root, panel.column, root_count, 2)
    covered = teacher >= 0
    top1 = float(np.mean(best[covered] == teacher[covered]))
    hit2 = float(np.mean((top2[covered, 0] == teacher[covered]) |
                         (top2[covered, 1] == teacher[covered])))
    # Normalised regret of the student's choice under the teacher's values.
    value_by_root = np.full((root_count, pd.COLUMNS), np.nan, dtype=np.float64)
    value_by_root[panel.root, panel.column] = panel.value
    with np.errstate(invalid="ignore"):
        high = np.nanmax(value_by_root, axis=1)
        low = np.nanmin(value_by_root, axis=1)
    picked = value_by_root[np.arange(root_count), np.maximum(best, 0)]
    spread = high - low
    usable = covered & (spread > 1e-9) & np.isfinite(picked)
    regret = float(np.mean((high[usable] - picked[usable]) / spread[usable]))
    return {f"{tag}Top1": top1, f"{tag}Top2": hit2,
            f"{tag}NormalisedRegret": regret,
            f"{tag}Roots": int(covered.sum())}


def pairwise_accuracy(score: np.ndarray, panel, min_gap: float = 0.0) -> float:
    rng = np.random.default_rng(0xA526)
    pairs = build_pairs(panel.root, panel.value, min_gap, rng, max_per_root=64)
    if len(pairs) == 0:
        return float("nan")
    return float(np.mean(score[pairs[:, 0]] > score[pairs[:, 1]]))


# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", nargs="+", required=True)
    parser.add_argument("--after", nargs="+", default=[],
                        help="expand(1) output; when present, each training step "
                             "draws an independent realisation of the reveal "
                             "randomness for every sibling, which is the same "
                             "quantity the deployed search averages over its "
                             "chance strata")
    parser.add_argument("--out", required=True)
    parser.add_argument("--arch", choices=("leaf", "cnn"), default="leaf")
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--mid", type=int, default=64)
    parser.add_argument("--channels", type=int, default=128)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--init-from", default="")
    parser.add_argument("--batch-roots", type=int, default=1024)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--lr", type=float, default=2e-3)
    parser.add_argument("--temperature", type=float, default=0.5)
    parser.add_argument("--pairwise-weight", type=float, default=1.0)
    parser.add_argument("--regression-weight", type=float, default=0.3)
    parser.add_argument("--lifetime-weight", type=float, default=0.05)
    parser.add_argument("--margin-scale", type=float, default=1.0)
    parser.add_argument("--min-gap", type=float, default=0.02)
    parser.add_argument("--seed", type=int, default=0xA526)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--bf16", action="store_true", default=True)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")

    records = pd.load(*args.corpus)
    train_mask, val_mask, test_mask = pd.split_by_origin(records)
    print(json.dumps({"roots": int(len(records)),
                      "origins": int(len(np.unique(records["gameSeed"]))),
                      "trainRoots": int(train_mask.sum()),
                      "valRoots": int(val_mask.sum()),
                      "testRoots": int(test_mask.sum())}), flush=True)

    def make_panel(mask):
        return pd.Panel(np.asarray(records[mask]))

    panels = {name: make_panel(mask) for name, mask in
              (("train", train_mask), ("val", val_mask), ("test", test_mask))}

    draws = {}
    if args.after:
        after = np.concatenate([np.fromfile(path, dtype=pd.AFTER_DTYPE)
                                for path in args.after])
        for name, mask in (("train", train_mask), ("val", val_mask),
                           ("test", test_mask)):
            draws[name] = pd.attach_draws(panels[name], after,
                                          np.flatnonzero(mask), len(records))
        print(json.dumps({"drawsPerSibling": float(
            draws["train"].shape[1])}), flush=True)

    def featurise(panel):
        if args.arch == "leaf":
            return leaf_index(panel.after_board, panel.after_next_disc,
                              panel.after_moves_remaining)
        return (plane_encode(panel.after_board, panel.after_next_disc,
                             panel.after_moves_remaining),)

    features = {name: featurise(panel) for name, panel in panels.items()}

    def featurise_draw(name, rows, draw_index):
        """Features for one independently drawn realisation per sibling."""
        block = draws[name]
        board = block["board"][rows, draw_index]
        nxt = block["nextDisc"][rows, draw_index]
        rem = block["movesRemaining"][rows, draw_index]
        if args.arch == "leaf":
            return leaf_index(board, nxt, rem)
        return plane_encode(board, nxt, rem)

    if args.arch == "leaf":
        model = LeafStudent(args.hidden, args.mid).to(device)
        mirror = torch.from_numpy(lf.mirror_table().astype(np.int64)).to(device)
        adopted = 0
    else:
        model = CnnStudent(args.channels, args.blocks).to(device)
        mirror = None
        adopted = model.load_survival_backbone(args.init_from) if args.init_from else 0
    print(f"parameters {sum(p.numel() for p in model.parameters())}, "
          f"warm-started tensors {adopted}", flush=True)

    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-5)

    train = panels["train"]
    train_features = features["train"]
    root_ids = np.unique(train.root)
    offsets = pd.group_offsets(train.root, train.root_count)
    steps_per_epoch = max(1, len(root_ids) // args.batch_roots)
    schedule = torch.optim.lr_scheduler.OneCycleLR(
        optimizer, max_lr=args.lr, total_steps=steps_per_epoch * args.epochs)

    # Residual targets, standardised only for the weak regression anchor.
    residual_mean = float(train.residual.mean())
    residual_std = float(train.residual.std() + 1e-6)
    lifetime_target_all = {
        name: np.log1p(np.asarray(panels[name].roots["movesToEnd"],
                                  dtype=np.float32))[panels[name].root]
        for name in panels}

    history = []
    best_val = -1.0
    best_state = None
    for epoch in range(args.epochs):
        model.train()
        order = rng.permutation(root_ids)
        started = time.time()
        running, batches = 0.0, 0
        for step in range(steps_per_epoch):
            chunk = order[step * args.batch_roots:(step + 1) * args.batch_roots]
            rows = np.concatenate([np.arange(offsets[r], offsets[r + 1])
                                   for r in chunk])
            # `offsets` indexes a panel sorted by root; the panel already is.
            local_root = train.root[rows]
            remap = {int(r): i for i, r in enumerate(np.unique(local_root))}
            local = np.array([remap[int(r)] for r in local_root], dtype=np.int64)
            group_count = len(remap)

            target = torch.from_numpy(train.value[rows]).to(device)
            immediate = torch.from_numpy(train.immediate[rows]).to(device)
            residual = torch.from_numpy(train.residual[rows]).to(device)
            lifetime = torch.from_numpy(lifetime_target_all["train"][rows]).to(device)
            group = torch.from_numpy(local).to(device)

            flip = rng.random() < 0.5
            if draws:
                pick = rng.integers(0, draws["train"].shape[1], size=len(rows))
                drawn_features = featurise_draw("train", rows, pick)
            else:
                drawn_features = None
            if args.arch == "leaf":
                block_index = (drawn_features if drawn_features is not None
                               else train_features[rows])
                idx = torch.from_numpy(block_index.astype(np.int64)).to(device)
                if flip:
                    idx = mirror[idx]
                value, life = model(idx)
            else:
                block = (drawn_features if drawn_features is not None
                         else train_features[0][rows])
                if flip:
                    block = block[:, :, :, ::-1].copy()
                # bf16 on gfx1151 is 11-14x faster than fp32 and the losses
                # below are computed in fp32 after the cast back.
                with torch.autocast("cuda", dtype=torch.bfloat16,
                                    enabled=args.bf16 and device.type == "cuda"):
                    value, life = model(torch.from_numpy(block).to(device))
                value = value.float()
                life = life.float()

            score = immediate + value
            loss = listwise_loss(score, target, group, group_count, args.temperature)
            pairs = build_pairs(local, train.value[rows], args.min_gap, rng)
            if args.pairwise_weight > 0 and len(pairs):
                pt = torch.from_numpy(pairs).to(device)
                loss = loss + args.pairwise_weight * pairwise_loss(
                    score, target, group, None, pt, args.margin_scale)
            if args.regression_weight > 0:
                normalised = (residual - residual_mean) / residual_std
                loss = loss + args.regression_weight * F.smooth_l1_loss(
                    (value - residual_mean) / residual_std, normalised)
            if args.lifetime_weight > 0:
                loss = loss + args.lifetime_weight * F.smooth_l1_loss(life, lifetime)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()
            schedule.step()
            running += float(loss.detach())
            batches += 1

        val_panel = panels["val"]
        val_value = predict(model, args.arch, features["val"], device, 8192)
        val_score = val_panel.immediate + val_value
        report = rank_report(val_score, val_panel, "val")
        report["valPairwise"] = pairwise_accuracy(val_score, val_panel, args.min_gap)
        row = {"epoch": epoch, "trainLoss": running / max(batches, 1),
               "wallSeconds": time.time() - started, **report}
        history.append(row)
        print(json.dumps(row), flush=True)
        if report["valTop1"] > best_val:
            best_val = report["valTop1"]
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}

    if best_state is not None:
        model.load_state_dict(best_state)

    payload = {"arch": args.arch, "args": vars(args), "history": history,
               "residualMean": residual_mean, "residualStd": residual_std,
               "warmStartedTensors": adopted,
               "parameters": sum(p.numel() for p in model.parameters()),
               "device": str(device)}
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.save({"model": {k: v.cpu() for k, v in model.state_dict().items()},
                "arch": args.arch, "hidden": args.hidden, "mid": args.mid,
                "channels": args.channels, "blocks": args.blocks,
                "features": lf.FEATURES, "active": lf.ACTIVE}, args.out + ".pt")
    with open(args.out + ".json", "w") as handle:
        json.dump(payload, handle, indent=2)
    print(json.dumps({"bestValTop1": best_val}), flush=True)


if __name__ == "__main__":
    main()
