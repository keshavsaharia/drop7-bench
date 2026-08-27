// Bit-exact port of approaches/lifetime-objective/fast-engine/fast-leaf.hpp
// (itself proven bit-identical to the frozen reference fairLeaf by gate-leaf).
// Every floating-point expression is copied in the same order with the same
// accumulator types; nothing is re-associated.  The gate compares uint64 bit
// patterns of the returned values against the C++ build over real boards.

use crate::board::{BOARD_SIZE, CELL_COUNT, CRACKED, EMPTY, SOLID};
use crate::engine::State;
use crate::tables::RUN_TABLE;

pub mod weights {
    pub const FAIR_TERMINAL_UTILITY: f64 = -2_500_000.0;
    pub const OPEN_COLUMNS: f64 = 180.0;
    pub const HEIGHT_LOAD: f64 = -20.0;
    pub const SOLID_CELLS: f64 = -620.0;
    pub const CRACKED_CELLS: f64 = -220.0;
    pub const NUMBERED_CELLS: f64 = -18.0;
    pub const HIGH_LOW_NUMBERS: f64 = -90.0;
    pub const DIRECT_POTENTIAL: f64 = 1_600.0;
    pub const LATENT_CHAIN_POTENTIAL: f64 = 700.0;
    pub const CRACKED_EXPOSURE: f64 = 100.0;
    pub const SOLID_EXPOSURE: f64 = 40.0;
    pub const ADJACENT_ONES: f64 = -550.0;
    pub const TRIPLE_TWOS: f64 = -750.0;
    pub const DEAD_LOW_NUMBERS: f64 = -120.0;
    pub const COVERED_HEIGHT_RISK: f64 = -95.0;
    pub const LOW_NUMBER_HEIGHT_RISK: f64 = -85.0;
    pub const DANGER_HEIGHT_SQUARED: f64 = -1_250.0;
    // kRoughnessWeight is 0.0 in the reference; the term is removed (L7).
    pub const RISE_PRESSURE: f64 = -35.0;
    pub const NEXT_DISC_VERTICAL_OPTIONS: f64 = 220.0;
}

pub const LEAF_TERM_NAMES: [&str; 18] = [
    "open_columns",
    "height_load",
    "solid_cells",
    "cracked_cells",
    "numbered_cells",
    "high_low_numbers",
    "direct_potential",
    "latent_chain_potential",
    "cracked_exposure",
    "solid_exposure",
    "adjacent_ones",
    "triple_twos",
    "dead_low_numbers",
    "covered_height_risk",
    "low_number_height_risk",
    "danger_height_squared",
    "rise_pressure",
    "next_disc_vertical_options",
];

pub const FROZEN_LEAF_WEIGHTS: [f64; 18] = [
    weights::OPEN_COLUMNS,
    weights::HEIGHT_LOAD,
    weights::SOLID_CELLS,
    weights::CRACKED_CELLS,
    weights::NUMBERED_CELLS,
    weights::HIGH_LOW_NUMBERS,
    weights::DIRECT_POTENTIAL,
    weights::LATENT_CHAIN_POTENTIAL,
    weights::CRACKED_EXPOSURE,
    weights::SOLID_EXPOSURE,
    weights::ADJACENT_ONES,
    weights::TRIPLE_TWOS,
    weights::DEAD_LOW_NUMBERS,
    weights::COVERED_HEIGHT_RISK,
    weights::LOW_NUMBER_HEIGHT_RISK,
    weights::DANGER_HEIGHT_SQUARED,
    weights::RISE_PRESSURE,
    weights::NEXT_DISC_VERTICAL_OPTIONS,
];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LeafWeights {
    pub values: [f64; 18],
}

impl LeafWeights {
    pub const fn frozen() -> Self {
        Self {
            values: FROZEN_LEAF_WEIGHTS,
        }
    }

    pub fn is_frozen(&self) -> bool {
        self.values
            .iter()
            .zip(FROZEN_LEAF_WEIGHTS.iter())
            .all(|(left, right)| left.to_bits() == right.to_bits())
    }

