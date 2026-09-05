// Bit-faithful port of Drop7QLearning.py's Drop7FeatureExtractor (upstream
// lines 85-209): the six features Klein & Friedmann kept, computed the way
// their code computes them rather than the way the report describes them.
//
// Upstream conventions, and how they map here:
//   * `free_loc[col]` is the column height; the drop lands at row y = height,
//     rows counted from the bottom.  `PublicView::cell(x, y)` uses the same
//     coordinates.
//   * Gray discs: upstream 9 = untouched, 8 = cracked; this engine is the
//     other way round (8 = untouched, 9 = cracked).  `cover_weight` maps both
//     to the upstream weights (untouched 1, cracked 2).
//   * `groups_of_elements[x][y][0]` is the length of the contiguous occupied
//     run in row y through (x, y).  Upstream tracks it incrementally; the
//     bookkeeping is occupancy-based and therefore equals the run length
//     computed directly from the board, which is what `row_run` does.
//   * A feature is a (key, value) pair appended to a list; keys absent from
//     the list are NOT updated by the ridge term, so presence matters even
//     when the value is 0.  `Features` therefore carries a presence mask.

use drop7_rs::board::{BOARD_SIZE, CRACKED, SOLID};

use crate::view::PublicView;

pub const FEATURE_COUNT: usize = 6;
pub const MIN_EQ_ELEM: usize = 0;
pub const ROW_DETS: usize = 1;
pub const COL_DETS: usize = 2;
pub const MAX_EQ_ELEM: usize = 3;
pub const ONE_DETS: usize = 4;
pub const ELEM_DET: usize = 5;

/// Upstream dictionary keys, in the order the upstream list is built (which is
/// also the order its Q sum is accumulated in).
pub const FEATURE_NAMES: [&str; FEATURE_COUNT] = [
    "min_eq_elem_True",
    "row_dets",
    "col_dets",
    "max_eq_elem",
    "1_dets",
    "elem_det",
];

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Features {
    pub present: [bool; FEATURE_COUNT],
    pub values: [i32; FEATURE_COUNT],
}

impl Features {
    #[inline(always)]
    fn set(&mut self, index: usize, value: i32) {
        self.present[index] = true;
        self.values[index] = value;
    }

    /// Presence mask as six '0'/'1' characters, the parity-export format.
    pub fn mask_string(&self) -> String {
        self.present.iter().map(|&p| if p { '1' } else { '0' }).collect()
    }
}

/// Upstream `next_to_disc` weights: a blank (untouched) neighbour counts 1, a
/// cracked neighbour counts 2, anything else 0.
#[inline(always)]
fn cover_weight(cell: u8) -> i32 {
    match cell {
        SOLID => 1,
        CRACKED => 2,
        _ => 0,
    }
}

/// Upstream `next_to_disc(state, x, y)`: cover weight summed over the left,
/// lower, upper and right neighbours of (x, y); 0 above the board.
#[inline]
pub fn next_to_disc(view: &PublicView, x: usize, y: usize) -> i32 {
    if y >= BOARD_SIZE {
        return 0;
    }
    let mut total = 0;
    if x > 0 {
        total += cover_weight(view.cell(x - 1, y));
    }
    if y > 0 {
        total += cover_weight(view.cell(x, y - 1));
    }
    if y < BOARD_SIZE - 1 {
        total += cover_weight(view.cell(x, y + 1));
    }
    if x < BOARD_SIZE - 1 {
        total += cover_weight(view.cell(x + 1, y));
    }
    total
}

/// Length of the contiguous occupied run in row `y` through column `c`; 0 when
/// (c, y) is empty.  Equals upstream `groups_of_elements[c][y][0]`.
#[inline]
fn row_run(view: &PublicView, c: usize, y: usize) -> usize {
    if view.cell(c, y) == 0 {
        return 0;
    }
    let mut left = c;
    while left > 0 && view.cell(left - 1, y) != 0 {
        left -= 1;
    }
    let mut right = c;
    while right < BOARD_SIZE - 1 && view.cell(right + 1, y) != 0 {
        right += 1;
    }
    right - left + 1
}

/// Upstream `get_new_group_size(state, x, y)` minus its unused third return:
/// the row run the dropped disc would join at (x, y), and the number of discs
/// already in that merged run whose value equals the merged length, each
/// weighted 1 plus its gray adjacency.
#[inline]
fn new_group(view: &PublicView, x: usize, y: usize) -> (usize, i32) {
    if y >= BOARD_SIZE {
        return (0, 0);
    }
    let left = if x > 0 { row_run(view, x - 1, y) } else { 0 };
    let right = if x < BOARD_SIZE - 1 { row_run(view, x + 1, y) } else { 0 };
    let group_size = 1 + left + right;
    let start = x - left;
    let mut detonations = 0;
    for i in 0..group_size {
        let c = start + i;
        if view.cell(c, y) as usize == group_size {
            detonations += next_to_disc(view, c, y) + 1;
        }
    }
    (group_size, detonations)
}

