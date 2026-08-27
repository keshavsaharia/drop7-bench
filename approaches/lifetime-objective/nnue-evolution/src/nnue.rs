// The NNUE leaf network: an EmbeddingBag-style sparse first layer (exactly
// ACTIVE gathered rows summed, never a dense product) and a tiny dense tail.
// This is the leaf-affordable model class characterised by
// approaches/lifetime-objective/learned-leaf (~572k parameters, microsecond
// inference); the approach's novelty is the training signal (depth-5 teacher
// distillation followed by whole-game evolution), not the shape.
//
// DETERMINISM CONTRACT.  f32 arithmetic in a fixed accumulation order, no
// threading, no atomics: the same weights and the same state always produce
// the same bits.  VALUE_SCALE converts the network's output unit (one row
// rise bonus) into the points unit the search composes with score deltas.

use crate::features::{self, ACTIVE, FEATURES};
use drop7_rs::engine::State;
use drop7_rs::rng::Mulberry32;
use drop7_rs::search::Leaf;

pub const HIDDEN: usize = 64;
pub const MID: usize = 32;
pub const VALUE_SCALE: f64 = 17_000.0;

const MAGIC: &[u8; 8] = b"D7NNUE01";

#[derive(Clone)]
pub struct Nnue {
    /// Sparse first layer: one HIDDEN-wide row per feature.
    pub ft_weight: Vec<f32>, // [FEATURES][HIDDEN] row-major
    pub ft_bias: Vec<f32>,   // [HIDDEN]
    pub w1: Vec<f32>,        // [MID][HIDDEN] row-major
    pub b1: Vec<f32>,        // [MID]
    pub w2: Vec<f32>,        // [MID]
    pub b2: f32,
}

impl Nnue {
    pub fn parameter_count() -> usize {
        FEATURES * HIDDEN + HIDDEN + MID * HIDDEN + MID + MID + 1
    }

