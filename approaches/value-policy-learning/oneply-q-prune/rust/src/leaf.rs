// A linear leaf over the eighteen fair-leaf terms with an intercept and an
// optional "best immediate Klein-Friedmann drop" term:
//
//   leaf(s) = bias + sum_j w_j term_j(s) + kf_beta * max_{legal a} Q_kf(s, a)
//
// With the frozen term weights, bias 0 and kf_beta 0 it is the frozen fair
// leaf bit for bit (`frozen_linear_leaf_matches_fair_leaf_bits`).  The
// weights are fitted offline (analysis/fit_leaf.py) to the exact depth-4
// root value on panel roots: one TreeStrap-style bootstrapped refit.

use drop7_kf_linear_q::features::{features, FEATURE_COUNT, FEATURE_NAMES as KF_NAMES};
use drop7_kf_linear_q::learn::q_of;
use drop7_kf_linear_q::view::PublicView;
use drop7_rs::board::BOARD_SIZE;
use drop7_rs::engine::State;
use drop7_rs::leaf::{
    fair_leaf_terms, weights::FAIR_TERMINAL_UTILITY, LeafScratch, FROZEN_LEAF_WEIGHTS, LEAF_TERM_NAMES,
};
use drop7_rs::search::Leaf;

pub const LEAF_TERMS: usize = 18;

#[derive(Clone, Debug, PartialEq)]
pub struct LinearLeafWeights {
    pub terms: [f64; LEAF_TERMS],
    pub bias: f64,
    pub kf_beta: f64,
    pub kf: [f64; FEATURE_COUNT],
}

impl LinearLeafWeights {
    pub fn frozen() -> Self {
        LinearLeafWeights {
            terms: FROZEN_LEAF_WEIGHTS,
            bias: 0.0,
            kf_beta: 0.0,
            kf: [0.0; FEATURE_COUNT],
        }
    }

    pub fn names() -> Vec<String> {
        let mut names: Vec<String> = LEAF_TERM_NAMES.iter().map(|n| n.to_string()).collect();
        names.push("bias".to_string());
        names.push("kf_beta".to_string());
        for n in KF_NAMES.iter() {
            names.push(format!("kf_{n}"));
        }
        names
    }

    /// `name value` lines; every name in `names()` exactly once.
    pub fn load(path: &str) -> Result<Self, String> {
        let text = std::fs::read_to_string(path).map_err(|e| format!("cannot read {path}: {e}"))?;
        let names = Self::names();
        let mut values = vec![0.0f64; names.len()];
        let mut seen = vec![false; names.len()];
        for (line_number, source) in text.lines().enumerate() {
            let line = source.split('#').next().unwrap_or("").trim();
            if line.is_empty() {
                continue;
            }
            let fields: Vec<&str> = line.split_whitespace().collect();
            if fields.len() != 2 {
                return Err(format!("{path}:{}: expected `name value`", line_number + 1));
            }
            let Some(index) = names.iter().position(|n| n == fields[0]) else {
                return Err(format!("{path}:{}: unknown leaf weight {}", line_number + 1, fields[0]));
            };
            if seen[index] {
                return Err(format!("{path}:{}: duplicate {}", line_number + 1, fields[0]));
            }
            let value: f64 = fields[1]
                .parse()
                .map_err(|_| format!("{path}:{}: bad value for {}", line_number + 1, fields[0]))?;
            if !value.is_finite() {
                return Err(format!("{path}:{}: non-finite {}", line_number + 1, fields[0]));
            }
            values[index] = value;
            seen[index] = true;
        }
        if let Some(index) = seen.iter().position(|s| !s) {
            return Err(format!("{path}: missing {}", names[index]));
        }
        let mut result = Self::frozen();
        result.terms.copy_from_slice(&values[..LEAF_TERMS]);
        result.bias = values[LEAF_TERMS];
        result.kf_beta = values[LEAF_TERMS + 1];
        result.kf.copy_from_slice(&values[LEAF_TERMS + 2..]);
        Ok(result)
    }

    pub fn save(&self, path: &str) -> std::io::Result<()> {
        let names = Self::names();
        let mut values: Vec<f64> = self.terms.to_vec();
        values.push(self.bias);
        values.push(self.kf_beta);
        values.extend_from_slice(&self.kf);
        let mut text = String::new();
        for (name, value) in names.iter().zip(values.iter()) {
            text.push_str(&format!("{name} {value:?}\n"));
        }
        std::fs::write(path, text)
    }
}

/// max over legal columns of the six-feature Q, or 0 when no column is legal.
pub fn kf_best(state: &State, kf: &[f64; FEATURE_COUNT]) -> f64 {
    let view = PublicView::from_state(state);
    let mut best = f64::NEG_INFINITY;
    for column in 0..BOARD_SIZE {
        if view.is_legal(column) {
            best = best.max(q_of(kf, &features(&view, column)));
        }
    }
    if best.is_finite() {
        best
    } else {
        0.0
    }
}

pub struct LinearLeaf {
    pub weights: LinearLeafWeights,
    scratch: LeafScratch,
}

impl LinearLeaf {
    pub fn new(weights: LinearLeafWeights) -> LinearLeaf {
        LinearLeaf {
            weights,
            scratch: LeafScratch::default(),
        }
    }
}

impl Leaf for LinearLeaf {
    #[inline]
    fn value(&mut self, state: &State) -> f64 {
        if state.game_over {
            return FAIR_TERMINAL_UTILITY;
        }
        let terms = fair_leaf_terms(state, &mut self.scratch);
        let mut value = self.weights.bias;
        for j in 0..LEAF_TERMS {
            value += self.weights.terms[j] * terms[j];
        }
        if self.weights.kf_beta != 0.0 {
            value += self.weights.kf_beta * kf_best(state, &self.weights.kf);
        }
        value
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use drop7_rs::engine::{center_first_move, play_headless_move, FullWaveSink};
    use drop7_rs::search::FairLeaf;

    #[test]
    fn frozen_linear_leaf_matches_fair_leaf_bits() {
        let mut fair = FairLeaf::default();
        let mut linear = LinearLeaf::new(LinearLeafWeights::frozen());
        let mut compared = 0;
        for seed in [0xa527_8006u32, 0xa527_8007] {
            let mut state = State::initial_headless(seed);
            let mut sink = FullWaveSink::new();
            for _ in 0..30 {
                if state.game_over {
                    break;
                }
                assert_eq!(fair.value(&state).to_bits(), linear.value(&state).to_bits());
                compared += 1;
                let column = center_first_move(&state.board).unwrap();
                sink.clear();
                play_headless_move(&mut state, seed, column, &mut sink).unwrap();
            }
        }
        assert!(compared >= 40);
    }

    #[test]
    fn weights_round_trip() {
        let mut w = LinearLeafWeights::frozen();
        w.bias = 12345.5;
        w.kf_beta = 0.25;
        w.kf[4] = 2.0;
        let path = std::env::temp_dir().join("drop7-oneply-q-leaf-weights-test.txt");
        w.save(path.to_str().unwrap()).unwrap();
        let back = LinearLeafWeights::load(path.to_str().unwrap()).unwrap();
        assert_eq!(w, back);
    }
}
