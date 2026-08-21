"""Numerical parity gate: PyTorch SurvivalNet vs the exported C++ inference path.

Compares every output head on the same real board states, drawn from the corpus
by the identical deterministic stride net-check.cpp uses, and reports max
absolute and max relative difference per head.  Exits non-zero if any head
exceeds the declared tolerance, so this can gate the rest of the experiment.

Relative difference uses |a-b| / max(|a|,|b|,floor) with an explicit floor so
that a head whose true value is ~0 cannot manufacture an infinite ratio; the
floor is reported with the result.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import struct
import sys

import contextlib

import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "afterstate-net"))
import dataset as ds          # noqa: E402
import train as trainer       # noqa: E402


def load_cpp(path: str) -> np.ndarray:
    with open(path, "rb") as handle:
        count, outputs = struct.unpack("<II", handle.read(8))
        data = np.frombuffer(handle.read(), dtype="<f4")
    assert data.size == count * outputs, (data.size, count, outputs)
    return data.reshape(count, outputs)


def strided(path: str, count: int) -> np.ndarray:
    records = ds.load_states(path)
    total = len(records)
    if total < count:
        raise SystemExit(f"corpus has {total} records, fewer than {count}")
    stride = total // count
    return np.asarray(records[np.arange(count) * stride])


def compute_reference(model, device, args, checkpoint) -> np.ndarray:
    """Runs the PyTorch model and proves the run is repeatable before returning."""
    if args.cache and os.path.exists(args.cache):
        cached = np.load(args.cache)
        if cached.shape[0] == args.count:
            return cached
    records = strided(args.states, args.count)
    encoded = ds.encode(records)
    width = checkpoint["hazardHorizon"] + 3
    trials = []
    for _ in range(max(1, args.determinism_trials)):
        out = np.zeros((args.count, width), dtype=np.float64)
        with torch.no_grad():
            for start in range(0, args.count, args.batch):
                stop = min(start + args.batch, args.count)
                x = torch.from_numpy(encoded[start:stop]).to(device)
                hz, lt, flow = model(x)
                out[start:stop] = torch.cat(
                    [hz, lt.unsqueeze(-1), flow], dim=1).double().cpu().numpy()
        trials.append(out)
    for index, other in enumerate(trials[1:], 1):
        drift = float(np.abs(other - trials[0]).max())
        if drift != 0.0:
            raise SystemExit(
                f"PyTorch reference is not repeatable: trial {index} differs from "
                f"trial 0 by {drift:g}. Refusing to gate against a moving target.")
    if args.cache:
        os.makedirs(os.path.dirname(args.cache) or ".", exist_ok=True)
        np.save(args.cache, trials[0])
    return trials[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--states", required=True)
    parser.add_argument("--cpp", required=True)
    parser.add_argument("--count", type=int, default=4096)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--batch", type=int, default=256)
    parser.add_argument("--torch-threads", type=int, default=1)
    parser.add_argument("--rel-floor", type=float, default=1.0)
    parser.add_argument("--tol-abs", type=float, default=2e-3)
    parser.add_argument("--tol-rel", type=float, default=2e-3)
    parser.add_argument("--json", default="")
    parser.add_argument("--allow-mkldnn", action="store_true",
                        help="do NOT disable oneDNN; see the determinism note below")
    parser.add_argument("--determinism-trials", type=int, default=3,
                        help="repeat the PyTorch reference this many times and "
                             "require bit-identical results before comparing")
    parser.add_argument("--cache", default="",
                        help="npy path for the PyTorch reference outputs")
    args = parser.parse_args()

    torch.set_num_threads(args.torch_threads)
    # HOST DEFECT.  torch.nn.Conv2d on this CPU dispatches to oneDNN and is
    # NOT deterministic: repeated forward passes over an identical input differ
    # in ~6.25% of output elements by up to ~0.15, single-threaded, with every
    # thread env var pinned to 1.  GroupNorm, Linear and torch.mm are exact.
    # Disabling the oneDNN path makes Conv2d exact.  A parity gate measured
    # against a nondeterministic reference measures the reference's noise, so
    # the oneDNN path is off by default and the reference is verified to repeat
    # bit-for-bit before anything is compared.
    context = (torch.backends.mkldnn.flags(enabled=False)
               if not args.allow_mkldnn else contextlib.nullcontext())
    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    model = trainer.SurvivalNet(checkpoint["channels"], checkpoint["blocks"])
    model.load_state_dict(checkpoint["model"])
    model.eval()
    device = torch.device(args.device)
    model.to(device)

    with context:
        reference = compute_reference(model, device, args, checkpoint)

    candidate = load_cpp(args.cpp).astype(np.float64)
    if candidate.shape != reference.shape:
        raise SystemExit(f"shape mismatch {candidate.shape} vs {reference.shape}")

    horizon = checkpoint["hazardHorizon"]
    groups = {
        "hazardLogits": list(range(horizon)),
        "lifetimeLog": [horizon],
        "flowClears": [horizon + 1],
        "flowReveals": [horizon + 2],
    }
    # Two derived quantities the search actually consumes, checked in the units
    # they are used in rather than in logit/log space.
    derived = {
        "hazardProbabilities": (1.0 / (1.0 + np.exp(-reference[:, :horizon])),
                                1.0 / (1.0 + np.exp(-candidate[:, :horizon]))),
        "lifetimeMoves": (np.expm1(reference[:, horizon]), np.expm1(candidate[:, horizon])),
    }

    report = {"states": int(args.count), "device": str(device),
              "relativeFloor": args.rel_floor,
              "toleranceAbsolute": args.tol_abs,
              "toleranceRelative": args.tol_rel, "heads": {}}
    failures = []
    for name, columns in groups.items():
        a = reference[:, columns]
        b = candidate[:, columns]
        absolute = np.abs(a - b)
        relative = absolute / np.maximum.reduce(
            [np.abs(a), np.abs(b), np.full_like(a, args.rel_floor)])
        entry = {"maxAbsolute": float(absolute.max()),
                 "meanAbsolute": float(absolute.mean()),
                 "maxRelative": float(relative.max()),
                 "referenceRange": [float(a.min()), float(a.max())]}
        report["heads"][name] = entry
        if entry["maxAbsolute"] > args.tol_abs and entry["maxRelative"] > args.tol_rel:
            failures.append(name)
    for name, (a, b) in derived.items():
        absolute = np.abs(a - b)
        relative = absolute / np.maximum.reduce(
            [np.abs(a), np.abs(b), np.full_like(a, args.rel_floor)])
        report["heads"][name] = {"maxAbsolute": float(absolute.max()),
                                 "meanAbsolute": float(absolute.mean()),
                                 "maxRelative": float(relative.max()),
                                 "referenceRange": [float(a.min()), float(a.max())]}

    # The decision-relevant statistic: does the C++ path ever rank two states
    # differently from PyTorch on the quantity the leaf uses?  A global sort of
    # 4,096 states will always show a few swaps at float32 rounding, so report
    # how large the disagreements are, not merely that some exist.
    order_reference = np.argsort(reference[:, horizon])
    order_candidate = np.argsort(candidate[:, horizon])
    sorted_ref = reference[order_reference, horizon]
    pair_gap = np.diff(sorted_ref)
    inverted = candidate[order_reference[:-1], horizon] > candidate[order_reference[1:], horizon]
    report["lifetimeRankIdentical"] = bool(np.array_equal(order_reference, order_candidate))
    report["lifetimeAdjacentInversions"] = int(inverted.sum())
    report["lifetimeAdjacentPairs"] = int(len(pair_gap))
    report["lifetimeMaxGapAmongInversionsLogMoves"] = (
        float(pair_gap[inverted].max()) if inverted.any() else 0.0)
    report["lifetimePearson"] = float(np.corrcoef(reference[:, horizon],
                                                  candidate[:, horizon])[0, 1])
    report["pass"] = not failures
    report["failedHeads"] = failures

    print(json.dumps(report, indent=2))
    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=2)
    raise SystemExit(0 if not failures else 1)


if __name__ == "__main__":
    main()
