"""Numerical parity gate: PyTorch LeafNet vs the exported C++ inference path.

Same contract as parity_net.py.  Also reports the student's agreement with the
teacher CNN on the same states, because the student is the model that actually
plays and the reader needs to know how much foresight the affordability
constraint cost.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import struct
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "afterstate-net"))

import dataset as ds            # noqa: E402
import leaf_features as lf      # noqa: E402
from train_leaf import LeafNet  # noqa: E402


def load_cpp(path: str) -> np.ndarray:
    with open(path, "rb") as handle:
        count, outputs = struct.unpack("<II", handle.read(8))
        data = np.frombuffer(handle.read(), dtype="<f4")
    return data.reshape(count, outputs)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--states", required=True)
    parser.add_argument("--cpp", required=True)
    parser.add_argument("--teacher-cpp", default="",
                        help="optional net-check dump of the CNN on the same states")
    parser.add_argument("--count", type=int, default=4096)
    parser.add_argument("--rel-floor", type=float, default=1.0)
    parser.add_argument("--tol-abs", type=float, default=2e-3)
    parser.add_argument("--tol-rel", type=float, default=2e-3)
    parser.add_argument("--determinism-trials", type=int, default=3)
    parser.add_argument("--json", default="")
    args = parser.parse_args()

    torch.set_num_threads(1)
    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    model = LeafNet(checkpoint["hidden"], checkpoint["mid"])
    model.load_state_dict(checkpoint["model"])
    model.eval()

    records = ds.load_states(args.states)
    stride = len(records) // args.count
    subset = np.asarray(records[np.arange(args.count) * stride])
    index = lf.build(np.ascontiguousarray(subset["board"]),
                     np.asarray(subset["nextDisc"]),
                     np.asarray(subset["movesRemaining"]))
    horizon = int(checkpoint["hazardHorizon"])
    # The reference is proved repeatable before it is used as a gate.  On this
    # host torch.nn.Conv2d is nondeterministic through oneDNN (see
    # parity_net.py); LeafNet contains no convolution, but the check costs
    # nothing and a gate measured against a moving reference is worthless.
    trials = []
    with torch.no_grad():
        for _ in range(max(1, args.determinism_trials)):
            hz, lt, flow = model(torch.from_numpy(index.astype(np.int64)))
            trials.append(torch.cat([hz, lt.unsqueeze(-1), flow], dim=1).double().numpy().copy())
    for position, other in enumerate(trials[1:], 1):
        drift = float(np.abs(other - trials[0]).max())
        if drift != 0.0:
            raise SystemExit(f"PyTorch LeafNet reference is not repeatable: "
                             f"trial {position} differs by {drift:g}")
    reference = trials[0]
    report_repeatable = True

    candidate = load_cpp(args.cpp).astype(np.float64)
    if candidate.shape != reference.shape:
        raise SystemExit(f"shape mismatch {candidate.shape} vs {reference.shape}")

    groups = {"hazardLogits": list(range(horizon)),
              "lifetimeLog": [horizon],
              "flowClears": [horizon + 1],
              "flowReveals": [horizon + 2]}
    report = {"states": int(args.count), "relativeFloor": args.rel_floor,
              "toleranceAbsolute": args.tol_abs, "toleranceRelative": args.tol_rel,
              "heads": {}}
    failures = []
    for name, columns in groups.items():
        a, b = reference[:, columns], candidate[:, columns]
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

    life_ref = np.expm1(reference[:, horizon])
    life_cpp = np.expm1(candidate[:, horizon])
    report["heads"]["lifetimeMoves"] = {
        "maxAbsolute": float(np.abs(life_ref - life_cpp).max()),
        "meanAbsolute": float(np.abs(life_ref - life_cpp).mean()),
        "referenceRange": [float(life_ref.min()), float(life_ref.max())]}

    # The quantity the search consumes is a value in score units.  Report the
    # parity error there too, next to a 17,000-point row-rise bonus.
    report["leafValueScoreUnitsMaxAbsolute"] = float(
        np.abs(life_ref - life_cpp).max() * 3400.0)

    if args.teacher_cpp:
        teacher = load_cpp(args.teacher_cpp).astype(np.float64)
        t_life = np.expm1(teacher[:, horizon])
        report["teacherAgreement"] = {
            "lifetimePearsonStudentVsTeacher":
                float(np.corrcoef(life_ref, t_life)[0, 1]),
            "lifetimeMeanAbsoluteDifferenceMoves": float(np.abs(life_ref - t_life).mean()),
            "teacherMeanMoves": float(t_life.mean()),
            "studentMeanMoves": float(life_ref.mean()),
        }

    report["referenceRepeatable"] = bool(report_repeatable)
    report["determinismTrials"] = int(args.determinism_trials)
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
