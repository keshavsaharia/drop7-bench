// Training-time fair expectimax over the tables.
//
// The deployed search is drop7_rs::search::Searcher, a full-width
// fixed-depth expectimax with stratified reveal samples and a sampled next
// disc at every chance node, a terminal utility for death, and left-right
// canonicalisation.  This module re-implements its recursion for training,
// mirroring the engine bit for bit (same column order, same scenario seeds,
// same stratified samples, same accumulation order, same terminal utility,
// same canonicalisation; the CHECK gate compares root column values against
// Searcher::column_values and requires bit equality), with two additions the
// engine does not need:
//
//   * every node returns two values: the utility value the search decides by
//     (rewards plus the terminal utility at death, the engine's number) and
//     the score-only value of the same decisions (rewards only, zero at
//     death), which is what the tables estimate: the expected remaining
//     score of a board under the search's own play over the next plies and
//     the leaf beyond them.  Training toward the utility value would fold
//     the search's -1,000,000 death penalty into the tables; training toward
//     the score value keeps their meaning, as one-ply TD does (a terminal
//     move's target is its reward alone);
//
//   * every internal decision node of the tree (the boards one and two
//     imagined moves from the root at depth 3) is collected with its
//     score-only backup as a training target.  These are exactly the boards
//     the deployed search evaluates and one-ply play never visits.
//
// The root's score value is the target for the visited afterstate (TD toward
// the search's value rather than the one-step sample); the collected nodes
// are the TreeStrap targets.  Leaves (depth 0) are not collected: their
// backup is their own value.
//
// INFORMATION BOUNDARY.  Inputs: the public state.  Scenario seeds are
// derived from the public state and the fixed policy seed exactly as the
// engine derives them; the game seed never enters.

use drop7_rs::board::{Board, EMPTY};
use drop7_rs::engine::{center_first_move, play_move_sampled, MinimalWaveSink, State};
use drop7_rs::rng::{sampled_next_disc, scenario_seed_for_state, StratifiedRandom};
use drop7_rs::search::{canonical_state, SearchParams, COLUMN_ORDER};

use crate::model::{Model, MAX_ACTIVE, VALUE_SCALE};

/// One internal node of the search tree: a canonical decision state's board
/// and rise phase, its remaining depth, and its score-only backup in rise
/// units.
#[derive(Clone, Copy, Debug)]
pub struct Target {
    pub board: Board,
    pub moves_remaining: i32,
    pub depth: i32,
    pub value: f32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct SearchDecision {
    /// Chosen column (unmirrored), or -1 when no column is legal.
    pub action: i32,
    /// The root's utility value, in points (the engine's number).
    pub root_utility: f64,
    /// The root's score-only value, in points: the training target of the
    /// visited afterstate.
    pub root_score: f64,
    pub leaf_calls: u64,
    pub move_calls: u64,
    pub nodes: u64,
}

pub struct TrainSearch<'a> {
    model: &'a Model,
    params: SearchParams,
    scratch: [u64; MAX_ACTIVE],
    /// Internal-node targets of the last decision when it collected them.
    pub targets: Vec<Target>,
    collect: bool,
    leaf_calls: u64,
    move_calls: u64,
    nodes: u64,
}

