// Nibble-packed column-major Drop7 board.
//
// EQUIVALENCE CONTRACT.  Every observable result of this module (boards after
// moves, wave lists, reveal draw order, scores, terminal flags) is required to
// be bit-for-bit identical to src/core/native/engine.hpp and
// src/core/typescript/engine.ts.  The differences are representation only:
//
//   R1  The board is seven u32 column words, four bits per cell, instead of a
//       49-byte row-major array.  Nibble n of column word c (bits 4n..4n+3)
//       holds the cell at row (6 - n), column c: the least-significant nibble
//       is the bottom row.  Between waves every column is bottom-packed, so a
//       column's height is the popcount of its non-zero nibbles and the drop
//       landing spot is the first zero nibble.
//   R2  Gravity in one column is a bit gather: PEXT(word, expanded non-zero
//       mask) compacts the surviving nibbles toward the bottom in their
//       original order.  On targets without BMI2 a portable nibble loop
//       produces the identical word.
//   R3  A row rise is `(word << 4) | SOLID` per column: every cell moves up
//       one nibble and a fresh solid disc appears at the bottom.
//   R4  Popper detection derives the seven row and seven column occupancy
//       masks from the column words (SWAR nibble tests + PEXT/PDEP) and reads
//       run lengths from the same 128-entry table the proven C++ fast engine
//       uses, producing the identical row-major popper list.
//   R5  Cover hits are counted for the whole board at once: the popper
//       bitboard is shifted in the four neighbour directions and the four
//       resulting bitboards are summed with a bitwise parallel counter, so
//       reveal and crack masks fall out without a per-cell loop.  Reveal
//       values are still consumed in ascending row-major index order, which
//       is the order the reference consumes random draws.
//
// Cell encoding matches the references: 0 empty, 1..=7 numbered, 8 solid
// (untouched gray), 9 cracked (hit once).

use crate::tables::{REV7, RUN_TABLE};

pub const BOARD_SIZE: usize = 7;
pub const CELL_COUNT: usize = 49;
pub const EMPTY: u8 = 0;
pub const SOLID: u8 = 8;
pub const CRACKED: u8 = 9;

pub const MOVES_PER_LEVEL: i32 = 5;
pub const LEVEL_BONUS: i64 = 17_000;
pub const CLEAR_BONUS: i64 = 70_000;

/// Low bit of each of the seven nibbles in a column word.
const NIBBLE_LOW: u32 = 0x1111_1111;
/// The 28 bits a column word actually uses.
const COL_USED: u32 = 0x0FFF_FFFF;
/// 49-bit mask of the whole board in row-major bitboard layout.
const BOARD_MASK: u64 = (1u64 << 49) - 1;
/// Bitboard of cells whose row-major index is not in column 0 (index % 7 != 0).
const NOT_COL0: u64 = BOARD_MASK & !0x0040_8102_0408_1u64;
/// Bitboard of cells whose row-major index is not in column 6 (index % 7 != 6).
const NOT_COL6: u64 = BOARD_MASK & !0x1020_4081_0204_0u64;
/// Bits 0, 7, 14, ..., 42: the positions of one column inside the row-major
/// 49-bit board layout.
const COL_STRIDE: u64 = 0x0040_8102_0408_1u64;

// ---------------------------------------------------------------------------
// PEXT/PDEP with portable fallbacks.  The fallback is bit-identical; only the
// instruction count differs.  Build with target-cpu=native (or BMI2 enabled)
// to get the single-instruction forms.
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn pext32(x: u32, mask: u32) -> u32 {
    #[cfg(all(target_arch = "x86_64", target_feature = "bmi2"))]
    unsafe {
        core::arch::x86_64::_pext_u32(x, mask)
    }
    #[cfg(not(all(target_arch = "x86_64", target_feature = "bmi2")))]
    {
        let mut result = 0u32;
        let mut bit = 0u32;
        let mut m = mask;
        while m != 0 {
            let low = m & m.wrapping_neg();
            if x & low != 0 {
                result |= 1u32 << bit;
            }
            bit += 1;
            m &= m - 1;
        }
        result
    }
}

#[inline(always)]
pub fn pdep64(x: u64, mask: u64) -> u64 {
    #[cfg(all(target_arch = "x86_64", target_feature = "bmi2"))]
    unsafe {
        core::arch::x86_64::_pdep_u64(x, mask)
    }
    #[cfg(not(all(target_arch = "x86_64", target_feature = "bmi2")))]
    {
        let mut result = 0u64;
        let mut src = x;
        let mut m = mask;
        while m != 0 {
            let low = m & m.wrapping_neg();
            if src & 1 != 0 {
                result |= low;
            }
            src >>= 1;
            m &= m - 1;
        }
        result
    }
}

