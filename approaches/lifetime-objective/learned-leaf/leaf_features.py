"""Sparse feature construction for the leaf-affordable student network.

WHY A SECOND, SMALLER MODEL EXISTS

The trained SurvivalNet (3,006,543 parameters, 18x7x7 residual CNN) costs
4.12 ms per state in the exported C++ path on this host.  A depth-4 fair
expectimax evaluates 615,090 leaves per decision at five chance strata and
2,271,280 at seven (measured, build/lifetime-leaf/leaf-probe).  Putting the CNN
at that leaf costs ~2,500 s per decision against a 0.887 s reference decision,
which is not a slower experiment but an impossible one.

A leaf evaluator has to cost about one microsecond.  That is an NNUE-shaped
budget, so this module defines an NNUE-shaped feature space: binary features
that are looked up and summed rather than multiplied through a dense matrix.

FEATURES (8,902 total, exactly 135 active per state)

    [0, 490)      cell c in 0..48 holding value v in 0..9   -> c*10 + v
                  (0 empty, 1..7 numbered, 8 solid, 9 cracked)
    [490, 497)    visible next disc 1..7
    [497, 502)    moves until the next rise, 1..5
    [502, 4702)   horizontal adjacent pair p in 0..41 with values (a, b)
                  -> 502 + p*100 + a*10 + b
    [4702, 8902)  vertical adjacent pair p in 0..41 with values (a, b)
                  -> 4702 + p*100 + a*10 + b

The pair features are the point.  A Drop7 disc clears when its number equals the
length of the run it lands in, so run structure is the mechanism; a per-cell bag
of features cannot see a run at all, while adjacent pairs let the first layer
build run detectors directly.
"""

from __future__ import annotations

import numpy as np

BOARD = 7
CELLS = BOARD * BOARD
VALUES = 10
CELL_BASE = 0
NEXT_BASE = 490
MOVES_BASE = 497
HPAIR_BASE = 502
VPAIR_BASE = 4702
FEATURES = 8902
ACTIVE = CELLS + 1 + 1 + 42 + 42          # 135

_H_PAIR_INDEX = np.arange(42, dtype=np.int32).reshape(BOARD, BOARD - 1)
_V_PAIR_INDEX = np.arange(42, dtype=np.int32).reshape(BOARD - 1, BOARD)


def build(board: np.ndarray, next_disc: np.ndarray,
          moves_remaining: np.ndarray) -> np.ndarray:
    """(N,49) uint8 board -> (N,135) uint16 feature indices."""
    count = board.shape[0]
    grid = board.reshape(count, BOARD, BOARD).astype(np.int32)

    cell = (np.arange(CELLS, dtype=np.int32) * VALUES)[None, :] + grid.reshape(count, CELLS)
    nxt = (NEXT_BASE + next_disc.astype(np.int32) - 1).reshape(count, 1)
    mov = (MOVES_BASE + moves_remaining.astype(np.int32) - 1).reshape(count, 1)

    ha, hb = grid[:, :, :-1], grid[:, :, 1:]
    hpair = (HPAIR_BASE + _H_PAIR_INDEX[None] * 100 + ha * VALUES + hb).reshape(count, 42)
    va, vb = grid[:, :-1, :], grid[:, 1:, :]
    vpair = (VPAIR_BASE + _V_PAIR_INDEX[None] * 100 + va * VALUES + vb).reshape(count, 42)

    out = np.concatenate([cell, nxt, mov, hpair, vpair], axis=1)
    assert out.shape[1] == ACTIVE, out.shape
    assert out.max() < FEATURES and out.min() >= 0
    return out.astype(np.uint16)


def mirror_table() -> np.ndarray:
    """Feature permutation implementing a horizontal board reflection.

    Drop7's rules are left-right symmetric, so this is a label-preserving
    augmentation.  It also matters at deployment: the frozen search hands the
    leaf a *canonicalised* state while the corpus stores boards in play
    orientation, so the model must not carry a column preference.
    """
    table = np.arange(FEATURES, dtype=np.int32)
    for cell in range(CELLS):
        row, column = divmod(cell, BOARD)
        target = row * BOARD + (BOARD - 1 - column)
        for value in range(VALUES):
            table[CELL_BASE + cell * VALUES + value] = CELL_BASE + target * VALUES + value
    for row in range(BOARD):
        for column in range(BOARD - 1):
            pair = row * (BOARD - 1) + column
            target = row * (BOARD - 1) + (BOARD - 2 - column)
            for a in range(VALUES):
                for b in range(VALUES):
                    # The reflected pair is traversed right-to-left, so the two
                    # values swap as well as the position.
                    table[HPAIR_BASE + pair * 100 + a * VALUES + b] = \
                        HPAIR_BASE + target * 100 + b * VALUES + a
    for row in range(BOARD - 1):
        for column in range(BOARD):
            pair = row * BOARD + column
            target = row * BOARD + (BOARD - 1 - column)
            for a in range(VALUES):
                for b in range(VALUES):
                    table[VPAIR_BASE + pair * 100 + a * VALUES + b] = \
                        VPAIR_BASE + target * 100 + a * VALUES + b
    assert np.array_equal(table[table], np.arange(FEATURES)), "mirror must be an involution"
    return table
