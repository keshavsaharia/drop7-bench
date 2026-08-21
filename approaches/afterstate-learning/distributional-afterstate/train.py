#!/usr/bin/env python3
"""Distributional afterstate ranker trainer + offline gate.

Experiment EX-20260820-afterstate-pilot-h40-29b8588a. Trains one action-free
afterstate evaluator on the successor-closed corpus, then evaluates the frozen
sibling-ranking gate against exact fair-D4 comparator labels on held-out whole
origins. FP32 only: mixed precision could flip near-tied action rankings.

Model input is strictly public: afterstate board, next visible disc, moves
until rise. Origin seeds and folds are used only for splitting and joining.
Score targets are rescaled by a fixed, data-independent constant.
"""

import argparse
import json
import os
import time

import numpy as np

try:
    import orjson
    def loads_line(line):
        return orjson.loads(line)
except ImportError:  # pragma: no cover
    def loads_line(line):
        return json.loads(line)

import torch
import torch.nn as nn

K_SCENARIOS = 8
HORIZON = 40
N_QUANTILES = 16
QUANTILES = [(i + 1) / (N_QUANTILES + 1) for i in range(N_QUANTILES)]
Q_LO, Q_HI = 0, N_QUANTILES - 1  # nominal 76.5% central interval
SCORE_SCALE = 10_000.0           # fixed target rescale, not data-dependent

BOARD_PLANES = 10   # cell values 0..9
EXTRA_PLANES = 12   # next disc one-hot 7 + moves remaining one-hot 5
IN_PLANES = BOARD_PLANES + EXTRA_PLANES
MAX_ACTIONS = 7


def root_uid(row):
    return f"{row['rootBoard']}:{row['rootNextDisc']}:{row['rootMovesRemaining']}"


def mix32(value):
    """Matches src/core/native/engine.hpp mix32 (for origin-hash half-folds)."""
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    value ^= value >> 16
    return value


def origin_half(row):
    return mix32(int(row["originSeed"], 16)) & 1


def fnv1a(text):
    h = 0x811C9DC5
    for byte in text.encode():
        h ^= byte
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def load_corpus(path, max_rows=None, expected_rows=None):
    """Streams the corpus. With max_rows, keeps a deterministic subsample by
    content-hash threshold (crc32 of the raw line) so memory never scales with
    the full corpus. The kept set is the rows with the smallest content hashes,
    identical to hashing and sorting all rows."""
    import zlib
    if max_rows is not None and expected_rows is None:
        manifest_path = os.path.join(os.path.dirname(path), "manifest.json")
        if os.path.exists(manifest_path):
            with open(manifest_path) as handle:
                expected_rows = json.load(handle)["rows"]
    if max_rows is not None and expected_rows and expected_rows > max_rows:
        # 1.25x headroom so the kept set almost surely covers max_rows.
        threshold = int(2**32 * min(1.0, 1.25 * max_rows / expected_rows))
    else:
        threshold = None
    rows = []
    hashes = []
    with open(path, "rb") as handle:
        for line in handle:
            if threshold is not None:
                h = zlib.crc32(line)
                if h >= threshold:
                    continue
                hashes.append(h)
                rows.append(loads_line(line))
            else:
                rows.append(loads_line(line))
    if threshold is not None and len(rows) > max_rows:
        order = np.argsort(np.array(hashes, dtype=np.uint32),
                           kind="stable")[:max_rows]
        rows = [rows[i] for i in order]
    return rows


class ResBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1)
        self.bn2 = nn.BatchNorm2d(channels)

    def forward(self, x):
        y = torch.nn.functional.gelu(self.bn1(self.conv1(x)))
        y = self.bn2(self.conv2(y))
        return torch.nn.functional.gelu(x + y)