/// Bit 4n of the result is set iff nibble n of `col` is non-zero.
#[inline(always)]
fn nonzero_flags(col: u32) -> u32 {
    (col | (col >> 1) | (col >> 2) | (col >> 3)) & NIBBLE_LOW
}

/// Bit 4n of the result is set iff nibble n of `col` is >= 8 (a cover disc).
#[inline(always)]
fn high_flags(col: u32) -> u32 {
    (col >> 3) & NIBBLE_LOW
}

/// Gravity inside one packed column: compact the surviving nibbles toward the
/// bottom (least-significant end), preserving their order.  This is the
/// "gravity as a mathematical function on a uint32" operation.
#[inline(always)]
pub fn gravity_col(col: u32) -> u32 {
    let mask = nonzero_flags(col).wrapping_mul(0xF);
    pext32(col, mask)
}

/// The board: seven packed column words.  28 bytes, `Copy`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Board {
    pub cols: [u32; BOARD_SIZE],
}

impl Board {
    #[inline]
    pub const fn empty() -> Board {
        Board { cols: [0; BOARD_SIZE] }
    }

    /// The initial position: one solid covered row along the bottom.
    #[inline]
    pub const fn initial() -> Board {
        Board {
            cols: [SOLID as u32; BOARD_SIZE],
        }
    }

    /// Cell at (row, column); row 0 is the top.
    #[inline(always)]
    pub fn get(&self, row: usize, col: usize) -> u8 {
        (self.cols[col] >> (4 * (BOARD_SIZE - 1 - row))) as u8 & 0xF
    }

    #[inline(always)]
    fn set(&mut self, row: usize, col: usize, value: u8) {
        let shift = 4 * (BOARD_SIZE - 1 - row);
        self.cols[col] =
            (self.cols[col] & !(0xFu32 << shift)) | ((value as u32) << shift);
    }

    /// Number of occupied cells in a column (columns are bottom-packed
    /// between waves, so this is the popcount of the non-zero nibbles).
    #[inline(always)]
    pub fn height(&self, col: usize) -> usize {
        nonzero_flags(self.cols[col]).count_ones() as usize
    }

    /// A column is legal exactly when its top row cell is empty, which under
    /// the bottom-packed invariant is "height < 7".
    #[inline(always)]
    pub fn is_legal(&self, col: usize) -> bool {
        col < BOARD_SIZE && self.cols[col] & 0x0F00_0000 == 0
    }

    /// Drop a disc into a legal column: it lands on the first zero nibble.
    /// Mirrors placeDisc; returns false for an illegal column.
    #[inline]
    pub fn place_disc(&mut self, col: usize, disc: u8) -> bool {
        if !self.is_legal(col) {
            return false;
        }
        let height = self.height(col);
        self.cols[col] |= (disc as u32) << (4 * height);
        true
    }

    /// Lift every column one row and lay a fresh solid row underneath.
    /// Returns false (game over) when any column already reaches the top row,
    /// mirroring raiseCoveredRow's top-row check.
    #[inline]
    pub fn raise_covered_row(&mut self) -> bool {
        for col in 0..BOARD_SIZE {
            if self.cols[col] & 0x0F00_0000 != 0 {
                return false;
            }
        }
        for col in 0..BOARD_SIZE {
            self.cols[col] = ((self.cols[col] << 4) & COL_USED) | SOLID as u32;
        }
        true
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.cols.iter().all(|&c| c == 0)
    }

    /// Row-major serialization identical to the references' serializeBoard:
    /// 49 characters '0'..'9', row 0 first.
    pub fn serialize(&self) -> String {
        let mut out = String::with_capacity(CELL_COUNT);
        for row in 0..BOARD_SIZE {
            for col in 0..BOARD_SIZE {
                out.push((b'0' + self.get(row, col)) as char);
            }
        }
        out
    }

    /// Build from a 49-character serialization (the reverse of `serialize`).
    pub fn from_serialized(text: &str) -> Option<Board> {
        let bytes = text.as_bytes();
        if bytes.len() != CELL_COUNT {
            return None;
        }
        let mut board = Board::empty();
        for (i, &b) in bytes.iter().enumerate() {
            let value = b.wrapping_sub(b'0');
            if value > 9 {
                return None;
            }
            board.set(i / BOARD_SIZE, i % BOARD_SIZE, value);
        }
        Some(board)
    }

