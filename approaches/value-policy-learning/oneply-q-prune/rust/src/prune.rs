// The drop7-rs fair search with sibling pruning at interior max nodes.
//
// Control flow, accumulation order, chance stratification, canonicalisation,
// column order, iterative deepening, table keys and work accounting are the
// same as drop7-rs `Searcher` (search.rs), from which `evaluate_action` and
// `best_future_value` are copied.  The one change: at an interior max node
// with `depth >= 2` plies remaining, if the node has more legal columns than
// `widths[depth]`, the prior ranks the legal columns and only the top
// `widths[depth]` are expanded.  The root is always full width (its values
// are what the decision and the panel read) and the one-ply-remaining layer
// is always full width (49 move-plus-leaf calls per node, not worth a prior
// call).  Chance scenarios and leaves are untouched, so every pruned value is
// a lower bound on the full-width value of the same node, and with every
// width at seven the search is the unchanged drop7-rs search bit for bit
// (`full_width_matches_drop7_rs_bit_for_bit`).

use drop7_rs::board::{BOARD_SIZE, EMPTY};
use drop7_rs::engine::{center_first_move, play_move_sampled, MinimalWaveSink, State};
use drop7_rs::leaf::{fair_leaf, LeafScratch};
use drop7_rs::rng::{sampled_next_disc, scenario_seed_for_state, StratifiedRandom};
use drop7_rs::search::{
    canonical_state, hash_key, DepthTable, PackedKey, SearchParams, SearchResult,
    TranspositionTable, WorkLimitReached, COLUMN_ORDER,
};

use crate::prior::Prior;

/// Expansion width by plies remaining (index 0 and 1 unused; 7 = full).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Widths(pub [usize; 8]);

impl Widths {
    pub fn full() -> Widths {
        Widths([BOARD_SIZE; 8])
    }

    /// `w_{D-1},w_{D-2},...,w_2`: one width per interior layer from the ply
    /// below the root down to two plies remaining.  Empty for depth <= 2.
    pub fn parse(spec: &str, depth: i32) -> Result<Widths, String> {
        let mut widths = Widths::full();
        let layers = (depth - 2).max(0) as usize;
        let parts: Vec<&str> = if spec.is_empty() || spec == "-" {
            Vec::new()
        } else {
            spec.split(',').collect()
        };
        if parts.len() != layers {
            return Err(format!(
                "widths {spec:?}: expected {layers} entries for depth {depth} (plies remaining {}..2)",
                depth - 1
            ));
        }
        for (i, part) in parts.iter().enumerate() {
            let width: usize = part.parse().map_err(|_| format!("bad width {part:?}"))?;
            if width == 0 || width > BOARD_SIZE {
                return Err(format!("width {width} out of range 1..=7"));
            }
            widths.0[(depth - 1) as usize - i] = width;
        }
        Ok(widths)
    }

    pub fn is_full(&self) -> bool {
        self.0.iter().all(|&w| w >= BOARD_SIZE)
    }