class AfterstateNet(nn.Module):
    def __init__(self, channels=96, blocks=6):
        super().__init__()
        self.stem = nn.Sequential(
            nn.Conv2d(IN_PLANES, channels, 3, padding=1),
            nn.BatchNorm2d(channels),
            nn.GELU(),
        )
        self.blocks = nn.Sequential(*[ResBlock(channels) for _ in range(blocks)])
        self.head = nn.Sequential(nn.Flatten(), nn.Linear(channels * 49, 512), nn.GELU())
        self.quantiles = nn.Linear(512, N_QUANTILES)
        self.survival = nn.Linear(512, 1)
        self.flow = nn.Linear(512, 2)

    def forward(self, x):
        x = self.stem(x)
        x = self.blocks(x)
        h = self.head(x)
        return {"quantiles": self.quantiles(h),
                "survival": self.survival(h).squeeze(-1),
                "flow": self.flow(h)}


def pinball_loss(pred, target):
    target = target.unsqueeze(1)
    taus = torch.tensor(QUANTILES, device=pred.device).unsqueeze(0)
    diff = target - pred
    return torch.maximum(taus * diff, (taus - 1.0) * diff).mean()


_CELL_ROW = np.arange(49) // 7
_CELL_COL = np.arange(49) % 7


def pack(subset):
    """Packs rows into arrays plus (root, scenario) group structure.

    Duplicate public roots recur within a fold; their labels are byte-identical
    (labeling is deterministic in the public state), so keep only the first
    occurrence of each (root, action, scenario).
    """
    seen = set()
    unique = []
    for r in subset:
        key = (root_uid(r), r["action"], r["scenario"])
        if key in seen:
            continue
        seen.add(key)
        unique.append(r)
    subset = unique
    n = len(subset)
    planes = np.zeros((n, IN_PLANES, 7, 7), dtype=np.uint8)
    score = np.empty(n, dtype=np.float64)
    survived = np.empty(n, dtype=np.float32)
    flow = np.empty((n, 2), dtype=np.float32)
    groups = {}

    # Vectorized board encoding: one bytes object per row -> cell matrix.
    cells = np.empty((n, 49), dtype=np.uint8)
    nexts = np.empty(n, dtype=np.int64)
    movess = np.empty(n, dtype=np.int64)
    for i, r in enumerate(subset):
        cells[i] = np.frombuffer(r["afterstateBoard"].encode(), dtype=np.uint8) - 48
        nexts[i] = r["afterstateNextDisc"]
        movess[i] = r["afterstateMovesRemaining"]
        score[i] = r["scoreGained"] / SCORE_SCALE
        survived[i] = float(not r["terminal"])
        played = 1 + r["movesSurvived"]
        flow[i] = (r["clears"] / played, r["reveals"] / played)
        groups.setdefault((root_uid(r), r["scenario"]), []).append(i)
    rows_idx = np.repeat(np.arange(n), 49)
    planes[rows_idx, cells.reshape(-1), np.tile(_CELL_ROW, n),
           np.tile(_CELL_COL, n)] = 1
    planes[np.arange(n), BOARD_PLANES + nexts - 1] = 1
    planes[np.arange(n), BOARD_PLANES + 7 + movess - 1] = 1

    # Padded group views for the vectorized pairwise loss.
    g = len(groups)
    g_index = np.full((g, MAX_ACTIONS), -1, dtype=np.int64)
    for gi, (_, members) in enumerate(groups.items()):
        for j, member in enumerate(members):
            g_index[gi, j] = member
    return {"planes": planes, "score": score, "survived": survived,
            "flow": flow, "g_index": g_index}


def group_batches(packed, groups_per_batch, shuffle):
    n = len(packed["score"])
    g_index = packed["g_index"]
    order = np.random.permutation(len(g_index)) if shuffle else np.arange(len(g_index))
    for start in range(0, len(g_index), groups_per_batch):
        gsel = g_index[order[start:start + groups_per_batch]]
        mask = gsel >= 0
        flat = np.where(mask, gsel, 0).reshape(-1)
        planes = torch.from_numpy(
            packed["planes"][flat].astype(np.float32)).reshape(
                gsel.shape[0], MAX_ACTIONS, IN_PLANES, 7, 7)
        score = torch.from_numpy(packed["score"][flat]).reshape(
            gsel.shape[0], MAX_ACTIONS)
        yield (planes, score, mask, flat)