    /// Expand to the references' 49-byte row-major array.  One PDEP per
    /// column spreads the seven nibbles into the low nibbles of seven bytes;
    /// the stores place them at their row-major positions.  The leaf
    /// evaluator reads cells many times, so it works on this byte view --
    /// the same random-access cost the C++ leaf pays -- while the engine
    /// keeps the packed words for gravity.
    #[inline]
    pub fn to_bytes(&self) -> [u8; CELL_COUNT] {
        let mut out = [0u8; CELL_COUNT];
        for col in 0..BOARD_SIZE {
            let spread = pdep64(self.cols[col] as u64, 0x0F0F_0F0F_0F0F_0F0F);
            for nibble in 0..BOARD_SIZE {
                // nibble n of the column word is row (6 - n).
                out[(BOARD_SIZE - 1 - nibble) * BOARD_SIZE + col] =
                    (spread >> (8 * nibble)) as u8 & 0xF;
            }
        }
        out
    }

    /// Horizontal mirror: reverse the column order.  Cells keep their rows,
    /// so the packed words themselves are unchanged.
    #[inline]
    pub fn mirrored(&self) -> Board {
        Board {
            cols: [
                self.cols[6], self.cols[5], self.cols[4], self.cols[3],
                self.cols[2], self.cols[1], self.cols[0],
            ],
        }
    }

    /// True when the mirrored board is lexicographically smaller than the
    /// original in the references' row-major byte order.  Only the first
    /// three columns of each row can hold the first difference (column 3
    /// mirrors to itself, and columns 4..6 are then determined), exactly as
    /// the C++ fast engine's mirroredIsSmallerFast reasons.
    pub fn mirrored_is_smaller(&self) -> bool {
        for row in 0..BOARD_SIZE {
            for col in 0..3 {
                let original = self.get(row, col);
                let mirrored = self.get(row, BOARD_SIZE - 1 - col);
                if mirrored != original {
                    return mirrored < original;
                }
            }
        }
        false
    }

    /// Masks-only scan for the leaf evaluator: the seven row and seven
    /// column occupancy masks, without the numbered/covered/cracked
    /// bitboards the leaf never reads.  Same contents as `scan`'s masks.
    pub fn scan_masks(&self) -> ([u8; BOARD_SIZE], [u8; BOARD_SIZE]) {
        let mut col_mask = [0u8; BOARD_SIZE];
        let mut occ = 0u64;
        for col in 0..BOARD_SIZE {
            let nz = nonzero_flags(self.cols[col]);
            let occ7 = REV7[pext32(nz, NIBBLE_LOW) as usize] as u64;
            occ |= pdep64(occ7, COL_STRIDE << col);
            col_mask[col] = occ7 as u8;
        }
        let mut row_mask = [0u8; BOARD_SIZE];
        for row in 0..BOARD_SIZE {
            row_mask[row] = ((occ >> (7 * row)) & 0x7F) as u8;
        }
        (row_mask, col_mask)
    }

    /// Derive the whole-board scan: occupancy/numbered/covered/cracked
    /// bitboards in row-major layout plus the seven row and seven column
    /// occupancy masks used by the run-length table.
    pub fn scan(&self) -> Scan {
        let mut scan = Scan::default();
        let mut occ = 0u64;
        let mut numbered = 0u64;
        let mut covered = 0u64;
        let mut cracked = 0u64;
        for col in 0..BOARD_SIZE {
            let word = self.cols[col];
            let nz = nonzero_flags(word);
            let high = high_flags(word);
            let bit0 = word & NIBBLE_LOW;
            // Flags are indexed by nibble n = row (6 - n); REV7 turns the
            // compacted 7-bit nibble mask into the references' convention
            // (bit r = row r).
            let occ7 = REV7[pext32(nz, NIBBLE_LOW) as usize] as u64;
            let num7 = REV7[pext32(nz & !high, NIBBLE_LOW) as usize] as u64;
            let cov7 = REV7[pext32(high, NIBBLE_LOW) as usize] as u64;
            let crk7 = REV7[pext32(high & bit0, NIBBLE_LOW) as usize] as u64;
            let spread = COL_STRIDE << col;
            occ |= pdep64(occ7, spread);
            numbered |= pdep64(num7, spread);
            covered |= pdep64(cov7, spread);
            cracked |= pdep64(crk7, spread);
            scan.col_mask[col] = occ7 as u8;
        }
        scan.occ = occ;
        scan.numbered = numbered;
        scan.covered = covered;
        scan.cracked = cracked;
        for row in 0..BOARD_SIZE {
            scan.row_mask[row] = ((occ >> (7 * row)) & 0x7F) as u8;
        }
        scan
    }