    /// Read the same strict "name value" format used by the C++ weighted
    /// leaf. Every one of the eighteen terms must occur exactly once.
    pub fn read_named(path: &str) -> Result<Self, String> {
        let text = std::fs::read_to_string(path)
            .map_err(|error| format!("cannot read leaf weights {path}: {error}"))?;
        let mut result = Self::frozen();
        let mut seen = [false; 18];
        for (line_number, source) in text.lines().enumerate() {
            let line = source.split('#').next().unwrap_or("").trim();
            if line.is_empty() {
                continue;
            }
            let fields: Vec<&str> = line.split_whitespace().collect();
            if fields.len() != 2 {
                return Err(format!(
                    "{path}:{}: expected one leaf name and one value",
                    line_number + 1
                ));
            }
            let Some(index) = LEAF_TERM_NAMES.iter().position(|name| *name == fields[0]) else {
                return Err(format!(
                    "{path}:{}: unknown leaf term {}",
                    line_number + 1,
                    fields[0]
                ));
            };
            if seen[index] {
                return Err(format!(
                    "{path}:{}: duplicate leaf term {}",
                    line_number + 1,
                    fields[0]
                ));
            }
            let value: f64 = fields[1].parse().map_err(|_| {
                format!(
                    "{path}:{}: invalid value for {}",
                    line_number + 1,
                    fields[0]
                )
            })?;
            if !value.is_finite() {
                return Err(format!(
                    "{path}:{}: non-finite value for {}",
                    line_number + 1,
                    fields[0]
                ));
            }
            result.values[index] = value;
            seen[index] = true;
        }
        if let Some(index) = seen.iter().position(|present| !present) {
            return Err(format!(
                "{path}: missing leaf term {}",
                LEAF_TERM_NAMES[index]
            ));
        }
        Ok(result)
    }
}

impl Default for LeafWeights {
    fn default() -> Self {
        Self::frozen()
    }
}

const READINESS_TABLE_SIZE: i32 = 80;

/// ldexp(1.0, 1 - cost): exact powers of two.  Table lookup in the reachable
/// range; beyond it an exact repeated halving (never taken in practice, kept
/// for total correctness).
#[inline]
fn readiness(cost: i32) -> f64 {
    if cost < 1 {
        return 0.0;
    }
    if cost < READINESS_TABLE_SIZE {
        // 2^(1-cost) for cost in 1..80: exponent field 1023+1-cost is normal.
        return f64::from_bits(((1023 + 1 - cost) as u64) << 52);
    }
    let mut value = 1.0f64;
    for _ in 1..cost {
        value *= 0.5;
    }
    value
}

#[inline(always)]
fn union_readiness(first: f64, second: f64) -> f64 {
    1.0 - (1.0 - first) * (1.0 - second)
}

/// minimumHorizontalAdditionCostFast: integer arithmetic only, identical to
/// the C++ difference-of-prefix-sums search.
#[allow(clippy::too_many_arguments)]
fn minimum_horizontal_addition_cost(
    value: i32,
    segment_start: i32,
    segment_end: i32,
    segment_length: i32,
    heights: &[i32; BOARD_SIZE],
    elevation: i32,
    prefix: &[i32; BOARD_SIZE + 1],
) -> f64 {
    if segment_start < 0 || value <= segment_length {
        return -1.0;
    }
    let mut best = i32::MAX;
    let lowest = (segment_end - value + 1).max(0);
    let highest = (segment_start).min(BOARD_SIZE as i32 - value);
    let mut start = lowest;
    while start <= highest {
        let end = start + value - 1;
        if start > 0 && heights[(start - 1) as usize] >= elevation {
            start += 1;
            continue;
        }
        if (end + 1) < BOARD_SIZE as i32 && heights[(end + 1) as usize] >= elevation {
            start += 1;
            continue;
        }
        let cost = prefix[(end + 1) as usize] - prefix[start as usize];
        if cost > 0 {
            best = best.min(cost);
        }
        start += 1;
    }
    if best == i32::MAX {
        -1.0
    } else {
        best as f64
    }
}

/// Descending insertion sort over at most seven doubles; the reference reads
/// one order statistic of the sorted values, a property of the multiset.
fn sort_descending(values: &mut [f64]) {
    for index in 1..values.len() {
        let key = values[index];
        let mut position = index;
        while position > 0 && values[position - 1] < key {
            values[position] = values[position - 1];
            position -= 1;
        }
        values[position] = key;
    }
}

