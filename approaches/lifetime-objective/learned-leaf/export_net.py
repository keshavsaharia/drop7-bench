"""Exports a trained SurvivalNet checkpoint to a versioned, self-describing
binary that a dependency-free C++ inference path can read.

Why a custom format rather than TorchScript or ONNX: the consumer is an
expectimax search that is called hundreds of thousands of times per decision
and must stay a single self-contained native binary with no runtime, no
dynamic library search, and no allocator surprises.  The format is therefore
deliberately dumb: a magic, a version, the architecture scalars, and a flat
name-keyed table of float32 tensors in the checkpoint's own layout.

Layout (little endian throughout):

    char[8]   "D7NET\0\0\0"
    u32       formatVersion (1)
    u32       planes, channels, blocks, poolChannels, hazardHorizon,
              headHidden, boardSize, groupCount
    f32       normEpsilon
    u32       tensorCount
    repeated tensorCount times:
        u32   nameBytes
        char  name[nameBytes]
        u32   rank
        u32   dims[rank]
        f32   data[prod(dims)]        (PyTorch's own contiguous order)
    u64       fnv1aOfEverythingAbove   (integrity, not cryptographic)
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import struct
import sys

import numpy as np
import torch

MAGIC = b"D7NET\0\0\0"
FORMAT_VERSION = 1


def fnv1a(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def pack_tensor(name: str, tensor: torch.Tensor) -> bytes:
    array = tensor.detach().cpu().float().contiguous().numpy()
    raw = name.encode("utf-8")
    out = struct.pack("<I", len(raw)) + raw
    out += struct.pack("<I", array.ndim)
    out += b"".join(struct.pack("<I", d) for d in array.shape)
    out += array.astype("<f4", copy=False).tobytes()
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--eps", type=float, default=1e-5,
                        help="GroupNorm epsilon; PyTorch's default is 1e-5")
    args = parser.parse_args()

    checkpoint = torch.load(args.checkpoint, map_location="cpu")
    state = checkpoint["model"]
    channels = int(checkpoint["channels"])
    blocks = int(checkpoint["blocks"])
    planes = int(checkpoint["planes"])
    horizon = int(checkpoint["hazardHorizon"])
    pool_channels = int(state["pool.0.weight"].shape[0])
    head_hidden = int(state["lifetime.0.weight"].shape[0])
    groups = min(32, channels)

    # The exporter refuses to guess: every tensor the C++ reader needs is named
    # explicitly, and any checkpoint key not consumed is a hard error.
    wanted = ["stem.0.weight", "stem.1.weight", "stem.1.bias"]
    for block in range(blocks):
        for part in ("a", "b"):
            wanted.append(f"tower.{block}.{part}.weight")
        for part in ("na", "nb"):
            wanted += [f"tower.{block}.{part}.weight", f"tower.{block}.{part}.bias"]
    wanted += ["pool.0.weight", "pool.1.weight", "pool.1.bias"]
    for head in ("hazard", "lifetime", "flow"):
        wanted += [f"{head}.0.weight", f"{head}.0.bias",
                   f"{head}.2.weight", f"{head}.2.bias"]

    missing = [k for k in wanted if k not in state]
    extra = [k for k in state if k not in wanted]
    if missing or extra:
        print(f"checkpoint mismatch: missing={missing} extra={extra}", file=sys.stderr)
        raise SystemExit(1)

    body = b""
    body += MAGIC
    body += struct.pack("<I", FORMAT_VERSION)
    body += struct.pack("<8I", planes, channels, blocks, pool_channels,
                        horizon, head_hidden, 7, groups)
    body += struct.pack("<f", args.eps)
    body += struct.pack("<I", len(wanted))
    for name in wanted:
        body += pack_tensor(name, state[name])

    digest = fnv1a(body)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as handle:
        handle.write(body)
        handle.write(struct.pack("<Q", digest))

    total = sum(state[name].numel() for name in wanted)
    print(f"wrote {args.out}: {len(wanted)} tensors, {total} floats, "
          f"{os.path.getsize(args.out)} bytes, fnv1a=0x{digest:016x}")


if __name__ == "__main__":
    main()