    /// The poppers of the current position in the references' row-major
    /// order.  A numbered disc pops when its value equals the contiguous
    /// occupied run through it in its row or its column.
    pub fn find_poppers(&self, scan: &Scan) -> (u64, usize, u8) {
        let mut popping = 0u64;
        let mut count = 0usize;
        let mut popped_columns = 0u8;
        let mut remaining = scan.numbered;
        while remaining != 0 {
            let index = remaining.trailing_zeros() as usize;
            remaining &= remaining - 1;
            let row = index / BOARD_SIZE;
            let col = index % BOARD_SIZE;
            let value = self.get(row, col);
            let row_run = RUN_TABLE[scan.row_mask[row] as usize].length[col];
            let col_run = RUN_TABLE[scan.col_mask[col] as usize].length[row];
            if row_run == value || col_run == value {
                popping |= 1u64 << index;
                count += 1;
                popped_columns |= 1u8 << col;
            }
        }
        (popping, count, popped_columns)
    }

    /// Resolve one simultaneous wave: remove the poppers, count adjacent hits
    /// on every covered disc with a whole-board parallel counter, draw reveal
    /// values in ascending row-major order, and compact the popped columns.
    /// Returns (reveal count, score-relevant popper count is the caller's).
    pub fn clear_wave<R: crate::rng::Random>(
        &mut self,
        scan: &Scan,
        popping: u64,
        popped_columns: u8,
        random: &mut R,
    ) -> usize {
        // Neighbour sets of the poppers, with column-edge wrap suppressed.
        let up = popping >> 7;
        let down = (popping << 7) & BOARD_MASK;
        let left = (popping & NOT_COL0) >> 1;
        let right = (popping & NOT_COL6) << 1;

        // Whole-board 4-input parallel counter: per cell, hits = ones +
        // 2*twos + 4*fours.  hits >= 2 <=> twos|fours; hits >= 1 <=>
        // ones|twos|fours; exactly one hit <=> ones & !(twos|fours).
        let half_sum = up ^ down;
        let half_carry = up & down;
        let ones1 = half_sum ^ left;
        let twos1 = half_carry | (half_sum & left);
        let ones = ones1 ^ right;
        let carry = ones1 & right;
        let twos = twos1 ^ carry;
        let fours = twos1 & carry;
        let any_hit = ones | twos | fours;
        let multi_hit = twos | fours;

        let solid = scan.covered & !scan.cracked;
        let reveal = (scan.cracked & any_hit) | (solid & multi_hit);
        let new_cracked = solid & ones & !multi_hit;

        // Apply to the column words.  The three update sets are disjoint
        // (poppers are numbered, reveals and new cracks are covered), so only
        // the reveal order is observable: it is ascending row-major, the
        // order the reference consumes random draws in.
        let mut rest = popping;
        while rest != 0 {
            let index = rest.trailing_zeros() as usize;
            rest &= rest - 1;
            let row = index / BOARD_SIZE;
            let col = index % BOARD_SIZE;
            let shift = 4 * (BOARD_SIZE - 1 - row);
            self.cols[col] &= !(0xFu32 << shift);
        }
        let mut reveal_count = 0usize;
        let mut rest = reveal;
        while rest != 0 {
            let index = rest.trailing_zeros() as usize;
            rest &= rest - 1;
            let row = index / BOARD_SIZE;
            let col = index % BOARD_SIZE;
            let value = random.next_disc();
            let shift = 4 * (BOARD_SIZE - 1 - row);
            self.cols[col] =
                (self.cols[col] & !(0xFu32 << shift)) | ((value as u32) << shift);
            reveal_count += 1;
        }
        let mut rest = new_cracked;
        while rest != 0 {
            let index = rest.trailing_zeros() as usize;
            rest &= rest - 1;
            let row = index / BOARD_SIZE;
            let col = index % BOARD_SIZE;
            let shift = 4 * (BOARD_SIZE - 1 - row);
            self.cols[col] =
                (self.cols[col] & !(0xFu32 << shift)) | ((CRACKED as u32) << shift);
        }

        // Only a column that lost a disc can have a hole; reveals and cracks
        // overwrite in place.  Compact just those columns.
        let mut columns = popped_columns;
        while columns != 0 {
            let col = columns.trailing_zeros() as usize;
            columns &= columns - 1;
            self.cols[col] = gravity_col(self.cols[col]);
        }
        reveal_count
    }
}