fn release_readiness(excess: i32, support: &mut [f64], count: usize) -> f64 {
    if excess <= 0 || (count as i32) < excess {
        return 0.0;
    }
    sort_descending(&mut support[..count]);
    support[(excess - 1) as usize] * readiness(excess)
}

/// Per-leaf scratch buffers (stack/struct allocated once per searcher).
pub struct LeafScratch {
    pub heights: [i32; BOARD_SIZE],
    pub row_mask: [u8; BOARD_SIZE],
    pub column_mask: [u8; BOARD_SIZE],
    pub twos_row: [u8; BOARD_SIZE],
    pub twos_column: [u8; BOARD_SIZE],
    pub present_bits: u64,
    pub cover_bits: u64,
    pub ones_bits: u64,
    pub addition: [f64; CELL_COUNT],
    pub release: [f64; CELL_COUNT],
    pub horizontal_addition: [f64; CELL_COUNT],
    pub vertical_addition: [f64; CELL_COUNT],
    pub horizontal_release: [f64; CELL_COUNT],
    pub vertical_release: [f64; CELL_COUNT],
}

impl Default for LeafScratch {
    fn default() -> Self {
        LeafScratch {
            heights: [0; BOARD_SIZE],
            row_mask: [0; BOARD_SIZE],
            column_mask: [0; BOARD_SIZE],
            twos_row: [0; BOARD_SIZE],
            twos_column: [0; BOARD_SIZE],
            present_bits: 0,
            cover_bits: 0,
            ones_bits: 0,
            addition: [0.0; CELL_COUNT],
            release: [0.0; CELL_COUNT],
            horizontal_addition: [0.0; CELL_COUNT],
            vertical_addition: [0.0; CELL_COUNT],
            horizontal_release: [0.0; CELL_COUNT],
            vertical_release: [0.0; CELL_COUNT],
        }
    }
}

impl LeafScratch {
    #[inline(always)]
    fn present(&self, index: usize) -> bool {
        (self.present_bits >> index) & 1 != 0
    }
}

#[derive(Default)]
struct Features {
    open_columns: i32,
    height_load: f64,
    solid_cells: i32,
    cracked_cells: i32,
    numbered_cells: i32,
    high_low_numbers: i32,
    direct_potential: f64,
    latent_chain_potential: f64,
    cracked_exposure: f64,
    solid_exposure: f64,
    adjacent_ones: f64,
    triple_twos: f64,
    dead_low_numbers: f64,
    covered_height_risk: f64,
    low_number_height_risk: f64,
    danger_height_squared: f64,
    rise_pressure: f64,
    next_disc_vertical_options: f64,
}

