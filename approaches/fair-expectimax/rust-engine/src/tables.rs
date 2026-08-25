// Shared lookup tables.
//
// RUN_TABLE and REV7 are computed at compile time by const evaluation; the
// wave-score table reproduces floor(7 * d^2.5) bit-for-bit and is checked
// against the floating-point expression at every process start in the gates
// (and by unit test here).

/// Run length, run start and run end of every position of a 7-cell line, for
/// each of the 128 occupancy patterns.  Identical contents to the C++ fast
/// engine's kRunTable / kRunLengthTable.
#[derive(Clone, Copy)]
pub struct RunInfo {
    pub length: [u8; 7],
    pub start: [i8; 7],
    pub end: [i8; 7],
}

const fn build_run_table() -> [RunInfo; 128] {
    let mut table = [RunInfo {
        length: [0; 7],
        start: [-1; 7],
        end: [-1; 7],
    }; 128];
    let mut mask = 0usize;
    while mask < 128 {
        let mut cursor = 0usize;
        while cursor < 7 {
            if (mask >> cursor) & 1 == 0 {
                cursor += 1;
                continue;
            }
            let start = cursor;
            while cursor < 7 && (mask >> cursor) & 1 != 0 {
                cursor += 1;
            }
            let end = cursor - 1;
            let mut position = start;
            while position <= end {
                table[mask].length[position] = (end - start + 1) as u8;
                table[mask].start[position] = start as i8;
                table[mask].end[position] = end as i8;
                position += 1;
            }
        }
        mask += 1;
    }
    table
}

pub static RUN_TABLE: [RunInfo; 128] = build_run_table();

/// Reverse the low 7 bits of each byte value.
const fn build_rev7() -> [u8; 128] {
    let mut table = [0u8; 128];
    let mut mask = 0usize;
    while mask < 128 {
        let mut reversed = 0u8;
        let mut bit = 0usize;
        while bit < 7 {
            if (mask >> bit) & 1 != 0 {
                reversed |= 1 << (6 - bit);
            }
            bit += 1;
        }
        table[mask] = reversed;
        mask += 1;
    }
    table
}

pub static REV7: [u8; 128] = build_rev7();

/// floor(7 * depth^2.5) for depth 1..1024, sized far above anything reachable
/// (a single move can produce at most 49 discs' worth of waves plus the rise
/// continuation).  Built once per process; the gate verifies every entry
/// against the floating-point expression.
pub const WAVE_TABLE_SIZE: usize = 1024;

use std::sync::OnceLock;

static WAVE_SCORES: OnceLock<[i64; WAVE_TABLE_SIZE]> = OnceLock::new();

pub fn wave_score_table() -> &'static [i64; WAVE_TABLE_SIZE] {
    WAVE_SCORES.get_or_init(|| {
        let mut values = [0i64; WAVE_TABLE_SIZE];
        for depth in 1..WAVE_TABLE_SIZE {
            values[depth] = (7.0f64 * (depth as f64).powf(2.5)).floor() as i64;
        }
        values
    })
}

#[inline]
pub fn wave_score(depth: i32) -> i64 {
    if depth < 1 {
        panic!("chain depth must be positive");
    }
    let table = wave_score_table();
    if (depth as usize) < WAVE_TABLE_SIZE {
        table[depth as usize]
    } else {
        (7.0f64 * (depth as f64).powf(2.5)).floor() as i64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wave_scores_match_float_expression() {
        for depth in 1..1024 {
            let expected = (7.0f64 * (depth as f64).powf(2.5)).floor() as i64;
            assert_eq!(wave_score(depth), expected);
        }
        assert_eq!(wave_score(1), 7);
        assert_eq!(wave_score(2), 39);
    }

    #[test]
    fn rev7_reverses() {
        assert_eq!(REV7[0b0000001], 0b1000000);
        assert_eq!(REV7[0b1000000], 0b0000001);
        assert_eq!(REV7[0b1010101], 0b1010101);
        assert_eq!(REV7[0b0111111], 0b1111110);
    }

    #[test]
    fn run_table_spans() {
        // Mask 0b0101110: runs at columns 1..=3 and column 5.
        let info = &RUN_TABLE[0b0101110];
        assert_eq!(info.length[1], 3);
        assert_eq!(info.length[2], 3);
        assert_eq!(info.length[3], 3);
        assert_eq!(info.start[1], 1);
        assert_eq!(info.end[3], 3);
        assert_eq!(info.length[5], 1);
        assert_eq!(info.length[0], 0);
    }
}