def train_epoch(model, opt, packed, device, planes_gpu, taus, groups_per_batch=256):
    model.train()
    totals = {"pinball": 0.0, "rank": 0.0, "bce": 0.0, "flow": 0.0, "n": 0}
    survived_gpu = torch.from_numpy(packed["survived"]).to(device)
    flow_gpu = torch.from_numpy(packed["flow"]).to(device)
    # Loss accumulators stay on the GPU; one host sync per epoch, not per batch.
    device_totals = {k: torch.zeros((), device=device)
                     for k in ("pinball", "rank", "bce", "flow")}
    count = 0
    for _, score, mask, flat in group_batches(packed, groups_per_batch, True):
        flat_t = torch.from_numpy(flat).to(device, non_blocking=True)
        planes = planes_gpu[flat_t].float()  # [G*A, planes, 7, 7]
        score = score.to(device, non_blocking=True)
        mask_t = torch.from_numpy(mask).to(device, non_blocking=True)
        groups = mask_t.shape[0]
        out = model(planes)
        q = out["quantiles"].reshape(groups, MAX_ACTIONS, N_QUANTILES)
        values = q.mean(dim=-1)

        diff = score.unsqueeze(-1) - q
        pin = torch.maximum(taus * diff, (taus - 1.0) * diff)
        l_pin = (pin * mask_t.unsqueeze(-1)).sum() / mask_t.sum().clamp(min=1)

        dv = values.unsqueeze(2) - values.unsqueeze(1)
        dt = score.unsqueeze(2) - score.unsqueeze(1)
        sign = torch.sign(dt)
        valid = (sign != 0) & mask_t.unsqueeze(2) & mask_t.unsqueeze(1)
        pair = torch.nn.functional.softplus(-dv * sign)
        l_rank = (pair * valid).sum() / valid.sum().clamp(min=1)

        l_bce = torch.nn.functional.binary_cross_entropy_with_logits(
            out["survival"], survived_gpu[flat_t])
        l_flow = torch.nn.functional.mse_loss(out["flow"], flow_gpu[flat_t])

        loss = l_pin + l_rank + 0.3 * l_bce + 0.1 * l_flow
        opt.zero_grad()
        loss.backward()
        opt.step()
        n = len(flat)
        for k, v in (("pinball", l_pin), ("rank", l_rank),
                     ("bce", l_bce), ("flow", l_flow)):
            device_totals[k] += v.detach() * n
        count += n
    for k in device_totals:
        totals[k] = float(device_totals[k].cpu())
    totals["n"] = count
    return {k: v / max(totals["n"], 1) for k, v in totals.items() if k != "n"}


def spearman(a, b):
    ra = np.argsort(np.argsort(a)).astype(np.float64)
    rb = np.argsort(np.argsort(b)).astype(np.float64)
    if ra.std() == 0 or rb.std() == 0:
        return None
    return float(np.corrcoef(ra, rb)[0, 1])


def ranking_metrics(model_values, target_values):
    actions = sorted(target_values)
    t = np.array([target_values[a] for a in actions])
    m = np.array([model_values[a] for a in actions])
    spread = t.max() - t.min()
    best = int(t.argmax())
    pick = int(m.argmax())
    pairs = 0
    correct = 0.0
    for i in range(len(actions)):
        for j in range(i + 1, len(actions)):
            if t[i] == t[j]:
                continue
            pairs += 1
            correct += float((m[i] - m[j]) * (t[i] - t[j]) > 0)
    return {
        "top1": float(pick == best),
        "top2": float(best in np.argsort(-m)[:2]),
        "pairwise": (correct / pairs) if pairs else None,
        "regret": float((t[best] - t[pick]) / spread) if spread > 0 else 0.0,
    }