    /// Seeded Gaussian init (Box-Muller over Mulberry32 draws): sparse rows
    /// narrow so the 135-row sum starts with unit-ish scale, dense tail
    /// Kaiming-flavoured, biases zero.  Only used for ablations; the approach
    /// initialises from the supervised teacher distillation.
    pub fn random(seed: u32) -> Nnue {
        let mut rng = Mulberry32::new(seed);
        let gauss = |rng: &mut Mulberry32, sigma: f32| -> f32 {
            // Box-Muller on two (0,1] draws.
            let u1 = (rng.next_bits() as f64 + 1.0) / 4_294_967_297.0;
            let u2 = (rng.next_bits() as f64 + 1.0) / 4_294_967_297.0;
            (sigma as f64 * (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos()) as f32
        };
        let ft_sigma = 0.02f32;
        let w1_sigma = (2.0f32 / HIDDEN as f32).sqrt();
        let w2_sigma = 0.05f32;
        Nnue {
            ft_weight: (0..FEATURES * HIDDEN).map(|_| gauss(&mut rng, ft_sigma)).collect(),
            ft_bias: vec![0.0; HIDDEN],
            w1: (0..MID * HIDDEN).map(|_| gauss(&mut rng, w1_sigma)).collect(),
            b1: vec![0.0; MID],
            w2: (0..MID).map(|_| gauss(&mut rng, w2_sigma)).collect(),
            b2: 0.0,
        }
    }

    /// Scalar value of one public state, in network units (divide-free; the
    /// Leaf wrapper multiplies by VALUE_SCALE).  Fixed accumulation order:
    /// feature slots in build() order, hidden units ascending.
    pub fn value(&self, index: &[u16; ACTIVE]) -> f32 {
        let mut acc = [0.0f32; HIDDEN];
        acc.copy_from_slice(&self.ft_bias);
        for &slot in index.iter() {
            let row = &self.ft_weight[slot as usize * HIDDEN..(slot as usize + 1) * HIDDEN];
            for unit in 0..HIDDEN {
                acc[unit] += row[unit];
            }
        }
        for unit in acc.iter_mut() {
            *unit = unit.max(0.0);
        }
        let mut mid = [0.0f32; MID];
        for m in 0..MID {
            let row = &self.w1[m * HIDDEN..(m + 1) * HIDDEN];
            let mut sum = self.b1[m];
            for h in 0..HIDDEN {
                sum += row[h] * acc[h];
            }
            mid[m] = sum.max(0.0);
        }
        let mut out = self.b2;
        for m in 0..MID {
            out += self.w2[m] * mid[m];
        }
        out
    }

    pub fn value_of_state(&self, state: &State) -> f32 {
        let mut index = [0u16; ACTIVE];
        features::build(state, &mut index);
        self.value(&index)
    }

    /// Ordered mutable tensor views for the evolutionary operators.  The
    /// order is part of the serialisation contract and must stay stable.
    pub fn tensors_mut(&mut self) -> [(&'static str, &mut [f32]); 5] {
        [
            ("ft_weight", &mut self.ft_weight),
            ("ft_bias", &mut self.ft_bias),
            ("w1", &mut self.w1),
            ("b1", &mut self.b1),
            ("w2", &mut self.w2),
        ]
    }

    pub fn tensors(&self) -> [(&'static str, &[f32]); 6] {
        [
            ("ft_weight", &self.ft_weight),
            ("ft_bias", &self.ft_bias),
            ("w1", &self.w1),
            ("b1", &self.b1),
            ("w2", &self.w2),
            ("b2", std::slice::from_ref(&self.b2)),
        ]
    }

    pub fn to_flat(&self) -> Vec<f32> {
        let mut flat = Vec::with_capacity(Self::parameter_count());
        for (_, tensor) in self.tensors() {
            flat.extend_from_slice(tensor);
        }
        flat
    }

    pub fn from_flat(flat: &[f32]) -> Nnue {
        let mut net = Nnue::random(0);
        let mut cursor = 0;
        for (_, tensor) in net.tensors_mut() {
            tensor.copy_from_slice(&flat[cursor..cursor + tensor.len()]);
            cursor += tensor.len();
        }
        net.b2 = flat[cursor];
        assert_eq!(cursor + 1, Self::parameter_count());
        net
    }

    /// Binary serialisation: magic, dims, then tensors in tensors() order as
    /// little-endian f32.  No version skew: the reader checks every length.
    pub fn write_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(16 + Self::parameter_count() * 4);
        out.extend_from_slice(MAGIC);
        out.extend_from_slice(&(HIDDEN as u32).to_le_bytes());
        out.extend_from_slice(&(MID as u32).to_le_bytes());
        for (_, tensor) in self.tensors() {
            for value in tensor {
                out.extend_from_slice(&value.to_le_bytes());
            }
        }
        out
    }

    pub fn read_bytes(bytes: &[u8]) -> Result<Nnue, String> {
        if bytes.len() < 16 || &bytes[..8] != MAGIC {
            return Err("bad magic".to_string());
        }
        let hidden = u32::from_le_bytes(bytes[8..12].try_into().unwrap()) as usize;
        let mid = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
        if hidden != HIDDEN || mid != MID {
            return Err(format!("dims {hidden}/{mid} != {HIDDEN}/{MID}"));
        }
        let expect = 16 + Self::parameter_count() * 4;
        if bytes.len() != expect {
            return Err(format!("length {} != {expect}", bytes.len()));
        }
        let mut flat = Vec::with_capacity(Self::parameter_count());
        for chunk in bytes[16..].chunks_exact(4) {
            flat.push(f32::from_le_bytes(chunk.try_into().unwrap()));
        }
        Ok(Nnue::from_flat(&flat))
    }

    pub fn save(&self, path: &std::path::Path) -> std::io::Result<()> {
        std::fs::write(path, self.write_bytes())
    }

    pub fn load(path: &std::path::Path) -> Result<Nnue, String> {
        let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
        Nnue::read_bytes(&bytes)
    }
}

/// The search::Leaf adapter.  Holds the network by value (cheap to clone for
/// worker threads) and converts units; contains no state of its own, so it is
/// trivially deterministic.
pub struct NnueLeaf {
    pub net: Nnue,
}

impl Leaf for NnueLeaf {
    #[inline]
    fn value(&mut self, state: &State) -> f64 {
        self.net.value_of_state(state) as f64 * VALUE_SCALE
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serialisation_round_trips_bit_exactly() {
        let net = Nnue::random(0x0e701_e50);
        let bytes = net.write_bytes();
        let back = Nnue::read_bytes(&bytes).unwrap();
        assert_eq!(net.to_flat(), back.to_flat());
    }

    #[test]
    fn forward_is_deterministic_and_finite() {
        let net = Nnue::random(0x0e701_e51);
        for seed in [0xa527_7001u32, 0xa527_7002, 0xa527_7003] {
            let state = State::initial_headless(seed);
            let a = net.value_of_state(&state);
            let b = net.value_of_state(&state);
            assert_eq!(a.to_bits(), b.to_bits());
            assert!(a.is_finite());
        }
    }

    #[test]
    fn flat_round_trip_preserves_parameter_order() {
        let net = Nnue::random(7);
        let flat = net.to_flat();
        assert_eq!(flat.len(), Nnue::parameter_count());
        let back = Nnue::from_flat(&flat);
        assert_eq!(net.to_flat(), back.to_flat());
    }
}