    pub fn describe(&self, depth: i32) -> String {
        (2..depth)
            .rev()
            .map(|r| self.0[r as usize].to_string())
            .collect::<Vec<_>>()
            .join(",")
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RootDecision {
    /// Unmirrored column, or -1 when the game is over / nothing is legal.
    pub action: i32,
    pub mirrored: bool,
    /// Legal columns of the canonical root in COLUMN_ORDER, with their values.
    pub columns: Vec<usize>,
    pub values: Vec<f64>,
    pub completed_depth: i32,
    pub nodes: u64,
    pub work: u64,
    pub leaf_calls: u64,
    pub move_calls: u64,
    pub prior_work: u64,
    pub pruned_nodes: u64,
}

pub struct PrunedSearcher<T: TranspositionTable> {
    pub params: SearchParams,
    pub widths: Widths,
    pub prior: Prior,
    leaf_scratch: LeafScratch,
    prior_scratch: LeafScratch,
    table: T,
    nodes: u64,
    work: u64,
    leaf_calls: u64,
    move_calls: u64,
    prior_work: u64,
    pruned_nodes: u64,
    order: Vec<usize>,
}

impl PrunedSearcher<DepthTable> {
    /// The deployment parameters of the repository's fair search (terminal
    /// -1,000,000, policy seed 0xd7075eed, a completion-guaranteeing budget)
    /// with a depth-gated table from depth 1, as `FairSearch` in the
    /// Klein-Friedmann crate builds it.
    pub fn deployment(
        depth: i32,
        strata: i32,
        widths: Widths,
        prior: Prior,
        table_entries: usize,
    ) -> PrunedSearcher<DepthTable> {
        let params = SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: u64::MAX >> 1,
            policy_seed: 0xd707_5eed,
        };
        PrunedSearcher::new(params, widths, prior, DepthTable::new(table_entries, 1))
    }
}

impl<T: TranspositionTable> PrunedSearcher<T> {
    pub fn new(params: SearchParams, widths: Widths, prior: Prior, table: T) -> Self {
        PrunedSearcher {
            params,
            widths,
            prior,
            leaf_scratch: LeafScratch::default(),
            prior_scratch: LeafScratch::default(),
            table,
            nodes: 0,
            work: 0,
            leaf_calls: 0,
            move_calls: 0,
            prior_work: 0,
            pruned_nodes: 0,
            order: Vec::with_capacity(BOARD_SIZE),
        }
    }

    pub fn table_bytes(&self) -> usize {
        self.table.bytes()
    }

    fn reset(&mut self) {
        self.table.clear();
        self.nodes = 0;
        self.work = 0;
        self.leaf_calls = 0;
        self.move_calls = 0;
        self.prior_work = 0;
        self.pruned_nodes = 0;
    }

    #[inline]
    fn check_budget(&self) -> SearchResult<()> {
        if self.work >= self.params.maximum_work {
            return Err(WorkLimitReached);
        }
        Ok(())
    }

    // Copied from drop7-rs Searcher::evaluate_action.
    fn evaluate_action(&mut self, state: &State, column: usize, depth: i32) -> SearchResult<f64> {
        let state_seed = scenario_seed_for_state(
            &state.board,
            state.next_disc,
            state.moves_remaining,
            self.params.policy_seed,
            depth,
        );
        let mut value = 0.0f64;
        for sample in 0..self.params.chance_samples {
            self.check_budget()?;
            let mut random = StratifiedRandom {
                seed: state_seed,
                sample,
                count: self.params.chance_samples,
                event: 0,
            };
            let mut sink = MinimalWaveSink::default();
            let played = play_move_sampled(state, column, &mut random, &mut sink);
            self.work += 1;
            self.move_calls += 1;
            let Some(move_result) = played else {
                value += self.params.terminal_utility;
                continue;
            };
            let score_delta = move_result.score_delta as f64;
            if move_result.state.game_over {
                value += score_delta + self.params.terminal_utility;
                continue;
            }
            let mut next = move_result.state;
            next.score = 0;
            next.next_disc = sampled_next_disc(state_seed, sample, self.params.chance_samples);
            let next = canonical_state(&next).0;
            value += score_delta + self.best_future_value(&next, depth - 1)?;
        }
        Ok(value / self.params.chance_samples as f64)
    }

    fn evaluate_leaf(&mut self, state: &State) -> SearchResult<f64> {
        self.check_budget()?;
        self.work += 1;
        self.leaf_calls += 1;
        let value = fair_leaf(state, &mut self.leaf_scratch);
        if !value.is_finite() {
            panic!("leaf evaluator returned a non-finite value");
        }
        Ok(value)
    }

