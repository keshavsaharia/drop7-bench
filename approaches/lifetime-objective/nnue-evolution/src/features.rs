// Sparse public-state feature space for the NNUE leaf.
//
// This is a statement-for-statement Rust port of the proven feature space in
// approaches/lifetime-objective/learned-leaf/leaf_features.py (mirrored in
// C++ by leafnet.hpp and gated by leaf-check.cpp): 8,902 binary features of
// which exactly 135 are active per state.  The pair features are the point of
// the design: a Drop7 disc clears when its number equals the contiguous run
// through it, a pattern a per-cell encoding cannot see but adjacent pairs let
// the first layer represent.
//
// INFORMATION BOUNDARY.  The inputs are exactly the public state: the visible
// board (including the public solid/cracked status of covered discs, never
// their hidden values), the visible next disc, and the moves remaining until
// the row rise.  Score, level, absolute move number, seed and history are
// never read; gate_info_boundary proves states differing only in those fields
// produce identical features.

use drop7_rs::engine::State;

pub const BOARD: usize = 7;
pub const CELLS: usize = BOARD * BOARD;
pub const VALUES: usize = 10;

pub const CELL_BASE: usize = 0;
pub const NEXT_BASE: usize = 490;
pub const MOVES_BASE: usize = 497;
pub const HPAIR_BASE: usize = 502;
pub const VPAIR_BASE: usize = 4702;
pub const FEATURES: usize = 8902;
pub const ACTIVE: usize = CELLS + 1 + 1 + 42 + 42; // 135

/// Writes the ACTIVE feature indices for one public state, in the fixed order
/// of leaf_features.py build(): cells row-major, next disc, moves remaining,
/// horizontal pairs row-major, vertical pairs row-major.  The fixed order is
/// part of the determinism contract: the accumulator sums rows in this order.
pub fn build(state: &State, out: &mut [u16; ACTIVE]) {
    let board = state.board.to_bytes();
    build_from_bytes(&board, state.next_disc, state.moves_remaining, out);
}

/// The same construction from a row-major byte board (row 0 is the top row),
/// for corpus records that no longer carry a live State.
pub fn build_from_bytes(
    board: &[u8; CELLS],
    next_disc: u8,
    moves_remaining: i32,
    out: &mut [u16; ACTIVE],
) {
    let mut cursor = 0;
    for cell in 0..CELLS {
        debug_assert!((board[cell] as usize) < VALUES);
        out[cursor] = (CELL_BASE + cell * VALUES + board[cell] as usize) as u16;
        cursor += 1;
    }
    debug_assert!((1..=7).contains(&next_disc));
    out[cursor] = (NEXT_BASE + next_disc as usize - 1) as u16;
    cursor += 1;
    debug_assert!((1..=5).contains(&moves_remaining));
    out[cursor] = (MOVES_BASE + moves_remaining as usize - 1) as u16;
    cursor += 1;
    for row in 0..BOARD {
        for col in 0..BOARD - 1 {
            let pair = row * (BOARD - 1) + col;
            let a = board[row * BOARD + col] as usize;
            let b = board[row * BOARD + col + 1] as usize;
            out[cursor] = (HPAIR_BASE + pair * 100 + a * VALUES + b) as u16;
            cursor += 1;
        }
    }
    for row in 0..BOARD - 1 {
        for col in 0..BOARD {
            let pair = row * BOARD + col;
            let a = board[row * BOARD + col] as usize;
            let b = board[(row + 1) * BOARD + col] as usize;
            out[cursor] = (VPAIR_BASE + pair * 100 + a * VALUES + b) as u16;
            cursor += 1;
        }
    }
    debug_assert_eq!(cursor, ACTIVE);
}

/// Feature permutation implementing a horizontal board reflection, ported
/// from leaf_features.mirror_table().  Drop7's rules are left-right
/// symmetric; the table is an involution.  Used by the reflection gate and
/// available as a label-preserving augmentation.
pub fn mirror_table() -> Vec<u16> {
    let mut table: Vec<u16> = (0..FEATURES as u16).collect();
    for cell in 0..CELLS {
        let (row, col) = (cell / BOARD, cell % BOARD);
        let target = row * BOARD + (BOARD - 1 - col);
        for value in 0..VALUES {
            table[CELL_BASE + cell * VALUES + value] =
                (CELL_BASE + target * VALUES + value) as u16;
        }
    }
    for row in 0..BOARD {
        for col in 0..BOARD - 1 {
            let pair = row * (BOARD - 1) + col;
            let target = row * (BOARD - 1) + (BOARD - 2 - col);
            for a in 0..VALUES {
                for b in 0..VALUES {
                    // The reflected pair is traversed right-to-left, so the
                    // two values swap as well as the position.
                    table[HPAIR_BASE + pair * 100 + a * VALUES + b] =
                        (HPAIR_BASE + target * 100 + b * VALUES + a) as u16;
                }
            }
        }
    }
    for row in 0..BOARD - 1 {
        for col in 0..BOARD {
            let pair = row * BOARD + col;
            let target = row * BOARD + (BOARD - 1 - col);
            for a in 0..VALUES {
                for b in 0..VALUES {
                    table[VPAIR_BASE + pair * 100 + a * VALUES + b] =
                        (VPAIR_BASE + target * 100 + a * VALUES + b) as u16;
                }
            }
        }
    }
    table
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn active_count_and_bounds_hold_on_the_initial_board() {
        let state = State::initial_headless(0xa527_7001);
        let mut out = [0u16; ACTIVE];
        build(&state, &mut out);
        assert!(out.iter().all(|&f| (f as usize) < FEATURES));
        // The initial board is one solid row along the bottom: rows 0-5
        // empty, row 6 solid.
        for cell in 0..CELLS {
            let expect_value = if cell / BOARD == BOARD - 1 { 8usize } else { 0 };
            assert_eq!(out[cell] as usize, CELL_BASE + cell * VALUES + expect_value);
        }
    }

    #[test]
    fn mirror_table_is_an_involution() {
        let table = mirror_table();
        for f in 0..FEATURES {
            assert_eq!(table[table[f] as usize] as usize, f);
        }
    }

    #[test]
    fn mirrored_features_match_a_mirrored_board() {
        // The mirror table permutes the feature *bag*: the accumulator sums
        // rows, so the invariant is on the sorted multiset of active
        // features, not on the slot order build() emits.
        let state = State::initial_headless(0xa527_7002);
        let table = mirror_table();
        let mut original = [0u16; ACTIVE];
        build(&state, &mut original);
        let mut mirrored_state = state;
        mirrored_state.board = state.board.mirrored();
        let mut mirrored = [0u16; ACTIVE];
        build(&mirrored_state, &mut mirrored);
        let mut permuted: Vec<u16> = original.iter().map(|&f| table[f as usize]).collect();
        permuted.sort_unstable();
        let mut mirrored_sorted = mirrored.to_vec();
        mirrored_sorted.sort_unstable();
        assert_eq!(permuted, mirrored_sorted);
    }
}