def batched_values(model, rows, device, batch=8192):
    """Predicts mean-quantile values and outer interval for afterstate rows."""
    n = len(rows)
    planes = np.zeros((n, IN_PLANES, 7, 7), dtype=np.uint8)
    cells = np.empty((n, 49), dtype=np.uint8)
    nexts = np.empty(n, dtype=np.int64)
    movess = np.empty(n, dtype=np.int64)
    for i, r in enumerate(rows):
        cells[i] = np.frombuffer(r["afterstateBoard"].encode(), dtype=np.uint8) - 48
        nexts[i] = r["afterstateNextDisc"]
        movess[i] = r["afterstateMovesRemaining"]
    rows_idx = np.repeat(np.arange(n), 49)
    planes[rows_idx, cells.reshape(-1), np.tile(_CELL_ROW, n),
           np.tile(_CELL_COL, n)] = 1
    planes[np.arange(n), BOARD_PLANES + nexts - 1] = 1
    planes[np.arange(n), BOARD_PLANES + 7 + movess - 1] = 1
    values = np.empty(len(rows))
    lo = np.empty(len(rows))
    hi = np.empty(len(rows))
    with torch.no_grad():
        for start in range(0, len(rows), batch):
            x = torch.from_numpy(
                planes[start:start + batch].astype(np.float32)).to(device)
            q = model(x)["quantiles"].cpu().numpy()
            values[start:start + batch] = q.mean(axis=1)
            lo[start:start + batch] = q[:, Q_LO]
            hi[start:start + batch] = q[:, Q_HI]
    return values, lo, hi