/// The runtime-weighted fair leaf. At `LeafWeights::frozen()` this preserves
/// the exact operation order and is bit-identical to fastFairLeaf.
pub fn weighted_fair_leaf(
    state: &State,
    scratch: &mut LeafScratch,
    leaf_weights: &LeafWeights,
) -> f64 {
    if state.game_over {
        return weights::FAIR_TERMINAL_UTILITY;
    }
    // The leaf reads cells many times in row-major order; unpack the packed
    // column words to the byte view once (7 PDEPs) so every read below is a
    // direct byte load, the same access cost the C++ leaf pays.
    let cells = state.board.to_bytes();
    let board = &state.board;
    let mut f = Features::default();

    // --- stage 1: occupancy masks, heights, open columns -------------------
    // The packed representation yields the masks directly (measured faster
    // than a byte sweep here); contents are the same as the C++ row sweep.
    let (row_mask, col_mask) = board.scan_masks();
    scratch.row_mask = row_mask;
    scratch.column_mask = col_mask;
    let mut maximum_height = 0i32;
    for column in 0..BOARD_SIZE {
        let height = scratch.column_mask[column].count_ones() as i32;
        scratch.heights[column] = height;
        if cells[column] == EMPTY {
            f.open_columns += 1;
        }
        maximum_height = maximum_height.max(height);
        f.rise_pressure += ((height * height * height) as f64) / state.moves_remaining as f64;
        if height < BOARD_SIZE as i32 && height + 1 == state.next_disc as i32 {
            f.next_disc_vertical_options += 1.0;
        }
    }
    let danger = (maximum_height - 4).max(0);
    f.danger_height_squared = (danger * danger) as f64;
    scratch.present_bits = 0;
    scratch.cover_bits = 0;
    scratch.ones_bits = 0;
    scratch.twos_row = [0; BOARD_SIZE];
    scratch.twos_column = [0; BOARD_SIZE];

    // --- stage 2: per-cell sweep -------------------------------------------
    for row in 0..BOARD_SIZE {
        let elevation = (BOARD_SIZE - row) as i32;
        let mut prefix = [0i32; BOARD_SIZE + 1];
        for column in 0..BOARD_SIZE {
            prefix[column + 1] = prefix[column] + (elevation - scratch.heights[column]).max(0);
        }
        let horizontal = &RUN_TABLE[scratch.row_mask[row] as usize];
        for column in 0..BOARD_SIZE {
            let index = row * BOARD_SIZE + column;
            let cell = cells[row * BOARD_SIZE + column];
            if cell == EMPTY {
                continue;
            }
            f.height_load += (elevation * elevation) as f64;
            let edge_multiplier = if column == 0 || column == BOARD_SIZE - 1 {
                1.65
            } else {
                1.0
            };
            if cell == SOLID || cell == CRACKED {
                if cell == SOLID {
                    f.solid_cells += 1;
                    f.covered_height_risk += (elevation * elevation) as f64 * edge_multiplier;
                } else {
                    f.cracked_cells += 1;
                    f.covered_height_risk +=
                        (elevation * elevation) as f64 * edge_multiplier * 0.72;
                }
                scratch.cover_bits |= 1u64 << index;
                continue;
            }
            if !(1..=7).contains(&cell) {
                continue;
            }
            f.numbered_cells += 1;
            if cell <= 2 {
                let height_risk = (elevation - 2).max(0);
                f.low_number_height_risk += (height_risk * height_risk) as f64;
                if elevation >= 5 {
                    f.high_low_numbers += 1;
                }
            }
            scratch.present_bits |= 1u64 << index;
            if cell == 1 {
                scratch.ones_bits |= 1u64 << index;
            } else if cell == 2 {
                scratch.twos_row[row] |= 1u8 << column;
                scratch.twos_column[column] |= 1u8 << row;
            }
            let height = scratch.heights[column];
            let vertical_addition = if cell as i32 > height {
                readiness(cell as i32 - height)
            } else {
                0.0
            };
            let horizontal_cost = minimum_horizontal_addition_cost(
                cell as i32,
                horizontal.start[column] as i32,
                horizontal.end[column] as i32,
                horizontal.length[column] as i32,
                &scratch.heights,
                elevation,
                &prefix,
            );
            let horizontal_addition = if horizontal_cost < 0.0 {
                0.0
            } else {
                readiness(horizontal_cost as i32)
            };
            let addition = union_readiness(horizontal_addition, vertical_addition);
            scratch.vertical_addition[index] = vertical_addition;
            scratch.horizontal_addition[index] = horizontal_addition;
            scratch.addition[index] = addition;
            f.direct_potential += addition;
        }
    }

    // --- stage 3: release inventory ----------------------------------------
    let mut remaining = scratch.present_bits;
    while remaining != 0 {
        let index = remaining.trailing_zeros() as usize;
        remaining &= remaining - 1;
        let row = index / BOARD_SIZE;
        let column = index % BOARD_SIZE;
        let horizontal = &RUN_TABLE[scratch.row_mask[row] as usize];
        let vertical = &RUN_TABLE[scratch.column_mask[column] as usize];
        let mut horizontal_support = [0.0f64; BOARD_SIZE];
        let mut vertical_support = [0.0f64; BOARD_SIZE];
        let mut horizontal_count = 0usize;
        let mut vertical_count = 0usize;
        let mut scan_col = horizontal.start[column] as i32;
        while scan_col <= horizontal.end[column] as i32 {
            let supporter = row * BOARD_SIZE + scan_col as usize;
            if supporter != index && scratch.present(supporter) {
                horizontal_support[horizontal_count] = scratch.addition[supporter];
                horizontal_count += 1;
            }
            scan_col += 1;
        }
        let mut scan_row = vertical.start[row] as i32;
        while scan_row <= vertical.end[row] as i32 {
            let supporter = scan_row as usize * BOARD_SIZE + column;
            if supporter != index && scratch.present(supporter) {
                vertical_support[vertical_count] = scratch.addition[supporter];
                vertical_count += 1;
            }
            scan_row += 1;
        }
        let value = cells[index] as i32;
        let horizontal_release = release_readiness(
            horizontal.length[column] as i32 - value,
            &mut horizontal_support,
            horizontal_count,
        );
        let vertical_release = release_readiness(
            vertical.length[row] as i32 - value,
            &mut vertical_support,
            vertical_count,
        );
        let release = union_readiness(horizontal_release, vertical_release);
        scratch.horizontal_release[index] = horizontal_release;
        scratch.vertical_release[index] = vertical_release;
        scratch.release[index] = release;
        f.latent_chain_potential += release;
        if value <= 2
            && horizontal.length[column] as i32 > value
            && vertical.length[row] as i32 > value
        {
            f.dead_low_numbers += 1.0 - union_readiness(scratch.addition[index], release);
        }
    }

    // --- stage 4: adjacent ones --------------------------------------------
    let mut remaining = scratch.ones_bits;
    while remaining != 0 {
        let index = remaining.trailing_zeros() as usize;
        remaining &= remaining - 1;
        let row = index / BOARD_SIZE;
        let column = index % BOARD_SIZE;
        if column + 1 < BOARD_SIZE && cells[index + 1] == 1 {
            let escape = union_readiness(
                scratch.vertical_addition[index],
                scratch.vertical_release[index],
            )
            .max(union_readiness(
                scratch.vertical_addition[index + 1],
                scratch.vertical_release[index + 1],
            ));
            f.adjacent_ones += 1.0 - escape;
        }
        if row + 1 < BOARD_SIZE && cells[index + BOARD_SIZE] == 1 {
            let escape = union_readiness(
                scratch.horizontal_addition[index],
                scratch.horizontal_release[index],
            )
            .max(union_readiness(
                scratch.horizontal_addition[index + BOARD_SIZE],
                scratch.horizontal_release[index + BOARD_SIZE],
            ));
            f.adjacent_ones += 1.0 - escape;
        }
    }

    // --- stage 5: runs of twos ----------------------------------------------
    for row in 0..BOARD_SIZE {
        let mut mask = scratch.twos_row[row];
        while mask != 0 {
            let start = mask.trailing_zeros() as usize;
            let mut run = mask >> start;
            let mut length = 0usize;
            while run & 1 != 0 {
                length += 1;
                run >>= 1;
            }
            let excess = length as i32 - 2;
            if excess > 0 {
                let mut escape = 0.0f64;
                for column in start..start + length {
                    let slot = row * BOARD_SIZE + column;
                    escape = escape.max(union_readiness(
                        scratch.vertical_addition[slot],
                        scratch.vertical_release[slot],
                    ));
                }
                f.triple_twos += (excess * excess) as f64 * (1.0 - escape);
            }
            mask &= !(((1u8 << length) - 1) << start);
        }
    }
    for column in 0..BOARD_SIZE {
        let mut mask = scratch.twos_column[column];
        while mask != 0 {
            let start = mask.trailing_zeros() as usize;
            let mut run = mask >> start;
            let mut length = 0usize;
            while run & 1 != 0 {
                length += 1;
                run >>= 1;
            }
            let excess = length as i32 - 2;
            if excess > 0 {
                let mut escape = 0.0f64;
                for row in start..start + length {
                    let slot = row * BOARD_SIZE + column;
                    escape = escape.max(union_readiness(
                        scratch.horizontal_addition[slot],
                        scratch.horizontal_release[slot],
                    ));
                }
                f.triple_twos += (excess * excess) as f64 * (1.0 - escape);
            }
            mask &= !(((1u8 << length) - 1) << start);
        }
    }

    // --- stage 6: cover exposure --------------------------------------------
    let mut remaining = scratch.cover_bits;
    while remaining != 0 {
        let index = remaining.trailing_zeros() as usize;
        remaining &= remaining - 1;
        let row = index / BOARD_SIZE;
        let column = index % BOARD_SIZE;
        let mut support = [0.0f64; 4];
        let mut count = 0usize;
        // Neighbour order {-1,0},{1,0},{0,-1},{0,1} as in the reference.
        if row > 0 && scratch.present(index - BOARD_SIZE) {
            support[count] = union_readiness(
                scratch.addition[index - BOARD_SIZE],
                scratch.release[index - BOARD_SIZE],
            );
            count += 1;
        }
        if row + 1 < BOARD_SIZE && scratch.present(index + BOARD_SIZE) {
            support[count] = union_readiness(
                scratch.addition[index + BOARD_SIZE],
                scratch.release[index + BOARD_SIZE],
            );
            count += 1;
        }
        if column > 0 && scratch.present(index - 1) {
            support[count] =
                union_readiness(scratch.addition[index - 1], scratch.release[index - 1]);
            count += 1;
        }
        if column + 1 < BOARD_SIZE && scratch.present(index + 1) {
            support[count] =
                union_readiness(scratch.addition[index + 1], scratch.release[index + 1]);
            count += 1;
        }
        sort_descending(&mut support[..count]);
        if cells[index] == CRACKED {
            let mut inverse = 1.0f64;
            for item in support.iter().take(count) {
                inverse *= 1.0 - item;
            }
            f.cracked_exposure += 1.0 - inverse;
        } else {
            f.solid_exposure += (if count > 0 { support[0] * 0.35 } else { 0.0 })
                + (if count > 1 { support[1] * 0.65 } else { 0.0 });
        }
    }

    // The dot product, in the reference's order (L7: the roughness term is
    // exactly +0.0 and is removed, as proven in the C++ header).
    let mut result = 0.0f64;
    if leaf_weights.is_frozen() {
        // Preserve the original literal-constant code path. Besides proving
        // parity, this prevents loading the runtime array from changing
        // contraction/register choices in the frozen reference evaluator.
        result += weights::OPEN_COLUMNS * f.open_columns as f64;
        result += weights::HEIGHT_LOAD * f.height_load;
        result += weights::SOLID_CELLS * f.solid_cells as f64;
        result += weights::CRACKED_CELLS * f.cracked_cells as f64;
        result += weights::NUMBERED_CELLS * f.numbered_cells as f64;
        result += weights::HIGH_LOW_NUMBERS * f.high_low_numbers as f64;
        result += weights::DIRECT_POTENTIAL * f.direct_potential;
        result += weights::LATENT_CHAIN_POTENTIAL * f.latent_chain_potential;
        result += weights::CRACKED_EXPOSURE * f.cracked_exposure;
        result += weights::SOLID_EXPOSURE * f.solid_exposure;
        result += weights::ADJACENT_ONES * f.adjacent_ones;
        result += weights::TRIPLE_TWOS * f.triple_twos;
        result += weights::DEAD_LOW_NUMBERS * f.dead_low_numbers;
        result += weights::COVERED_HEIGHT_RISK * f.covered_height_risk;
        result += weights::LOW_NUMBER_HEIGHT_RISK * f.low_number_height_risk;
        result += weights::DANGER_HEIGHT_SQUARED * f.danger_height_squared;
        result += weights::RISE_PRESSURE * f.rise_pressure;
        result += weights::NEXT_DISC_VERTICAL_OPTIONS * f.next_disc_vertical_options;
    } else {
        result += leaf_weights.values[0] * f.open_columns as f64;
        result += leaf_weights.values[1] * f.height_load;
        result += leaf_weights.values[2] * f.solid_cells as f64;
        result += leaf_weights.values[3] * f.cracked_cells as f64;
        result += leaf_weights.values[4] * f.numbered_cells as f64;
        result += leaf_weights.values[5] * f.high_low_numbers as f64;
        result += leaf_weights.values[6] * f.direct_potential;
        result += leaf_weights.values[7] * f.latent_chain_potential;
        result += leaf_weights.values[8] * f.cracked_exposure;
        result += leaf_weights.values[9] * f.solid_exposure;
        result += leaf_weights.values[10] * f.adjacent_ones;
        result += leaf_weights.values[11] * f.triple_twos;
        result += leaf_weights.values[12] * f.dead_low_numbers;
        result += leaf_weights.values[13] * f.covered_height_risk;
        result += leaf_weights.values[14] * f.low_number_height_risk;
        result += leaf_weights.values[15] * f.danger_height_squared;
        result += leaf_weights.values[16] * f.rise_pressure;
        result += leaf_weights.values[17] * f.next_disc_vertical_options;
    }
    result
}

/// The frozen fair leaf evaluator. Bit-identical to
/// drop7::fast::fastFairLeaf.
pub fn fair_leaf(state: &State, scratch: &mut LeafScratch) -> f64 {
    weighted_fair_leaf(state, scratch, &LeafWeights::frozen())
}
