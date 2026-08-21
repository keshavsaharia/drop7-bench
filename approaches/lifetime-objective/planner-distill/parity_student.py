"""Numerical parity gate: the exported `D7PDST` file evaluated by `student.hpp`
must agree with the PyTorch checkpoint on real corpus states.

`docs/benchmarks.md` requires native/interpreted parity before any gameplay
tier.  It matters here for a specific reason: the C++ path is the one that
plays, and it reimplements the first layer as 135 gathered rows rather than a
matrix product, so a feature-space or ordering mistake would be silent and would
only show up as a mysteriously weak policy.

Also checks the reflection property.  Drop7's rules are left-right symmetric and
the frozen search hands the leaf a *canonicalised* state while the corpus stores
boards in play orientation, so a model with a column preference is broken in a
way that only appears at deployment.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import subprocess
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "learned-leaf"))
sys.path.insert(0, HERE)

import dataset as pd            # noqa: E402
import leaf_features as lf      # noqa: E402
import train_student as ts      # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--exported", required=True)
    parser.add_argument("--probe", default="build/planner-distill/student-probe")
    parser.add_argument("--states", type=int, default=4096)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    records = pd.load(args.corpus)
    panel = pd.Panel(records[: min(len(records), 4 * args.states)])
    take = np.arange(min(args.states, len(panel)))
    board = panel.after_board[take]
    next_disc = panel.after_next_disc[take]
    moves_remaining = panel.after_moves_remaining[take]

    blob = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    model = ts.LeafStudent(blob["hidden"], blob["mid"])
    model.load_state_dict(blob["model"])
    model.eval()

    index = ts.leaf_index(board, next_disc, moves_remaining)
    with torch.no_grad():
        reference, _ = model(torch.from_numpy(index.astype(np.int64)))
    reference = reference.numpy()

    mirror = lf.mirror_table().astype(np.int64)
    with torch.no_grad():
        mirrored, _ = model(torch.from_numpy(mirror[index.astype(np.int64)]))
    mirrored = mirrored.numpy()

    # The native path, driven through a tiny stdin protocol so no libtorch is
    # involved on that side.
    payload = "\n".join(
        " ".join(str(int(v)) for v in board[row]) + f" {int(next_disc[row])}"
        f" {int(moves_remaining[row])}" for row in take)
    result = subprocess.run([args.probe, args.exported], input=payload,
                            capture_output=True, text=True, check=True)
    native = np.array([float(line) for line in result.stdout.split()],
                      dtype=np.float32)

    delta = np.abs(native - reference)
    mirror_delta = np.abs(mirrored - reference)
    report = {
        "states": int(len(take)),
        "nativeVsTorchMaxAbsolute": float(delta.max()),
        "nativeVsTorchMeanAbsolute": float(delta.mean()),
        "nativeVsTorchRelativeMax": float(
            (delta / np.maximum(np.abs(reference), 1e-6)).max()),
        "reflectionMaxAbsolute": float(mirror_delta.max()),
        "reflectionMeanAbsolute": float(mirror_delta.mean()),
        "predictionStd": float(reference.std()),
    }
    report["nativeParityPass"] = report["nativeVsTorchMaxAbsolute"] < 1e-3
    print(json.dumps(report, indent=2))
    if args.out:
        with open(args.out, "w") as handle:
            json.dump(report, handle, indent=2)
    if not report["nativeParityPass"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
