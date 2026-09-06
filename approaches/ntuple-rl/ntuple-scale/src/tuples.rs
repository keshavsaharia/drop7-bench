// Pattern indices straight from the packed column words.
//
// The engine stores the board as seven u32 column words, four bits per cell,
// nibble n = row (6 - n) (drop7_rs::board, contract R1).  A cell holds one of
// ten values (0 empty, 1..=7 numbered, 8 solid, 9 cracked), so a seven-cell
// line has exactly 10^7 distinct patterns.  A column tuple's pattern index is
// therefore a function of its column word alone, and the conversion from the
// base-16 nibble packing to a dense base-10 index is two table lookups: the
// low four nibbles (16 bits) through a 65,536-entry table and the high three
// nibbles (12 bits) through a 4,096-entry table.  Both tables live in L2 and
// nothing is unpacked to bytes.
//
// Row tuples need the seven cells of one row, one nibble from each column
// word; row_words gathers them into seven row words of the same nibble layout
// (nibble c = column c) so the same line codec applies.  Window tuples read
// two, three or four consecutive nibbles from adjacent column words and use
// the small codecs directly (the four-nibble codec is the line codec's own
// low table).

use drop7_rs::board::BOARD_SIZE;

pub const CELL_VALUES: u32 = 10;
/// 10^7: patterns of one seven-cell line.
pub const LINE_PATTERNS: usize = 10_000_000;
/// 10^6: patterns of one six-cell window.
pub const WINDOW_PATTERNS: usize = 1_000_000;
/// 10^8: patterns of one eight-cell window (2x4 or 4x2).
pub const WIDE_WINDOW_PATTERNS: usize = 100_000_000;

/// Base-10 index of the low `nibbles` nibbles of `word`, nibble 0 as the
/// least-significant digit.  The reference the codec tables are checked
/// against.
pub fn base10_ref(word: u32, nibbles: u32) -> u32 {
    let mut result = 0u32;
    let mut scale = 1u32;
    for n in 0..nibbles {
        let digit = (word >> (4 * n)) & 0xF;
        debug_assert!(digit < CELL_VALUES);
        result += digit * scale;
        scale *= CELL_VALUES;
    }
    result
}

fn nibbles_valid(word: u32, nibbles: u32) -> bool {
    (0..nibbles).all(|n| ((word >> (4 * n)) & 0xF) < CELL_VALUES)
}

pub struct Codec {
    lo: Vec<u16>,
    hi: Vec<u16>,
    b2: [u16; 256],
}

impl Default for Codec {
    fn default() -> Self {
        Self::new()
    }
}

impl Codec {
    pub fn new() -> Codec {
        let mut lo = vec![u16::MAX; 1 << 16];
        for word in 0..(1u32 << 16) {
            if nibbles_valid(word, 4) {
                lo[word as usize] = base10_ref(word, 4) as u16;
            }
        }
        let mut hi = vec![u16::MAX; 1 << 12];
        for word in 0..(1u32 << 12) {
            if nibbles_valid(word, 3) {
                hi[word as usize] = base10_ref(word, 3) as u16;
            }
        }
        let mut b2 = [u16::MAX; 256];
        for word in 0..256u32 {
            if nibbles_valid(word, 2) {
                b2[word as usize] = base10_ref(word, 2) as u16;
            }
        }
        Codec { lo, hi, b2 }
    }

    /// Index of a seven-nibble line word in 0..LINE_PATTERNS.
    #[inline(always)]
    pub fn line(&self, word: u32) -> u32 {
        debug_assert!(nibbles_valid(word, 7));
        self.lo[(word & 0xFFFF) as usize] as u32
            + self.hi[((word >> 16) & 0xFFF) as usize] as u32 * 10_000
    }

    /// Index of three nibbles in 0..1000.
    #[inline(always)]
    pub fn nib3(&self, bits: u32) -> u32 {
        self.hi[(bits & 0xFFF) as usize] as u32
    }

    /// Index of two nibbles in 0..100.
    #[inline(always)]
    pub fn nib2(&self, bits: u32) -> u32 {
        self.b2[(bits & 0xFF) as usize] as u32
    }

    /// Index of four nibbles in 0..10_000.
    #[inline(always)]
    pub fn nib4(&self, bits: u32) -> u32 {
        self.lo[(bits & 0xFFFF) as usize] as u32
    }
}

/// Gather the seven row words from the seven column words.  Row word r has
/// nibble c = cell(row r, column c); column word c has nibble n = row (6-n).
#[inline]
pub fn row_words(cols: &[u32; BOARD_SIZE]) -> [u32; BOARD_SIZE] {
    let mut rows = [0u32; BOARD_SIZE];
    for (c, &word) in cols.iter().enumerate() {
        let shift = 4 * c as u32;
        // nibble n of the column word belongs to row 6 - n.
        rows[6] |= (word & 0xF) << shift;
        rows[5] |= ((word >> 4) & 0xF) << shift;
        rows[4] |= ((word >> 8) & 0xF) << shift;
        rows[3] |= ((word >> 12) & 0xF) << shift;
        rows[2] |= ((word >> 16) & 0xF) << shift;
        rows[1] |= ((word >> 20) & 0xF) << shift;
        rows[0] |= ((word >> 24) & 0xF) << shift;
    }
    rows
}

