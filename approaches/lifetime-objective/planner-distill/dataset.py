"""Reader for the fair-planner teacher corpus written by `corpus-gen.cpp`.

The record layout is fixed by `corpus.hpp` and asserted there at compile time
(576 bytes, packed, no implicit padding).  Keep the two in sync.

WHAT ONE ROW IS
---------------
One row is one *root*: a public state, the legal column set, and the teacher's
mean value for EVERY legal column under the same K sampled completions of the
hidden board.  That is the object six of the seventeen documented learned-model
failures in this repository never had
(`docs/exploratory/audit-05-optimistic-curriculum.md` section 4: class (iii),
sibling coverage / within-root discrimination).

A row also carries, per legal column, the *realised* afterstate under the true
master tape.  Those boards are public states, and they are exactly what a chance
node inside the fair search hands to a leaf evaluator, so training on them keeps
the student's input distribution matched to its deployment.

THE DECOMPOSITION THE STUDENT IS FITTED TO
------------------------------------------
    value[c]  =  immediate[c]  +  (mean value of the afterstate)

`immediate[c]` is the discs the move itself clears, averaged over the same K
completions.  A state-only afterstate evaluator cannot represent the first term
- the move's own reward is not a function of the state after it - so the student
is fitted to the residual and the immediate term is supplied by the search, the
way an afterstate evaluator is always used.
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import numpy as np

BOARD_SIZE = 7
CELL_COUNT = BOARD_SIZE * BOARD_SIZE
COLUMNS = 7
RECORD_BYTES = 576

EMPTY, SOLID, CRACKED = 0, 8, 9

ROOT_DTYPE = np.dtype([
    ("board", np.uint8, (CELL_COUNT,)),
    ("nextDisc", np.uint8),
    ("movesRemaining", np.uint8),
    ("legalMask", np.uint8),
    ("occupied", np.uint8),
    ("chosenColumn", np.uint8),
    ("playedColumn", np.uint8),
    ("explored", np.uint8),
    ("samplesUsed", np.uint8),
    ("incomplete", np.uint8),
    ("value", np.float32, (COLUMNS,)),
    ("immediate", np.float32, (COLUMNS,)),
    ("valueLo", np.float32, (COLUMNS,)),
    ("valueHi", np.float32, (COLUMNS,)),
    ("afterBoard", np.uint8, (COLUMNS, CELL_COUNT)),
    ("afterSurvived", np.uint8, (COLUMNS,)),
    ("afterClears", np.uint8, (COLUMNS,)),
    ("afterReveals", np.uint8, (COLUMNS,)),
    ("afterOccupied", np.uint8, (COLUMNS,)),
    ("afterNextDisc", np.uint8, (COLUMNS,)),
    ("afterMovesRemaining", np.uint8, (COLUMNS,)),
    ("moveIndex", np.uint16),
    ("movesToEnd", np.uint16),
    ("censoredGame", np.uint8),
    ("horizon", np.uint8),
    ("samplesConfigured", np.uint16),
    ("gameSeed", np.uint32),
    ("padding", np.uint8, (9,)),
])
assert ROOT_DTYPE.itemsize == RECORD_BYTES, ROOT_DTYPE.itemsize


def load(*paths: str) -> np.ndarray:
    """Concatenates one or more corpus files into a single array of roots."""
    blocks = [np.fromfile(path, dtype=ROOT_DTYPE) for path in paths]
    return np.concatenate(blocks) if len(blocks) > 1 else blocks[0]


def legal_matrix(records: np.ndarray) -> np.ndarray:
    """(N, 7) bool: column c legal at this root."""
    mask = np.asarray(records["legalMask"], dtype=np.uint8)[:, None]
    bits = (np.arange(COLUMNS, dtype=np.uint8)[None, :])
    return ((mask >> bits) & 1).astype(bool)


def split_by_origin(records: np.ndarray, fractions=(0.8, 0.1, 0.1),
                    seed: int = 0xA526_D157):
    """Splits by WHOLE ORIGIN GAME, never by root row.

    `docs/benchmarks.md` requires this, and the repository's own history is the
    reason: consecutive roots inside one game share almost their whole board, so
    a row-level split leaks the answer and every historical model that used one
    reported an optimistic held-out number.
    """
    seeds = np.unique(np.asarray(records["gameSeed"]))
    rng = np.random.default_rng(seed)
    rng.shuffle(seeds)
    first = int(len(seeds) * fractions[0])
    second = first + int(len(seeds) * fractions[1])
    groups = (seeds[:first], seeds[first:second], seeds[second:])
    row_seed = np.asarray(records["gameSeed"])
    return tuple(np.isin(row_seed, group) for group in groups)


# ---------------------------------------------------------------------------
# Sibling panels
# ---------------------------------------------------------------------------

class Panel:
    """A flattened view of every legal (root, column) pair.

    `root[i]` is the index of the root that pair `i` belongs to, so a listwise
    loss is a scatter-softmax over `root`.  Nothing is dropped and nothing is
    imputed: `docs/benchmarks.md` asks for an explicit action-completeness and
    missing-label rate, and both are computed here rather than assumed.
    """

    def __init__(self, records: np.ndarray):
        legal = legal_matrix(records)
        value = np.asarray(records["value"], dtype=np.float32)
        # A legal column with no value is a missing label; there should be none.
        self.missing = int(np.count_nonzero(legal & (value < 0.0)))
        self.illegal_labelled = int(np.count_nonzero(~legal & (value >= 0.0)))
        usable = legal & (value >= 0.0)

        root_index, column = np.nonzero(usable)
        self.roots = records
        self.root = root_index.astype(np.int64)
        self.column = column.astype(np.int64)
        self.value = value[root_index, column]
        self.immediate = np.asarray(records["immediate"],
                                    dtype=np.float32)[root_index, column]
        self.value_lo = np.asarray(records["valueLo"],
                                   dtype=np.float32)[root_index, column]
        self.value_hi = np.asarray(records["valueHi"],
                                   dtype=np.float32)[root_index, column]
        self.after_board = np.asarray(
            records["afterBoard"])[root_index, column]
        self.after_next_disc = np.asarray(
            records["afterNextDisc"])[root_index, column]
        self.after_moves_remaining = np.asarray(
            records["afterMovesRemaining"])[root_index, column]
        self.after_clears = np.asarray(
            records["afterClears"], dtype=np.float32)[root_index, column]
        self.after_survived = np.asarray(
            records["afterSurvived"])[root_index, column]
        self.legal_count = usable.sum(axis=1).astype(np.int32)
        # Residual target: the mean value of the afterstate itself.
        self.residual = self.value - self.immediate

    def __len__(self) -> int:
        return len(self.root)

    @property
    def root_count(self) -> int:
        return len(self.roots)


def group_offsets(root: np.ndarray, root_count: int) -> np.ndarray:
    """Start offset of each root's block in a panel sorted by root index."""
    counts = np.bincount(root, minlength=root_count)
    offsets = np.zeros(root_count + 1, dtype=np.int64)
    np.cumsum(counts, out=offsets[1:])
    return offsets


