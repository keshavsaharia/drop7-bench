"""Does the student carry information the fair depth-4 search does not already
have?

The preregistered gate compares the student *alone* against fair depth 4
*alone*, and that comparison is unequal in a way worth stating: fair D4 spends
615,090 leaf evaluations per decision, and the student spends one per sibling.
Losing that comparison does not by itself mean the student is useless, because
its intended use is as the leaf *inside* that search, adding to it rather than
replacing it.

This probe asks the cheap version of the deployment question directly, on the
same held-out roots and with no further gameplay:

    blended_c = z(fairD4_c) + lambda * z(student_c)

where `z` standardises within the root, and sweeps `lambda`.  If agreement with
the teacher rises above `lambda = 0`, the student holds signal the search does
not, and the failure is one of *search integration* rather than of the label.
If it falls monotonically, the student holds nothing fair D4 lacks, and the
failure is upstream of any integration.

This is a diagnostic on already-read development data.  It selects nothing and
opens no cohort.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import argparse
import json
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "learned-leaf"))
sys.path.insert(0, HERE)

import dataset as pd            # noqa: E402
import train_student as ts      # noqa: E402
import offline_gate as og       # noqa: E402


def standardise(score: np.ndarray, root: np.ndarray, root_count: int) -> np.ndarray:
    total = np.bincount(root, weights=score, minlength=root_count)
    count = np.bincount(root, minlength=root_count).astype(np.float64)
    mean = total / np.maximum(count, 1)
    centred = score - mean[root]
    variance = np.bincount(root, weights=centred ** 2, minlength=root_count)
    sd = np.sqrt(variance / np.maximum(count, 1))
    return centred / np.maximum(sd[root], 1e-9)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", nargs="+", required=True)
    parser.add_argument("--after", nargs="+", required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--d4-rank", required=True)
    parser.add_argument("--split", default="test")
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    records = pd.load(*args.corpus)
    masks = dict(zip(("train", "val", "test"), pd.split_by_origin(records)))
    mask = masks[args.split]
    rows_kept = np.flatnonzero(mask)
    subset = np.asarray(records[mask])
    panel = pd.Panel(subset)
    root_count = panel.root_count

    blob = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")
    model = (ts.LeafStudent(blob["hidden"], blob["mid"]) if blob["arch"] == "leaf"
             else ts.CnnStudent(blob["channels"], blob["blocks"]))
    model.load_state_dict(blob["model"])
    model = model.to(device).eval()

    after = np.concatenate([np.fromfile(p, dtype=pd.AFTER_DTYPE) for p in args.after])
    after = after[np.isin(after["row"], rows_kept)]
    remap = np.full(len(records), -1, dtype=np.int64)
    remap[rows_kept] = np.arange(len(rows_kept))
    after_root = remap[after["row"].astype(np.int64)]
    boards = np.ascontiguousarray(after["board"])
    if blob["arch"] == "leaf":
        feats = ts.leaf_index(boards, after["nextDisc"], after["movesRemaining"])
    else:
        feats = (ts.plane_encode(boards, after["nextDisc"], after["movesRemaining"]),)
    values = ts.predict(model, blob["arch"], feats, device, 8192)
    per_draw = values + after["clears"].astype(np.float32)
    key = after_root * pd.COLUMNS + after["column"].astype(np.int64)
    sums = np.bincount(key, weights=per_draw, minlength=root_count * pd.COLUMNS)
    counts = np.bincount(key, minlength=root_count * pd.COLUMNS)
    pair_key = panel.root * pd.COLUMNS + panel.column
    student = sums[pair_key] / np.maximum(counts[pair_key], 1)

    rank = np.fromfile(args.d4_rank, dtype=og.RANK_DTYPE)
    rank = rank[np.isin(rank["row"], rows_kept)]
    grid = np.full((root_count, pd.COLUMNS), -np.inf)
    grid[remap[rank["row"].astype(np.int64)]] = rank["value"]
    covered = np.zeros(root_count, dtype=bool)
    covered[remap[rank["row"].astype(np.int64)]] = True
    take = covered[panel.root]
    uniq, local = np.unique(panel.root[take], return_inverse=True)
    column = panel.column[take]
    value = panel.value[take]
    d4 = grid[panel.root[take], column]
    student = student[take]

    zd4 = standardise(d4.astype(np.float64), local, len(uniq))
    zst = standardise(student.astype(np.float64), local, len(uniq))

    out = {"roots": int(len(uniq)), "sweep": []}
    for lam in [0.0, 0.05, 0.1, 0.15, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 1e9]:
        blended = zd4 + (lam if lam < 1e8 else 1.0) * zst
        if lam >= 1e8:
            blended = zst
        block, _, _ = og.ranking_block(blended.astype(np.float32), local, column,
                                       value, len(uniq))
        out["sweep"].append({"lambda": lam if lam < 1e8 else None,
                             **{k: block[k] for k in
                                ("top1", "top2", "pairwise", "normalisedRegret")}})
    with open(args.out, "w") as handle:
        json.dump(out, handle, indent=2)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
