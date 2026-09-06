// The one-ply chance-state policy that generates training play, and the
// leaf adapter that puts the same tables inside the fair search.
//
// For every legal column the policy plays the visible disc with the engine's
// own move resolver, drawing the covered-disc reveals from the same public
// stratified sampler the fair search uses (seeded from the public state and
// the fixed policy seed, never from the game seed), and scores the resulting
// chance state: the move's points plus the table value of the board after the
// cascade and any rise, before the next disc is known.  When the first sample
// reveals nothing the cascade was deterministic and the remaining samples are
// skipped, because they would produce the same board.
//
// INFORMATION BOUNDARY.  Inputs: the public state.  The reveal sampler is a
// deterministic function of (board, next disc, moves remaining, policy seed),
// so the policy is a deterministic function of the public state with no
// cross-move memory.

use std::sync::Arc;

use drop7_rs::board::EMPTY;
use drop7_rs::engine::{play_move_sampled, State, Wave, WaveSink};
use drop7_rs::rng::{scenario_seed_for_state, StratifiedRandom};
use drop7_rs::search::{canonical_state, Leaf, COLUMN_ORDER};

use crate::model::{Model, MAX_ACTIVE, VALUE_SCALE};

#[derive(Clone, Copy, Debug)]
pub struct PolicyParams {
    /// Stratified reveal samples per column (7 = the fair search's strata).
    pub reveal_samples: i32,
    /// The fixed public sampler seed (the fair search's policy seed).
    pub policy_seed: u32,
}

impl Default for PolicyParams {
    fn default() -> Self {
        PolicyParams {
            reveal_samples: 7,
            policy_seed: 0xd707_5eed,
        }
    }
}

/// A wave sink that counts reveals: the policy needs to know whether a
/// sampled cascade drew any random value at all.
#[derive(Default, Clone, Copy)]
pub struct RevealSink {
    pub count: i32,
    pub last_depth: i32,
    pub revealed: i32,
}

impl WaveSink for RevealSink {
    #[inline(always)]
    fn push(&mut self, wave: Wave) {
        self.count += 1;
        self.last_depth = wave.depth;
        self.revealed += wave.revealed;
    }
    #[inline(always)]
    fn is_empty(&self) -> bool {
        self.count == 0
    }
    #[inline(always)]
    fn back_depth(&self) -> i32 {
        self.last_depth
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Decision {
    /// Chosen column, or -1 when no column is legal.
    pub action: i32,
    /// The chosen column's estimated value (points scored by the move plus
    /// the chance-state value), in rise units.
    pub value: f32,
    /// Move simulations performed.
    pub simulations: u32,
}

/// Expected value of one column, in rise units, averaged over the stratified
/// reveal samples; None when the column is illegal.
#[inline]
pub fn column_value(
    model: &Model,
    state: &State,
    column: usize,
    params: &PolicyParams,
    scratch: &mut [u64; MAX_ACTIVE],
    simulations: &mut u32,
) -> Option<f32> {
    if state.board.get(0, column) != EMPTY {
        return None;
    }
    let state_seed = scenario_seed_for_state(
        &state.board,
        state.next_disc,
        state.moves_remaining,
        params.policy_seed,
        0,
    );
    let mut total = 0.0f32;
    for sample in 0..params.reveal_samples {
        let mut random = StratifiedRandom {
            seed: state_seed,
            sample,
            count: params.reveal_samples,
            event: 0,
        };
        let mut sink = RevealSink::default();
        *simulations += 1;
        let played = play_move_sampled(state, column, &mut random, &mut sink)?;
        let reward = played.score_delta as f32 / VALUE_SCALE as f32;
        let value = if played.state.game_over {
            reward
        } else {
            reward + model.value(&played.state.board, played.state.moves_remaining, scratch)
        };
        if sample == 0 && sink.revealed == 0 {
            // No random draw touched the board: every stratum yields this
            // board, so the average is this value.
            return Some(value);
        }
        total += value;
    }
    Some(total / params.reveal_samples as f32)
}

/// Greedy column choice in the search's column order (ties go to the more
/// central column, as in the fair search).  The state is canonicalised under
/// horizontal reflection first, exactly as the fair search canonicalises its
/// root, so a position and its mirror are evaluated on the same board with
/// the same reveal samples and the chosen columns mirror each other.
pub fn choose(
    model: &Model,
    state: &State,
    params: &PolicyParams,
    scratch: &mut [u64; MAX_ACTIVE],
) -> Decision {
    let mut decision = Decision {
        action: -1,
        value: f32::NEG_INFINITY,
        simulations: 0,
    };
    if state.game_over {
        return decision;
    }
    let (canonical, mirrored) = canonical_state(state);
    for &column in COLUMN_ORDER.iter() {
        if let Some(value) = column_value(model, &canonical, column, params, scratch, &mut decision.simulations) {
            if value > decision.value {
                decision.value = value;
                decision.action = column as i32;
            }
        }
    }
    if mirrored && decision.action >= 0 {
        decision.action = 6 - decision.action;
    }
    decision
}

/// The tables as a search leaf: the chance-state value of the leaf board at
/// its rise phase, in points.  The visible next disc is not read.
pub struct NTupleLeaf {
    pub model: Arc<Model>,
    scratch: [u64; MAX_ACTIVE],
}

impl NTupleLeaf {
    pub fn new(model: Arc<Model>) -> NTupleLeaf {
        NTupleLeaf {
            model,
            scratch: [0u64; MAX_ACTIVE],
        }
    }
}

impl Leaf for NTupleLeaf {
    #[inline]
    fn value(&mut self, state: &State) -> f64 {
        VALUE_SCALE * self.model.value(&state.board, state.moves_remaining, &mut self.scratch) as f64
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Layout;

    #[test]
    fn choose_returns_a_legal_column_and_is_deterministic() {
        let model = Model::new(Layout::parse("cols,win32").unwrap(), 5.0, false);
        let params = PolicyParams::default();
        let mut scratch = [0u64; MAX_ACTIVE];
        let state = State::initial_headless(0xa527_7001);
        let a = choose(&model, &state, &params, &mut scratch);
        let b = choose(&model, &state, &params, &mut scratch);
        assert_eq!(a.action, b.action);
        assert!((0..7).contains(&a.action));
        assert!(state.board.is_legal(a.action as usize));
        assert!(a.simulations >= 7);
    }
}