impl<'a> TrainSearch<'a> {
    pub fn new(model: &'a Model, params: SearchParams) -> TrainSearch<'a> {
        TrainSearch {
            model,
            params,
            scratch: [0u64; MAX_ACTIVE],
            targets: Vec::with_capacity(4096),
            collect: false,
            leaf_calls: 0,
            move_calls: 0,
            nodes: 0,
        }
    }

    pub fn params(&self) -> SearchParams {
        self.params
    }

    /// Decide at `source` with the full fixed-depth search.  With `collect`
    /// the internal-node targets of the tree are left in `self.targets`.
    pub fn decide(&mut self, source: &State, collect: bool) -> SearchDecision {
        self.targets.clear();
        self.collect = collect;
        self.leaf_calls = 0;
        self.move_calls = 0;
        self.nodes = 0;
        let mut decision = SearchDecision { action: -1, ..Default::default() };
        if source.game_over {
            return decision;
        }
        let (canonical, mirrored) = canonical_state(source);
        let depth = self.params.depth;
        let mut best_utility = f64::NEG_INFINITY;
        let mut best_score = 0.0f64;
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) != EMPTY {
                continue;
            }
            let (utility, score) = self.action(&canonical, column, depth);
            if utility > best_utility {
                best_utility = utility;
                best_score = score;
                decision.action = column as i32;
            }
        }
        if decision.action < 0 {
            decision.action = center_first_move(&canonical.board).map(|c| c as i32).unwrap_or(-1);
            best_utility = self.params.terminal_utility;
            best_score = 0.0;
        }
        if mirrored && decision.action >= 0 {
            decision.action = 6 - decision.action;
        }
        decision.root_utility = best_utility;
        decision.root_score = best_score;
        decision.leaf_calls = self.leaf_calls;
        decision.move_calls = self.move_calls;
        decision.nodes = self.nodes;
        decision
    }

    /// Root column values (utility, in COLUMN_ORDER) of the canonical state
    /// at `depth`, for the gate that compares against the engine.
    pub fn column_values(&mut self, source: &State, depth: i32) -> (Vec<(usize, f64)>, i32) {
        self.targets.clear();
        self.collect = false;
        let (canonical, mirrored) = canonical_state(source);
        let mut values = Vec::new();
        let mut action = -1i32;
        let mut best = f64::NEG_INFINITY;
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) != EMPTY {
                continue;
            }
            let (utility, _) = self.action(&canonical, column, depth);
            values.push((column, utility));
            if utility > best {
                best = utility;
                action = column as i32;
            }
        }
        if mirrored && action >= 0 {
            action = 6 - action;
        }
        (values, action)
    }

    /// Mirrors Searcher::evaluate_action: (utility, score) averaged over the
    /// stratified samples.
    fn action(&mut self, state: &State, column: usize, depth: i32) -> (f64, f64) {
        let state_seed = scenario_seed_for_state(
            &state.board,
            state.next_disc,
            state.moves_remaining,
            self.params.policy_seed,
            depth,
        );
        let mut utility = 0.0f64;
        let mut score = 0.0f64;
        for sample in 0..self.params.chance_samples {
            let mut random = StratifiedRandom {
                seed: state_seed,
                sample,
                count: self.params.chance_samples,
                event: 0,
            };
            let mut sink = MinimalWaveSink::default();
            let played = play_move_sampled(state, column, &mut random, &mut sink);
            self.move_calls += 1;
            let Some(move_result) = played else {
                utility += self.params.terminal_utility;
                continue;
            };
            let score_delta = move_result.score_delta as f64;
            if move_result.state.game_over {
                utility += score_delta + self.params.terminal_utility;
                score += score_delta;
                continue;
            }
            let mut next = move_result.state;
            next.score = 0;
            next.next_disc = sampled_next_disc(state_seed, sample, self.params.chance_samples);
            let next = canonical_state(&next).0;
            let (u, s) = self.node(&next, depth - 1);
            utility += score_delta + u;
            score += score_delta + s;
        }
        let n = self.params.chance_samples as f64;
        (utility / n, score / n)
    }

    /// Mirrors Searcher::best_future_value, returning (utility, score) and
    /// collecting the node as a target when it is internal.
    fn node(&mut self, state: &State, depth: i32) -> (f64, f64) {
        self.nodes += 1;
        if state.game_over {
            return (self.params.terminal_utility, 0.0);
        }
        if depth == 0 {
            self.leaf_calls += 1;
            let value = VALUE_SCALE * self.model.value(&state.board, state.moves_remaining, &mut self.scratch) as f64;
            return (value, value);
        }
        let mut best_utility = f64::NEG_INFINITY;
        let mut best_score = 0.0f64;
        for &column in COLUMN_ORDER.iter() {
            if state.board.get(0, column) != EMPTY {
                continue;
            }
            let (utility, score) = self.action(state, column, depth);
            if utility > best_utility {
                best_utility = utility;
                best_score = score;
            }
        }
        if !best_utility.is_finite() {
            best_utility = self.params.terminal_utility;
            best_score = 0.0;
        }
        if self.collect {
            self.targets.push(Target {
                board: state.board,
                moves_remaining: state.moves_remaining,
                depth,
                value: (best_score / VALUE_SCALE) as f32,
            });
        }
        (best_utility, best_score)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::deployment_params;
    use crate::model::{Layout, UpdateStats};
    use crate::policy::NTupleLeaf;
    use drop7_rs::engine::{play_headless_move, FullWaveSink};
    use drop7_rs::search::{NoTable, Searcher};
    use std::sync::Arc;

    fn trained_small_model() -> Model {
        let layout = Layout::parse("rows,win32,phase=cols").unwrap();
        let model = Model::new(layout, 20.0, true);
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut prev = [0u64; MAX_ACTIVE];
        let mut next = [0u64; MAX_ACTIVE];
        let mut stats = UpdateStats::default();
        let mut sink = FullWaveSink::new();
        for game in 0..4u32 {
            let seed = 0xa527_7300u32.wrapping_add(game);
            let mut state = State::initial_headless(seed);
            let mut n_prev = model.features(&state.board, state.moves_remaining, &mut prev);
            while !state.game_over && state.moves_played < 60 {
                let d = crate::policy::choose(&model, &state, &crate::policy::PolicyParams::default(), &mut scratch);
                if d.action < 0 {
                    break;
                }
                sink.clear();
                let Some(result) = play_headless_move(&mut state, seed, d.action as usize, &mut sink) else { break };
                let reward = result.score_delta as f32 / VALUE_SCALE as f32;
                let target = if state.game_over {
                    reward
                } else {
                    let n = model.features(&state.board, state.moves_remaining, &mut next);
                    reward + model.value_of(&next[..n])
                };
                model.update(&prev[..n_prev], target, 1.0, 30.0, &mut stats);
                if !state.game_over {
                    std::mem::swap(&mut prev, &mut next);
                    n_prev = layout.active_count();
                }
            }
        }
        model
    }

    #[test]
    fn training_search_matches_the_engine_bit_for_bit_and_collects_internal_nodes() {
        let model = Arc::new(trained_small_model());
        let params = deployment_params();
        // column_values never resets the engine's work counter, so the
        // comparison engine runs without a budget; values are unaffected.
        let unbounded = SearchParams { maximum_work: u64::MAX, ..params };
        let mut engine = Searcher::new(unbounded, NTupleLeaf::new(model.clone()), NoTable);
        let mut train = TrainSearch::new(&model, params);
        let seed = 0xa527_7310u32;
        let mut state = State::initial_headless(seed);
        let mut sink = FullWaveSink::new();
        let mut compared = 0;
        while !state.game_over && state.moves_played < 12 {
            let (engine_values, engine_action) = engine.column_values(&state, 3);
            let (train_values, train_action) = train.column_values(&state, 3);
            assert_eq!(engine_values.len(), train_values.len());
            for ((c1, v1), (c2, v2)) in engine_values.iter().zip(train_values.iter()) {
                assert_eq!(c1, c2);
                assert_eq!(v1.to_bits(), v2.to_bits(), "column {c1}: engine {v1} vs training {v2}");
            }
            assert_eq!(engine_action, train_action);
            let decision = train.decide(&state, true);
            assert_eq!(decision.action, engine_action);
            assert!(decision.root_utility.is_finite() && decision.root_score.is_finite());
            assert!(decision.root_score >= decision.root_utility - 1e-6);
            let internal = train.targets.len();
            assert!(internal > 0 && internal <= 49 + 49 * 49, "{internal} internal targets");
            assert!(train.targets.iter().all(|t| t.value.is_finite() && (t.depth == 1 || t.depth == 2) && t.moves_remaining >= 1));
            let again = train.decide(&state, true);
            assert_eq!(again.action, decision.action);
            assert_eq!(again.root_score.to_bits(), decision.root_score.to_bits());
            assert_eq!(again.leaf_calls, decision.leaf_calls);
            compared += 1;
            sink.clear();
            play_headless_move(&mut state, seed, decision.action as usize, &mut sink).unwrap();
        }
        assert!(compared >= 10);
    }
}