def argmax_by_root(score: np.ndarray, root: np.ndarray, column: np.ndarray,
                   root_count: int) -> np.ndarray:
    """Per-root argmax column of a flat per-pair score.  Ties go to the lower
    column index, which is exactly the planner's own tie rule."""
    order = np.lexsort((column, -score, root))
    first = np.ones(len(order), dtype=bool)
    sorted_root = root[order]
    first[1:] = sorted_root[1:] != sorted_root[:-1]
    out = np.full(root_count, -1, dtype=np.int64)
    out[sorted_root[first]] = column[order][first]
    return out


def topk_by_root(score: np.ndarray, root: np.ndarray, column: np.ndarray,
                 root_count: int, k: int) -> np.ndarray:
    """(root_count, k) columns in descending score order; -1 where a root has
    fewer than k legal columns."""
    order = np.lexsort((column, -score, root))
    sorted_root = root[order]
    sorted_column = column[order]
    out = np.full((root_count, k), -1, dtype=np.int64)
    starts = np.searchsorted(sorted_root, np.arange(root_count), side="left")
    stops = np.searchsorted(sorted_root, np.arange(root_count), side="right")
    for rank in range(k):
        take = starts + rank < stops
        out[take, rank] = sorted_column[starts[take] + rank]
    return out


# ---------------------------------------------------------------------------
# Independent chance realisations of each sibling afterstate
# ---------------------------------------------------------------------------

AFTER_DTYPE = np.dtype([
    ("row", np.uint32), ("column", np.uint8), ("draw", np.uint8),
    ("survived", np.uint8), ("clears", np.uint8), ("reveals", np.uint8),
    ("nextDisc", np.uint8), ("movesRemaining", np.uint8), ("occupied", np.uint8),
    ("board", np.uint8, (CELL_COUNT,)), ("padding", np.uint8, (3,)),
])
assert AFTER_DTYPE.itemsize == 64, AFTER_DTYPE.itemsize


def attach_draws(panel: "Panel", after: np.ndarray, rows_kept: np.ndarray,
                 total_rows: int) -> np.ndarray:
    """Aligns `expand(1)` output to a panel, as a (pairs, draws) structured array.

    Draw 0 is always the realisation the game actually entered, so a consumer
    that ignores this table sees exactly the corpus's own afterstate.  The other
    draws are independent samples of the reveal randomness from its exact public
    marginal, which is the quantity the deployed search averages over its chance
    strata.  Missing draws are filled with draw 0 rather than dropped, so every
    sibling keeps the same shape and no root is silently down-weighted.
    """
    remap = np.full(total_rows, -1, dtype=np.int64)
    remap[rows_kept] = np.arange(len(rows_kept))
    keep = np.isin(after["row"], rows_kept)
    after = after[keep]
    local_root = remap[after["row"].astype(np.int64)]

    pair_of = np.full((panel.root_count, COLUMNS), -1, dtype=np.int64)
    pair_of[panel.root, panel.column] = np.arange(len(panel))
    pair_index = pair_of[local_root, after["column"].astype(np.int64)]
    valid = pair_index >= 0
    after = after[valid]
    pair_index = pair_index[valid]
    draw_index = after["draw"].astype(np.int64)
    depth = int(draw_index.max()) + 1 if len(draw_index) else 1

    fields = [("board", np.uint8, (CELL_COUNT,)), ("nextDisc", np.uint8),
              ("movesRemaining", np.uint8), ("clears", np.uint8),
              ("survived", np.uint8)]
    out = np.zeros((len(panel), depth), dtype=np.dtype(fields))
    # Seed every slot with draw 0 so gaps are filled rather than zeroed.
    out["board"][:] = panel.after_board[:, None, :]
    out["nextDisc"][:] = panel.after_next_disc[:, None]
    out["movesRemaining"][:] = panel.after_moves_remaining[:, None]
    out["clears"][:] = panel.after_clears[:, None].astype(np.uint8)
    out["survived"][:] = panel.after_survived[:, None]
    for name in ("board", "nextDisc", "movesRemaining", "clears", "survived"):
        out[name][pair_index, draw_index] = after[name]
    return out