    // drop7-rs Searcher::best_future_value plus the pruning step.
    fn best_future_value(&mut self, state: &State, depth: i32) -> SearchResult<f64> {
        self.nodes += 1;
        self.check_budget()?;
        if state.game_over {
            return Ok(self.params.terminal_utility);
        }
        if depth == 0 {
            return self.evaluate_leaf(state);
        }
        let key = PackedKey::new(state, depth);
        let hash = hash_key(&key);
        if let Some(cached) = self.table.lookup(&key, hash, depth) {
            return Ok(cached);
        }
        let mut legal = [0usize; BOARD_SIZE];
        let mut count = 0usize;
        for &column in COLUMN_ORDER.iter() {
            if state.board.get(0, column) == EMPTY {
                legal[count] = column;
                count += 1;
            }
        }
        let width = self.widths.0[depth as usize];
        let mut keep: u8 = 0xFF;
        if depth >= 2 && width < count {
            let mut order = std::mem::take(&mut self.order);
            let params = self.params;
            let spent = self.prior.rank(
                state,
                &legal[..count],
                &params,
                depth,
                &mut self.prior_scratch,
                &mut order,
            );
            self.work += spent;
            self.prior_work += spent;
            self.pruned_nodes += 1;
            keep = 0;
            for &column in order.iter().take(width) {
                keep |= 1u8 << column;
            }
            self.order = order;
        }
        let mut best = f64::NEG_INFINITY;
        for &column in legal[..count].iter() {
            if keep & (1u8 << column) == 0 {
                continue;
            }
            let value = self.evaluate_action(state, column, depth)?;
            if value > best {
                best = value;
            }
        }
        if !best.is_finite() {
            best = self.params.terminal_utility;
        }
        self.table.store(&key, hash, depth, best);
        Ok(best)
    }