/// Reference gather through the engine's cell accessor, for the gate.
pub fn row_words_ref(board: &drop7_rs::board::Board) -> [u32; BOARD_SIZE] {
    let mut rows = [0u32; BOARD_SIZE];
    for row in 0..BOARD_SIZE {
        for col in 0..BOARD_SIZE {
            rows[row] |= (board.get(row, col) as u32) << (4 * col);
        }
    }
    rows
}

/// Window placements.  A 2-wide x 3-tall window at (column c, top row r)
/// covers rows r..r+3 of columns c and c+1: 6 x 5 = 30 placements.  A 3-wide x
/// 2-tall window at (c, r) covers rows r..r+2 of columns c..c+3: 5 x 6 = 30.
pub const WIN23_PLACEMENTS: usize = 30;
pub const WIN32_PLACEMENTS: usize = 30;
/// A 2-wide x 4-tall window at (c, r) covers rows r..r+4 of columns c and
/// c+1: 6 x 4 = 24 placements.  A 4-wide x 2-tall window at (c, r) covers
/// rows r..r+2 of columns c..c+4: 4 x 6 = 24 placements.
pub const WIN24_PLACEMENTS: usize = 24;
pub const WIN42_PLACEMENTS: usize = 24;

/// Three consecutive nibbles of a column word for rows r..r+3 (r in 0..=4):
/// rows r, r+1, r+2 are nibbles 6-r, 5-r, 4-r, so the chunk starts at nibble
/// 4-r.  The chunk's least-significant nibble is the lowest row (r+2).
#[inline(always)]
pub fn chunk3(word: u32, top_row: usize) -> u32 {
    (word >> (4 * (4 - top_row))) & 0xFFF
}

/// Two consecutive nibbles for rows r..r+2 (r in 0..=5): nibbles 6-r, 5-r.
#[inline(always)]
pub fn chunk2(word: u32, top_row: usize) -> u32 {
    (word >> (4 * (5 - top_row))) & 0xFF
}

/// Four consecutive nibbles for rows r..r+4 (r in 0..=3): nibbles 6-r down
/// to 3-r, so the chunk starts at nibble 3-r; the least-significant nibble
/// is the lowest row (r+3).
#[inline(always)]
pub fn chunk4(word: u32, top_row: usize) -> u32 {
    (word >> (4 * (3 - top_row))) & 0xFFFF
}

#[cfg(test)]
mod tests {
    use super::*;
    use drop7_rs::board::Board;
    use drop7_rs::rng::Mulberry32;

    fn random_valid_word(rng: &mut Mulberry32) -> u32 {
        let mut word = 0u32;
        for n in 0..7 {
            let digit = (rng.next_bits() % 10) as u32;
            word |= digit << (4 * n);
        }
        word
    }

    #[test]
    fn codec_matches_horner_on_random_lines() {
        let codec = Codec::new();
        let mut rng = Mulberry32::new(0x5eed_0001);
        for _ in 0..200_000 {
            let word = random_valid_word(&mut rng);
            assert_eq!(codec.line(word), base10_ref(word, 7));
            assert_eq!(codec.nib3(word & 0xFFF), base10_ref(word & 0xFFF, 3));
            assert_eq!(codec.nib2(word & 0xFF), base10_ref(word & 0xFF, 2));
            assert_eq!(codec.nib4(word & 0xFFFF), base10_ref(word & 0xFFFF, 4));
        }
        assert_eq!(codec.line(0), 0);
        assert_eq!(codec.line(0x0999_9999), LINE_PATTERNS as u32 - 1);
    }

    #[test]
    fn row_words_match_cell_accessor() {
        let mut rng = Mulberry32::new(0x5eed_0002);
        for _ in 0..10_000 {
            let mut cols = [0u32; 7];
            for word in cols.iter_mut() {
                *word = random_valid_word(&mut rng);
            }
            let board = Board { cols };
            assert_eq!(row_words(&cols), row_words_ref(&board));
        }
    }

    #[test]
    fn chunks_read_the_named_rows() {
        let mut rng = Mulberry32::new(0x5eed_0003);
        for _ in 0..1000 {
            let word = random_valid_word(&mut rng);
            let board = Board { cols: [word; 7] };
            for top in 0..=4 {
                let chunk = chunk3(word, top);
                // least-significant nibble = lowest row = top + 2
                assert_eq!(chunk & 0xF, board.get(top + 2, 0) as u32);
                assert_eq!((chunk >> 4) & 0xF, board.get(top + 1, 0) as u32);
                assert_eq!((chunk >> 8) & 0xF, board.get(top, 0) as u32);
            }
            for top in 0..=5 {
                let chunk = chunk2(word, top);
                assert_eq!(chunk & 0xF, board.get(top + 1, 0) as u32);
                assert_eq!((chunk >> 4) & 0xF, board.get(top, 0) as u32);
            }
            for top in 0..=3 {
                let chunk = chunk4(word, top);
                assert_eq!(chunk & 0xF, board.get(top + 3, 0) as u32);
                assert_eq!((chunk >> 4) & 0xF, board.get(top + 2, 0) as u32);
                assert_eq!((chunk >> 8) & 0xF, board.get(top + 1, 0) as u32);
                assert_eq!((chunk >> 12) & 0xF, board.get(top, 0) as u32);
            }
        }
    }
}