/// The six features of dropping the visible disc into `action`.  `action` may
/// be a full column: upstream evaluates those too (and may choose them), so
/// the learner's bootstrap max needs them.
pub fn features(view: &PublicView, action: usize) -> Features {
    let y = view.heights[action] as usize;
    let mut max_height = 0usize;
    let mut min_height = BOARD_SIZE + 1;
    for col in 0..BOARD_SIZE {
        let h = view.heights[col] as usize;
        if h > max_height {
            max_height = h;
        }
        if h < min_height {
            min_height = h;
        }
    }
    let max_count = view
        .heights
        .iter()
        .filter(|&&h| h as usize == max_height)
        .count();

    let (group_size, row_dets) = new_group(view, action, y);
    let disc = view.next_disc as usize;
    let elem_det = group_size == disc || y + 1 == disc;

    let mut col_dets = 0;
    if y < BOARD_SIZE {
        for row in 0..y {
            if view.cell(action, row) as usize == y + 1 {
                col_dets += next_to_disc(view, action, row) + 1;
            }
        }
        // Upstream adds the landing cell's gray adjacency unconditionally.
        col_dets += next_to_disc(view, action, y);
    }

    let mut f = Features::default();
    if min_height == y {
        f.set(MIN_EQ_ELEM, 1);
    }
    f.set(ROW_DETS, row_dets);
    f.set(COL_DETS, col_dets);
    if max_height == y && max_count <= 2 {
        // Upstream appends ('max_eq_elem', col_dets), not 1.
        f.set(MAX_EQ_ELEM, col_dets);
    }
    if view.next_disc == 1 && elem_det {
        f.set(ONE_DETS, 1 + next_to_disc(view, action, y));
    }
    if elem_det {
        f.set(ELEM_DET, 1 + next_to_disc(view, action, y));
    }
    f
}

#[cfg(test)]
mod tests {
    use super::*;
    use drop7_rs::board::Board;

    fn view(text: &str, next: u8) -> PublicView {
        PublicView::new(Board::from_serialized(text).unwrap(), next, 5)
    }

    #[test]
    fn opening_position_features() {
        // Bottom row all solid gray, next disc 3: every column lands at height
        // 1 on a gray disc (col_dets = 1 from the lower neighbour), the row
        // run would be 1, no detonation, every column is both lowest and
        // tallest with seven-way ties (so max_eq_elem is absent).
        let v = view("0000000000000000000000000000000000000000008888888", 3);
        for a in 0..7 {
            let f = features(&v, a);
            assert_eq!(f.present, [true, true, true, false, false, false]);
            assert_eq!(f.values[MIN_EQ_ELEM], 1);
            assert_eq!(f.values[ROW_DETS], 0);
            assert_eq!(f.values[COL_DETS], 1);
        }
    }

    #[test]
    fn dropped_disc_detonating_by_column_height() {
        // Column 3 holds a gray at the bottom and a 7 above it; dropping a 3
        // makes height 3, so the disc detonates (elem_det present), and the
        // gray adjacency of the landing cell is 0 (its lower neighbour is 7).
        let mut text = String::from("0000000000000000000000000000000000000000008888888");
        // Row 5 (second from bottom), column 3 = '7'.
        let idx = 5 * 7 + 3;
        text.replace_range(idx..idx + 1, "7");
        let v = view(&text, 3);
        let f = features(&v, 3);
        assert!(f.present[ELEM_DET]);
        assert_eq!(f.values[ELEM_DET], 1);
        assert!(!f.present[ONE_DETS]);
        // Column 3 is the unique tallest: max_eq_elem present with col_dets.
        assert!(f.present[MAX_EQ_ELEM]);
        assert!(!f.present[MIN_EQ_ELEM]);
    }

    #[test]
    fn row_run_merges_left_and_right() {
        // Bottom row: cells 0..2 numbered 5, cell 3 empty, cells 4..6 gray.
        // Dropping into column 3 at height 0 joins a run of 7.
        let v = view("0000000000000000000000000000000000000000005550888", 7);
        let (size, dets) = new_group(&v, 3, 0);
        assert_eq!(size, 7);
        assert_eq!(dets, 0);
        let f = features(&v, 3);
        assert!(f.present[ELEM_DET], "a 7 completing a seven-run detonates");
    }

    #[test]
    fn cracked_neighbour_counts_double() {
        // Bottom row: column 2 cracked (9), column 4 solid (8); land in column 3.
        let v = view("0000000000000000000000000000000000000000000090800", 2);
        assert_eq!(next_to_disc(&v, 3, 0), 3);
    }

    #[test]
    fn full_column_has_no_features_beyond_height_indicators() {
        let mut text = String::from("0000000000000000000000000000000000000000008888888");
        for row in 0..7 {
            let idx = row * 7 + 6;
            text.replace_range(idx..idx + 1, "1");
        }
        let v = view(&text, 4);
        assert!(!v.is_legal(6));
        let f = features(&v, 6);
        assert_eq!(f.values[ROW_DETS], 0);
        assert_eq!(f.values[COL_DETS], 0);
        assert!(!f.present[ELEM_DET]);
        assert!(!f.present[MIN_EQ_ELEM]);
        // Unique tallest column: max_eq_elem present with value 0.
        assert!(f.present[MAX_EQ_ELEM]);
        assert_eq!(f.values[MAX_EQ_ELEM], 0);
    }
}
