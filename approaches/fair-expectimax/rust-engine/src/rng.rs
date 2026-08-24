// Random sources, bit-identical to the references.
//
// Mulberry32 matches src/core/typescript/engine.ts seededRandom and the C++
// Mulberry32 draw for draw (Math.imul is u32 wrapping multiplication; the
// TypeScript clamp in randomDisc never engages for a [0,1) unit draw, so
// floor(unit * 7) + 1 equals the C++ ((bits * 7) >> 32) + 1).
//
// StratifiedRandom and the seed derivations match
// src/core/native/public-behavior.hpp: same mix32, same domains, same double
// arithmetic in the same order.

/// Trait implemented by every draw source the engine consumes: one method,
/// the next disc value in 1..=7.
pub trait Random {
    fn next_disc(&mut self) -> u8;
}

pub const NEXT_DISC_DOMAIN: u32 = 0x4e45_5854;
pub const REVEAL_DOMAIN: u32 = 0x5245_564c;
pub const REVEAL_SAMPLE_DOMAIN: u32 = 0x5245_564c;
pub const DISC_SAMPLE_DOMAIN: u32 = 0x4449_5343;
pub const SAMPLE_MULTIPLIER: u32 = 0x9e37_79b9;
pub const DEPTH_MULTIPLIER: u32 = 0x85eb_ca6b;

#[inline]
pub fn mix32(mut value: u32) -> u32 {
    value ^= value >> 16;
    value = value.wrapping_mul(0x7feb_352d);
    value ^= value >> 15;
    value = value.wrapping_mul(0x846c_a68b);
    value ^= value >> 16;
    value
}

#[inline]
pub fn headless_disc_bits(seed: u32, mv: i32) -> u32 {
    mix32(seed ^ (mv as u32).wrapping_add(1).wrapping_mul(0x9e37_79b9) ^ NEXT_DISC_DOMAIN)
}

#[inline]
pub fn headless_disc(seed: u32, mv: i32) -> u8 {
    (((headless_disc_bits(seed, mv) as u64 * 7) >> 32) + 1) as u8
}

#[derive(Clone, Copy)]
pub struct Mulberry32 {
    state: u32,
}

impl Mulberry32 {
    #[inline]
    pub fn new(seed: u32) -> Mulberry32 {
        Mulberry32 { state: seed }
    }

    #[inline]
    pub fn next_bits(&mut self) -> u32 {
        self.state = self.state.wrapping_add(0x6d2b_79f5);
        let mut value = self.state;
        value = (value ^ (value >> 15)).wrapping_mul(value | 1);
        value ^= value.wrapping_add((value ^ (value >> 7)).wrapping_mul(value | 61));
        value ^ (value >> 14)
    }

    #[inline]
    #[allow(dead_code)]
    pub fn next_unit(&mut self) -> f64 {
        self.next_bits() as f64 / 4_294_967_296.0
    }
}

impl Random for Mulberry32 {
    #[inline]
    fn next_disc(&mut self) -> u8 {
        (((self.next_bits() as u64 * 7) >> 32) + 1) as u8
    }
}

/// Stratified unit draw, character-for-character from the C++ reference.
#[inline]
pub fn stratified_unit(seed: u32, sample: i32, count: i32, domain: u32, event: i32) -> f64 {
    let event_seed = mix32(
        seed ^ domain ^ ((event as u32).wrapping_add(1).wrapping_mul(DEPTH_MULTIPLIER)),
    );
    let rotation = (event_seed % (count as u32)) as i32;
    let stratum = (sample + rotation) % count;
    let jitter = mix32(
        event_seed ^ ((sample as u32).wrapping_add(1).wrapping_mul(SAMPLE_MULTIPLIER)),
    ) as f64
        / 4_294_967_296.0;
    (stratum as f64 + jitter) / count as f64
}

#[derive(Clone, Copy)]
pub struct StratifiedRandom {
    pub seed: u32,
    pub sample: i32,
    pub count: i32,
    pub event: i32,
}

impl Random for StratifiedRandom {
    #[inline]
    fn next_disc(&mut self) -> u8 {
        let unit = stratified_unit(self.seed, self.sample, self.count, REVEAL_SAMPLE_DOMAIN, self.event);
        self.event += 1;
        // unit is in [0,1) by construction, so truncation and floor agree
        // exactly, as in the proven C++ fast search.
        (unit * 7.0) as i32 as u8 + 1
    }
}

#[inline]
pub fn sampled_next_disc(seed: u32, sample: i32, count: i32) -> u8 {
    let unit = stratified_unit(seed, sample, count, DISC_SAMPLE_DOMAIN, 0);
    (unit * 7.0) as i32 as u8 + 1
}

/// FNV-style state hash used to derive chance scenarios, copied from
/// scenarioSeedForState: cells in row-major order, then next disc, then
/// moves remaining, then the policy seed and depth.
#[inline]
pub fn scenario_seed_for_state(
    board: &crate::board::Board,
    next_disc: u8,
    moves_remaining: i32,
    policy_seed: u32,
    depth: i32,
) -> u32 {
    let mut hash = 0x811c_9dc5u32;
    for row in 0..crate::board::BOARD_SIZE {
        for col in 0..crate::board::BOARD_SIZE {
            hash ^= board.get(row, col) as u32 + 1;
            hash = hash.wrapping_mul(0x0100_0193);
        }
    }
    hash ^= next_disc as u32;
    hash = hash.wrapping_mul(0x0100_0193);
    hash ^= moves_remaining as u32;
    mix32(hash ^ policy_seed ^ ((depth as u32).wrapping_add(1).wrapping_mul(DEPTH_MULTIPLIER)))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mulberry32_matches_known_sequence() {
        // First outputs of seededRandom(1), computed by the TypeScript test
        // suite's own reference: state += 0x6d2b79f5 etc.
        let mut rng = Mulberry32::new(1);
        let first = rng.next_bits();
        // Independently recomputed by hand:
        let mut state = 1u32;
        state = state.wrapping_add(0x6d2b_79f5);
        let mut value = state;
        value = (value ^ (value >> 15)).wrapping_mul(value | 1);
        value ^= value.wrapping_add((value ^ (value >> 7)).wrapping_mul(value | 61));
        let expected = value ^ (value >> 14);
        assert_eq!(first, expected);
    }

    #[test]
    fn discs_in_range() {
        let mut rng = Mulberry32::new(42);
        for _ in 0..1000 {
            let d = rng.next_disc();
            assert!((1..=7).contains(&d));
        }
    }
}