/// Whole-board derived bitboards and occupancy masks.
#[derive(Clone, Copy, Default)]
pub struct Scan {
    pub occ: u64,
    pub numbered: u64,
    pub covered: u64,
    pub cracked: u64,
    pub row_mask: [u8; BOARD_SIZE],
    pub col_mask: [u8; BOARD_SIZE],
}

#[cfg(test)]
mod tests {
    use super::*;

    fn board_from_cols(cols: [u32; 7]) -> Board {
        Board { cols }
    }

    #[test]
    fn initial_board_matches_reference_layout() {
        let board = Board::initial();
        assert_eq!(
            board.serialize(),
            "0000000000000000000000000000000000000000008888888"
        );
    }

    #[test]
    fn gravity_compacts_toward_bottom() {
        // Column with cells at rows 1, 3, 6 (nibbles 5, 3, 0).
        let mut board = Board::empty();
        board.set(1, 2, 3);
        board.set(3, 2, 5);
        board.set(6, 2, 1);
        let compacted = gravity_col(board.cols[2]);
        // Expect bottom-packed: nibbles 0,1,2 = 1,5,3 (order preserved).
        assert_eq!(compacted & 0xF, 1);
        assert_eq!((compacted >> 4) & 0xF, 5);
        assert_eq!((compacted >> 8) & 0xF, 3);
        assert_eq!(compacted >> 12, 0);
    }

    #[test]
    fn pext_fallback_matches_semantics() {
        // Exercise the portable path logic directly.
        let col = 0x0ABC_0DEF & COL_USED;
        let mask = nonzero_flags(col).wrapping_mul(0xF);
        let mut result = 0u32;
        let mut bit = 0u32;
        let mut m = mask;
        while m != 0 {
            let low = m & m.wrapping_neg();
            if col & low != 0 {
                result |= 1u32 << bit;
            }
            bit += 1;
            m &= m - 1;
        }
        assert_eq!(gravity_col(col), result);
    }

    #[test]
    fn place_and_height() {
        let mut board = Board::empty();
        assert!(board.is_legal(3));
        assert!(board.place_disc(3, 4));
        assert_eq!(board.height(3), 1);
        assert_eq!(board.get(6, 3), 4);
        assert!(board.place_disc(3, 2));
        assert_eq!(board.get(5, 3), 2);
        let mut full = board_from_cols([0, 0, 0, 0x1111_1111 & COL_USED, 0, 0, 0]);
        // Column 3 has seven 1s: height 7, illegal.
        assert!(!full.is_legal(3));
        assert!(!full.place_disc(3, 1));
        full.cols[3] = COL_USED; // all 9s
        assert_eq!(full.height(3), 7);
    }

    #[test]
    fn rise_shifts_up_and_adds_solid_row() {
        let mut board = Board::empty();
        board.set(6, 0, 5);
        board.set(5, 0, 3);
        assert!(board.raise_covered_row());
        assert_eq!(board.get(5, 0), 5);
        assert_eq!(board.get(4, 0), 3);
        assert_eq!(board.get(6, 0), SOLID);
        // A column reaching the top row blocks the rise.
        let mut tall = Board::empty();
        for row in 0..BOARD_SIZE {
            tall.set(row, 2, 1);
        }
        assert!(!tall.raise_covered_row());
    }

    #[test]
    fn mirror_reverses_columns() {
        let mut board = Board::empty();
        board.set(6, 0, 5);
        board.set(6, 6, 2);
        let mirrored = board.mirrored();
        assert_eq!(mirrored.get(6, 6), 5);
        assert_eq!(mirrored.get(6, 0), 2);
    }

    #[test]
    fn scan_matches_cell_contents() {
        let mut board = Board::empty();
        board.set(6, 0, 1);
        board.set(6, 1, SOLID);
        board.set(5, 1, CRACKED);
        board.set(0, 6, 7);
        let scan = board.scan();
        assert_eq!(scan.occ.count_ones(), 4);
        assert_eq!(scan.numbered.count_ones(), 2);
        assert_eq!(scan.covered.count_ones(), 2);
        assert_eq!(scan.cracked.count_ones(), 1);
        assert_eq!(scan.col_mask[0].count_ones(), 1);
        assert_eq!(scan.col_mask[1].count_ones(), 2);
        assert_eq!(scan.row_mask[6].count_ones(), 2);
        assert_eq!(scan.row_mask[0].count_ones(), 1);
    }
}
