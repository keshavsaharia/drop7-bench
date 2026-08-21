"""Exports a trained LeafNet checkpoint to the versioned d7leaf binary read by
leafnet.hpp.  Same philosophy as export_net.py: a magic, a version, the shape
scalars, and a flat name-keyed float32 table, so the search stays a single
self-contained native binary.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import struct
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import leaf_features as lf  # noqa: E402

MAGIC = b"D7LEAF\0\0"


def fnv1a(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def pack(name: str, tensor: torch.Tensor) -> bytes:
    array = tensor.detach().cpu().float().contiguous().numpy()
    raw = name.encode("utf-8")
    out = struct.pack("<I", len(raw)) + raw + struct.pack("<I", array.ndim)
    out += b"".join(struct.pack("<I", d) for d in array.shape)
    return out + array.astype("<f4", copy=False).tobytes()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    state = checkpoint["model"]
    hidden = int(checkpoint["hidden"])
    mid = int(checkpoint["mid"])
    horizon = int(checkpoint["hazardHorizon"])
    outputs = int(state["out.bias"].shape[0])
    if int(checkpoint["features"]) != lf.FEATURES or int(checkpoint["active"]) != lf.ACTIVE:
        raise SystemExit("checkpoint feature space differs from leaf_features.py")

    names = ["ft.weight", "ft_bias", "l2.weight", "l2.bias", "out.weight", "out.bias"]
    missing = [n for n in names if n not in state]
    extra = [n for n in state if n not in names]
    if missing or extra:
        print(f"checkpoint mismatch missing={missing} extra={extra}", file=sys.stderr)
        raise SystemExit(1)

    body = MAGIC + struct.pack("<I", 1)
    body += struct.pack("<7I", lf.FEATURES, lf.ACTIVE, hidden, mid, outputs, horizon, lf.BOARD)
    body += struct.pack("<I", len(names))
    for name in names:
        body += pack(name, state[name])
    digest = fnv1a(body)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as handle:
        handle.write(body)
        handle.write(struct.pack("<Q", digest))
    total = sum(state[n].numel() for n in names)
    print(f"wrote {args.out}: {total} floats, {os.path.getsize(args.out)} bytes, "
          f"fnv1a=0x{digest:016x}")


if __name__ == "__main__":
    main()
