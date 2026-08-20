"""Reads the binary corpus written by approaches/lifetime-objective/sibling-corpus.

The record layout is fixed by generate.cpp and asserted there at compile time.
Keep the two in sync; the struct is packed with no implicit padding.

Design note on the target.  Score in five-move Hardcore is ~94% flat row-rise
bonus and correlates with lifetime at r = 0.9995 (see
docs/exploratory/finding-01-score-is-survival.md), so this pipeline never
regresses on score.  It predicts survival, which is the same quantity with no
heavy tail, no 17,000-point quantisation, and one label per move rather than one
per game.
"""

from __future__ import annotations

import dataclasses
import numpy as np

BOARD_SIZE = 7
CELL_COUNT = BOARD_SIZE * BOARD_SIZE
STATE_RECORD_BYTES = 72

EMPTY, SOLID, CRACKED = 0, 8, 9

STATE_DTYPE = np.dtype([
    ("board", np.uint8, (CELL_COUNT,)),
    ("nextDisc", np.uint8),
    ("movesRemaining", np.uint8),
    ("legalMask", np.uint8),
    ("chosenColumn", np.uint8),
    ("movesToDeath", np.uint16),
    ("risesToDeath", np.uint16),
    ("clearsThisMove", np.uint8),
    ("revealsThisMove", np.uint8),
    ("behaviorDepth", np.uint8),
    ("explored", np.uint8),
    ("censoredGame", np.uint8),
    ("occupiedCells", np.uint8),
    ("moveIndex", np.uint16),
    ("gameSeed", np.uint32),
    ("padding", np.uint8, (3,)),
])
assert STATE_DTYPE.itemsize == STATE_RECORD_BYTES, STATE_DTYPE.itemsize


def load_states(path: str) -> np.ndarray:
    """Memory-maps a .states file. 125 GiB of RAM is available but memory-mapping
    keeps multi-gigabyte corpora cheap to slice and shuffle."""
    return np.memmap(path, dtype=STATE_DTYPE, mode="r")


# ---------------------------------------------------------------------------
# Encoding
# ---------------------------------------------------------------------------
# 18 planes of 7x7:
#   0..6   one-hot numbered disc value 1..7
#   7      solid cover
#   8      cracked cover
#   9      empty
#   10..16 the visible next disc, one-hot, broadcast over the board
#   17     moves until the next rise, as (movesRemaining - 1) / 4, broadcast
#
# Broadcasting the two scalars as planes keeps the model fully convolutional and
# lets a residual CNN mix them with local board structure at every layer.
PLANES = 18


def encode(records: np.ndarray) -> np.ndarray:
    """(N,) structured records -> (N, 18, 7, 7) float32."""
    count = len(records)
    board = np.ascontiguousarray(records["board"]).reshape(count, BOARD_SIZE, BOARD_SIZE)
    out = np.zeros((count, PLANES, BOARD_SIZE, BOARD_SIZE), dtype=np.float32)
    for value in range(1, 8):
        out[:, value - 1] = board == value
    out[:, 7] = board == SOLID
    out[:, 8] = board == CRACKED
    out[:, 9] = board == EMPTY
    next_disc = np.asarray(records["nextDisc"], dtype=np.int64)
    for value in range(1, 8):
        out[:, 9 + value] = (next_disc == value)[:, None, None]
    remaining = np.asarray(records["movesRemaining"], dtype=np.float32)
    out[:, 17] = ((remaining - 1.0) / 4.0)[:, None, None]
    return out


def mirror(encoded: np.ndarray) -> np.ndarray:
    """Horizontal reflection. Drop7's rules are left-right symmetric, so this is
    a label-preserving augmentation and also the check that the model has not
    learned a spurious column preference."""
    return encoded[:, :, :, ::-1].copy()


# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
HAZARD_HORIZON = 12  # rises, i.e. 60 moves of lookahead


@dataclasses.dataclass
class Targets:
    hazard: np.ndarray        # (N, HAZARD_HORIZON) 1.0 if the game survived k more rises
    hazard_mask: np.ndarray   # (N, HAZARD_HORIZON) 0.0 where a censored game makes it unknown
    log_moves: np.ndarray     # (N,) log1p(movesToDeath)
    clears: np.ndarray        # (N,) numbered discs cleared by the move actually played
    reveals: np.ndarray       # (N,) covers revealed by the move actually played


def build_targets(records: np.ndarray) -> Targets:
    count = len(records)
    rises = np.asarray(records["risesToDeath"], dtype=np.int32)
    censored = np.asarray(records["censoredGame"], dtype=bool)
    steps = np.arange(1, HAZARD_HORIZON + 1, dtype=np.int32)[None, :]
    hazard = (rises[:, None] >= steps).astype(np.float32)
    # A game stopped at the move cap has a lower bound on its remaining life, not
    # a value.  Where the game survived k rises the label is a true 1; where it
    # did not, a censored game tells us nothing, so mask those entries out.
    mask = np.ones((count, HAZARD_HORIZON), dtype=np.float32)
    mask[censored] = hazard[censored]
    return Targets(
        hazard=hazard,
        hazard_mask=mask,
        log_moves=np.log1p(np.asarray(records["movesToDeath"], dtype=np.float32)),
        clears=np.asarray(records["clearsThisMove"], dtype=np.float32),
        reveals=np.asarray(records["revealsThisMove"], dtype=np.float32),
    )


def split_by_origin(records: np.ndarray, fractions=(0.8, 0.1, 0.1), seed: int = 0x5911_7000):
    """Splits by WHOLE ORIGIN GAME, never by state row.

    docs/benchmarks.md requires this: states from one game are highly correlated,
    so a row-level split leaks the answer and every historical model that did it
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