    /// Full-width root: legal columns in COLUMN_ORDER with their values, and
    /// the canonical-frame argmax (first in order on ties), as
    /// Searcher::root_decision / column_values compute it.
    fn root_values(&mut self, canonical: &State, depth: i32) -> SearchResult<(Vec<usize>, Vec<f64>, i32)> {
        let mut columns = Vec::with_capacity(BOARD_SIZE);
        let mut values = Vec::with_capacity(BOARD_SIZE);
        let mut action = -1i32;
        let mut best = f64::NEG_INFINITY;
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) != EMPTY {
                continue;
            }
            let value = self.evaluate_action(canonical, column, depth)?;
            columns.push(column);
            values.push(value);
            if value > best {
                best = value;
                action = column as i32;
            }
        }
        Ok((columns, values, action))
    }

    fn metrics_into(&self, decision: &mut RootDecision) {
        decision.nodes = self.nodes;
        decision.work = self.work;
        decision.leaf_calls = self.leaf_calls;
        decision.move_calls = self.move_calls;
        decision.prior_work = self.prior_work;
        decision.pruned_nodes = self.pruned_nodes;
    }

    /// One fixed-depth decision on a fresh table (no iterative deepening):
    /// what the panel and the pruning evaluation read.
    pub fn decide(&mut self, source: &State, depth: i32) -> RootDecision {
        let mut decision = RootDecision::default();
        if source.game_over {
            decision.action = -1;
            return decision;
        }
        let (canonical, mirrored) = canonical_state(source);
        self.reset();
        let (columns, values, action) = self
            .root_values(&canonical, depth)
            .expect("the decision budget is unbounded");
        decision.mirrored = mirrored;
        decision.columns = columns;
        decision.values = values;
        decision.completed_depth = depth;
        decision.action = if mirrored && action >= 0 {
            BOARD_SIZE as i32 - 1 - action
        } else {
            action
        };
        self.metrics_into(&mut decision);
        decision
    }

    /// The gameplay decision: iterative deepening to `params.depth` on one
    /// table, centre-first fallback, unmirrored answer -- Searcher::choose_action.
    pub fn choose_action(&mut self, source: &State) -> RootDecision {
        let mut decision = RootDecision::default();
        if source.game_over {
            decision.action = -1;
            return decision;
        }
        let (canonical, mirrored) = canonical_state(source);
        self.reset();
        let mut action = -1i32;
        let mut completed_depth = 0i32;
        for depth in 1..=self.params.depth {
            match self.root_values(&canonical, depth) {
                Ok((columns, values, candidate)) => {
                    if candidate < 0 {
                        break;
                    }
                    action = candidate;
                    completed_depth = depth;
                    decision.columns = columns;
                    decision.values = values;
                }
                Err(WorkLimitReached) => break,
            }
        }
        if action < 0 {
            action = center_first_move(&canonical.board)
                .map(|c| c as i32)
                .unwrap_or(-1);
        }
        decision.mirrored = mirrored;
        decision.completed_depth = completed_depth;
        decision.action = if mirrored && action >= 0 {
            BOARD_SIZE as i32 - 1 - action
        } else {
            action
        };
        self.metrics_into(&mut decision);
        decision
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::oneply::oneply;
    use drop7_kf_linear_q::game::play_game;
    use drop7_kf_linear_q::policy::{CenterFirst, Policy};
    use drop7_kf_linear_q::view::PublicView;
    use drop7_rs::engine::{play_headless_move, FullWaveSink};
    use drop7_rs::search::{work_bound_for, FairLeaf, Searcher};

    /// A handful of probe states from the CHECK probe block: openings and
    /// mid-game boards reached by centre-first play.
    fn probe_states() -> Vec<State> {
        let mut states = Vec::new();
        for seed in [0xa527_8000u32, 0xa527_8001, 0xa527_8002] {
            let mut state = State::initial_headless(seed);
            let mut sink = FullWaveSink::new();
            for step in 0..24 {
                if state.game_over {
                    break;
                }
                if step % 6 == 0 {
                    states.push(state);
                }
                let column = center_first_move(&state.board).unwrap();
                sink.clear();
                play_headless_move(&mut state, seed, column, &mut sink).unwrap();
            }
        }
        states
    }

    fn reference(depth: i32, strata: i32) -> Searcher<FairLeaf, DepthTable> {
        let params = SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        };
        Searcher::new(params, FairLeaf::default(), DepthTable::new(4096, 1))
    }

    #[test]
    fn full_width_matches_drop7_rs_bit_for_bit() {
        for (depth, strata) in [(3, 5), (3, 7), (4, 5)] {
            let mut expected = reference(depth, strata);
            let mut pruned =
                PrunedSearcher::new(expected_params(depth, strata), Widths::full(), Prior::Center, DepthTable::new(4096, 1));
            for state in probe_states() {
                expected.begin_parallel_decision();
                let (canonical, _) = canonical_state(&state);
                let mut expected_work = 0u64;
                let mut expected_values = Vec::new();
                for &column in COLUMN_ORDER.iter() {
                    if canonical.board.get(0, column) != EMPTY {
                        continue;
                    }
                    let value = expected.evaluate_root_column(&canonical, column, depth).unwrap();
                    expected_work += expected.last_metrics().work;
                    expected_values.push((column, value));
                }
                let (_, expected_action) = expected.column_values(&state, depth);
                let decision = pruned.decide(&state, depth);
                assert_eq!(decision.action, expected_action);
                assert_eq!(decision.columns.len(), expected_values.len());
                for (i, (column, value)) in expected_values.iter().enumerate() {
                    assert_eq!(decision.columns[i], *column);
                    assert_eq!(decision.values[i].to_bits(), value.to_bits(), "depth {depth} strata {strata}");
                }
                assert_eq!(decision.work, expected_work, "work at depth {depth} strata {strata}");
                assert_eq!(decision.prior_work, 0);
                assert_eq!(decision.pruned_nodes, 0);
            }
        }
    }

    fn expected_params(depth: i32, strata: i32) -> SearchParams {
        SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        }
    }

    #[test]
    fn oneply_d1_value_matches_depth_one_search_bit_for_bit() {
        // column_values never resets the work counter, so give the reference
        // an unbounded budget for the repeated depth-1 calls.
        let mut params = expected_params(1, 7);
        params.maximum_work = u64::MAX >> 1;
        let mut expected = Searcher::new(params, FairLeaf::default(), DepthTable::new(4096, 1));
        let mut scratch = LeafScratch::default();
        for state in probe_states() {
            let (canonical, _) = canonical_state(&state);
            let (values, _) = expected.column_values(&state, 1);
            for (column, value) in values {
                let one = oneply(&canonical, column, &params, 1, &mut scratch);
                assert_eq!(one.d1_value.to_bits(), value.to_bits());
                assert_eq!(one.work, 7 + (7.0 * (1.0 - one.values[crate::oneply::TERMINAL_FRAC])).round() as u64);
            }
        }
    }

    #[test]
    fn pruned_values_are_lower_bounds_and_legal() {
        let depth = 4;
        let mut full = PrunedSearcher::deployment(depth, 5, Widths::full(), Prior::Center, 4096);
        for prior in [Prior::Center, Prior::D1] {
            let widths = Widths::parse("2,2", depth).unwrap();
            let mut pruned = PrunedSearcher::deployment(depth, 5, widths, prior, 4096);
            for state in probe_states() {
                let a = full.decide(&state, depth);
                let b = pruned.decide(&state, depth);
                assert_eq!(a.columns, b.columns);
                for (x, y) in a.values.iter().zip(b.values.iter()) {
                    assert!(y <= x, "pruned value {y} above full value {x}");
                }
                assert!(b.work < a.work);
                assert!(b.pruned_nodes > 0);
                let view = PublicView::from_state(&state);
                assert!(view.is_legal(b.action as usize));
            }
        }
    }

    #[test]
    fn widths_parse_and_describe() {
        let w = Widths::parse("3,2", 4).unwrap();
        assert_eq!(w.0[3], 3);
        assert_eq!(w.0[2], 2);
        assert_eq!(w.describe(4), "3,2");
        assert!(Widths::parse("3", 4).is_err());
        assert!(Widths::parse("", 2).unwrap().is_full());
        assert_eq!(Widths::parse("3,3,2", 5).unwrap().describe(5), "3,3,2");
    }

    /// The gameplay path replays deterministically and never plays an
    /// illegal column, also under pruning.
    #[test]
    fn choose_action_replays_and_stays_legal() {
        struct P(PrunedSearcher<DepthTable>);
        impl Policy for P {
            fn name(&self) -> &str {
                "pruned-test"
            }
            fn choose(&mut self, view: &PublicView) -> drop7_kf_linear_q::policy::Decision {
                let d = self.0.choose_action(&view.as_search_state());
                drop7_kf_linear_q::policy::Decision {
                    column: if d.action < 0 { BOARD_SIZE } else { d.action as usize },
                    work: d.work,
                    complete: d.completed_depth >= self.0.params.depth,
                }
            }
        }
        let widths = Widths::parse("2", 3).unwrap();
        let mut a = P(PrunedSearcher::deployment(3, 5, widths, Prior::D1, 4096));
        let mut b = P(PrunedSearcher::deployment(3, 5, widths, Prior::D1, 4096));
        let mut ga = play_game(&mut a, 0, 0xa527_8004, 60);
        let mut gb = play_game(&mut b, 0, 0xa527_8004, 60);
        ga.wall_seconds = 0.0;
        gb.wall_seconds = 0.0;
        assert_eq!(ga, gb);
        assert_eq!(ga.illegal_decisions, 0);
        assert_eq!(ga.incomplete_decisions, 0);
        assert!(ga.moves >= 30);
        let mut c = CenterFirst;
        let gc = play_game(&mut c, 0, 0xa527_8004, 60);
        assert!(gc.moves > 0);
    }
}