def evaluate_gate(model, rows, comparator, device):
    """Frozen gate on the held-out corpus (all rows are held-out by fiat)."""
    by_root = {}
    for r in rows:
        by_root.setdefault(root_uid(r), []).append(r)
    max_scenario = max(r["scenario"] for r in rows) + 1
    half_split = max_scenario // 2

    model = model.to(device).eval()
    flat_rows = [r for group in by_root.values() for r in group]
    values, lo, hi = batched_values(model, flat_rows, device)
    for r, v, l, h in zip(flat_rows, values, lo, hi):
        r["_v"] = float(v)
        r["_lo"] = float(l)
        r["_hi"] = float(h)

    coverage = [float(r["_lo"] <= r["scoreGained"] / SCORE_SCALE <= r["_hi"])
                for r in flat_rows]

    all_metrics = {}
    decisive_metrics = {}
    stability = []
    stability_decisive = []
    for uid, group in by_root.items():
        model_actions = {}
        target_actions = {}
        half_targets = {"a": {}, "b": {}}
        for r in group:
            model_actions.setdefault(r["action"], []).append(r["_v"])
            target_actions.setdefault(r["action"], []).append(r["scoreGained"])
            half = "a" if r["scenario"] < half_split else "b"
            half_targets[half].setdefault(r["action"], []).append(r["scoreGained"])
        model_mean = {a: float(np.mean(v)) for a, v in model_actions.items()}
        target_mean = {a: float(np.mean(v)) for a, v in target_actions.items()}
        spread = max(target_mean.values()) - min(target_mean.values())
        decisive = spread > 20_000  # threshold fixed before this corpus was read
        all_metrics.setdefault("model", []).append(
            (uid, ranking_metrics(model_mean, target_mean)))
        if decisive:
            decisive_metrics.setdefault("model", []).append(
                (uid, ranking_metrics(model_mean, target_mean)))
        if uid in comparator:
            for name in ("d4", "d1"):
                comp = comparator[uid].get(name)
                if comp:
                    all_metrics.setdefault(name, []).append(
                        (uid, ranking_metrics(comp, target_mean)))
                    if decisive:
                        decisive_metrics.setdefault(name, []).append(
                            (uid, ranking_metrics(comp, target_mean)))
        if set(half_targets["a"]) == set(half_targets["b"]):
            actions = sorted(half_targets["a"])
            rho = spearman(
                [float(np.mean(half_targets["a"][a])) for a in actions],
                [float(np.mean(half_targets["b"][a])) for a in actions])
            if rho is not None:
                stability.append(rho)
                if decisive:
                    stability_decisive.append(rho)

    def summarize(metrics):
        out = {}
        for key in ("top1", "top2", "pairwise", "regret"):
            vals = [m[key] for _, m in metrics if m[key] is not None]
            out[key] = float(np.mean(vals)) if vals else None
        out["n"] = len(metrics)
        return out

    # Deterministic half-folds by origin-game hash, as preregistered.
    root_half = {uid: origin_half(group[0]) for uid, group in by_root.items()}
    half1, half2 = {}, {}
    for name, metrics in all_metrics.items():
        for uid, m in metrics:
            (half1 if root_half[uid] == 0 else half2).setdefault(
                name, []).append((uid, m))

    return {
        "roots": len(by_root),
        "decisiveRoots": len(decisive_metrics.get("model", [])),
        "pooled": {n: summarize(m) for n, m in all_metrics.items()},
        "half1": {n: summarize(m) for n, m in half1.items()},
        "half2": {n: summarize(m) for n, m in half2.items()},
        "decisivePooled": {n: summarize(m) for n, m in decisive_metrics.items()},
        "labelStabilityMeanSpearman": float(np.mean(stability)) if stability else None,
        "labelStabilityDecisiveSpearman": (
            float(np.mean(stability_decisive)) if stability_decisive else None),
        "quantileIntervalCoverage": float(np.mean(coverage)) if coverage else None,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", required=True,
                        help="training corpus; every row is training-role")
    parser.add_argument("--heldout-corpus", required=True,
                        help="held-out corpus; every row is evaluation-only")
    parser.add_argument("--comparator-labels", default=None)
    parser.add_argument("--out", required=True)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--channels", type=int, default=96)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--max-train-rows", type=int, default=1_000_000)
    parser.add_argument("--groups-per-batch", type=int, default=256)
    parser.add_argument("--max-train-seconds", type=float, default=2 * 3600)
    parser.add_argument("--seed", type=int, default=20260820)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"

    started = time.time()
    # Streaming deterministic subsample at load time: memory never scales with
    # the full corpus. The held-out corpus is loaded only after training.
    rows = load_corpus(args.corpus, max_rows=args.max_train_rows)
    print(f"training rows {len(rows)} (streamed subsample of corpus)")

    comparator = {}
    if args.comparator_labels:
        with open(args.comparator_labels) as handle:
            handle.readline()  # header
            for line in handle:
                uid, fold, action, d4, d1 = line.rstrip("\n").split("\t")
                entry = comparator.setdefault(uid, {"d4": {}, "d1": {}})
                entry["d4"][int(action)] = float(d4)
                entry["d1"][int(action)] = float(d1)
        print(f"comparator roots {len(comparator)}")

    train_rows = rows

    packed = pack(train_rows)
    # Keep the one-hot planes resident on the GPU as uint8; per-batch gather and
    # float conversion on device removes the CPU batch-preparation bottleneck.
    planes_gpu = torch.from_numpy(packed["planes"]).to(device)
    model = AfterstateNet(args.channels, args.blocks).to(device)
    print(f"model parameters {sum(p.numel() for p in model.parameters())} "
          f"device {device}")
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    taus = torch.tensor(QUANTILES, device=device).view(1, 1, -1)
    log = []
    for epoch in range(args.epochs):
        metrics = train_epoch(model, opt, packed, device, planes_gpu, taus,
                              groups_per_batch=args.groups_per_batch)
        sched.step()
        line = {"epoch": epoch, **metrics, "elapsed": time.time() - started}
        log.append(line)
        print(json.dumps(line))
        if time.time() - started > args.max_train_seconds:
            print("training time budget reached")
            break

    torch.save({"model": model.state_dict(), "channels": args.channels,
                "blocks": args.blocks, "quantiles": N_QUANTILES,
                "scoreScale": SCORE_SCALE},
               os.path.join(args.out, "model.pt"))
    with open(os.path.join(args.out, "training-log.json"), "w") as handle:
        json.dump(log, handle, indent=1)

    # Free training tensors before loading the held-out corpus.
    del packed, planes_gpu
    torch.cuda.empty_cache()
    heldout_rows = load_corpus(args.heldout_corpus)
    print(f"held-out rows {len(heldout_rows)}")
    report = evaluate_gate(model, heldout_rows, comparator, device)
    report["wallSeconds"] = time.time() - started
    report["device"] = torch.cuda.get_device_name(0) if device == "cuda" else "cpu"
    report["peakGpuBytes"] = (int(torch.cuda.max_memory_allocated(0))
                              if device == "cuda" else None)
    with open(os.path.join(args.out, "gate-report.json"), "w") as handle:
        json.dump(report, handle, indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
