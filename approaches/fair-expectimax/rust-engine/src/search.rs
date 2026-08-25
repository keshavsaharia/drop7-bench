// Fair expectimax search.  Control flow, accumulation order, chance
// stratification, canonicalisation, column order, iterative-deepening
// fallback and work accounting are copied from the proven C++ FastSearch
// (approaches/lifetime-objective/fast-engine/fast-search.hpp), which
// gate-search proved action-, work- and depth-identical to the frozen
// reference.
//
// MEMOIZATION IS A COMPILE-TIME CHOICE.  The C++ search carries a packed-key
// open-addressed strict-LRU table over every interior node.  Measured on real
// decisions, that table's node hit rate is 1.3% at d4s7 and 1.36% at d5s7 --
// but each hit prunes an entire subtree, so the table still eliminates ~64%
// of all work (230M -> 2.2M effective nodes at d4s7).  The interesting
// engineering question is where the payoff per table operation lives, and the
// answer is depth: a hit at depth d prunes ~49^d node expansions, so only
// deep interior nodes are worth caching.  The search here is generic over a
// TranspositionTable with a depth gate; the NoTable and DepthTable
// monomorphisations let the benchmark measure both arms with zero runtime
// branching cost.  Cached values equal recomputed values exactly (the cached
// quantity is a deterministic function of the state), so the table choice
// changes no per-column value, no chosen action and no completed depth --
// only node counts and per-node cost.  The parity gates therefore compare
// per-column values and chosen actions bit-for-bit.

use crate::board::{BOARD_SIZE, EMPTY};
use crate::engine::{center_first_move, play_move_sampled, MinimalWaveSink, State};
use crate::leaf::{fair_leaf, LeafScratch};
use crate::rng::{sampled_next_disc, scenario_seed_for_state, StratifiedRandom};

pub const COLUMN_ORDER: [usize; BOARD_SIZE] = [3, 2, 4, 1, 5, 0, 6];

/// Pluggable leaf evaluator.  Statically dispatched: the search is generic
/// over the leaf, so a different evaluator costs no branching in the hot
/// path.
pub trait Leaf {
    fn value(&mut self, state: &State) -> f64;
}

/// The fair leaf, bit-identical to the C++ fastFairLeaf.
pub struct FairLeaf {
    pub scratch: LeafScratch,
}

impl Default for FairLeaf {
    fn default() -> Self {
        FairLeaf {
            scratch: LeafScratch::default(),
        }
    }
}

impl Leaf for FairLeaf {
    #[inline]
    fn value(&mut self, state: &State) -> f64 {
        fair_leaf(state, &mut self.scratch)
    }
}

/// Injective packed key: the seven column words verbatim (28 bytes) plus
/// next disc, moves remaining and depth -- exactly the information the C++
/// PackedKey carries.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct PackedKey {
    words: [u64; 4],
}

impl PackedKey {
    #[inline]
    fn new(state: &State, depth: i32) -> PackedKey {
        let c = &state.board.cols;
        PackedKey {
            words: [
                c[0] as u64 | ((c[1] as u64) << 32),
                c[2] as u64 | ((c[3] as u64) << 32),
                c[4] as u64 | ((c[5] as u64) << 32),
                c[6] as u64
                    | ((state.next_disc as u64) << 32)
                    | ((state.moves_remaining as u64) << 40)
                    | ((depth as u64) << 48),
            ],
        }
    }
}

#[inline]
fn mix_key(mut value: u64) -> u64 {
    value ^= value >> 33;
    value = value.wrapping_mul(0xff51_afd7_ed55_8ccd);
    value ^= value >> 33;
    value = value.wrapping_mul(0xc4ce_b9fe_1a85_ec53);
    value ^= value >> 33;
    value
}

#[inline]
fn hash_key(key: &PackedKey) -> u64 {
    let mut hash = key.words[0];
    hash = mix_key(hash ^ key.words[1].wrapping_add(0x9e37_79b9_7f4a_7c15));
    hash = mix_key(hash ^ key.words[2].wrapping_add(0xbf58_476d_1ce4_e5b9));
    hash = mix_key(hash ^ key.words[3].wrapping_add(0x94d0_49bb_1331_11eb));
    hash
}

/// Memoization strategy for the search.  Implementations range from a
/// zero-sized no-op to a depth-gated table; the search is generic over the
/// choice, so an unused table compiles to nothing.
pub trait TranspositionTable {
    fn lookup(&mut self, key: &PackedKey, hash: u64, depth: i32) -> Option<f64>;
    fn store(&mut self, key: &PackedKey, hash: u64, depth: i32, value: f64);
    /// O(1) reset between decisions.
    fn clear(&mut self);
    fn bytes(&self) -> usize;
    fn hits(&self) -> u64;
}

/// No memoization: every node is expanded.  Zero-sized; both methods inline
/// to nothing.
pub struct NoTable;

impl TranspositionTable for NoTable {
    #[inline(always)]
    fn lookup(&mut self, _key: &PackedKey, _hash: u64, _depth: i32) -> Option<f64> {
        None
    }
    #[inline(always)]
    fn store(&mut self, _key: &PackedKey, _hash: u64, _depth: i32, _value: f64) {}
    #[inline(always)]
    fn clear(&mut self) {}
    #[inline(always)]
    fn bytes(&self) -> usize {
        0
    }
    #[inline(always)]
    fn hits(&self) -> u64 {
        0
    }
}

/// Depth-gated direct-mapped table.  Only nodes at depth >= `from_depth` are
/// cached: a hit there prunes a large subtree, and the shallow majority of
/// nodes never touch the table at all.  One slot per hash index (no probing
/// chain, no LRU links): a collision replaces the slot when the new node is
/// at least as deep, which keeps the entries with the largest pruning value.
/// An epoch stamp makes the per-decision clear O(1).
///
/// This is the cheapest table that still captures the deep-transposition
/// payoff; the benchmark measures its work reduction against NoTable and
/// against the C++ full-LRU table.
pub struct DepthTable {
    slots: Vec<Slot>,
    mask: usize,
    epoch: u32,
    from_depth: i32,
    hits: u64,
}

#[derive(Clone, Copy)]
struct Slot {
    key: PackedKey,
    value: f64,
    depth: i32,
    epoch: u32,
}

impl DepthTable {
    pub fn new(capacity: usize, from_depth: i32) -> DepthTable {
        let mut slots = 1usize;
        while slots < capacity {
            slots <<= 1;
        }
        DepthTable {
            slots: vec![
                Slot {
                    key: PackedKey { words: [0; 4] },
                    value: 0.0,
                    depth: 0,
                    epoch: 0,
                };
                slots
            ],
            mask: slots - 1,
            epoch: 1,
            from_depth,
            hits: 0,
        }
    }
}

impl TranspositionTable for DepthTable {
    #[inline]
    fn lookup(&mut self, key: &PackedKey, hash: u64, depth: i32) -> Option<f64> {
        if depth < self.from_depth {
            return None;
        }
        let slot = &self.slots[(hash as usize) & self.mask];
        if slot.epoch == self.epoch && slot.key == *key {
            self.hits += 1;
            return Some(slot.value);
        }
        None
    }

    #[inline]
    fn store(&mut self, key: &PackedKey, hash: u64, depth: i32, value: f64) {
        if depth < self.from_depth {
            return;
        }
        let index = (hash as usize) & self.mask;
        let slot = &mut self.slots[index];
        // Depth-preferred replacement: keep the deeper (more pruning-valuable)
        // entry on collision; always refresh a stale or equal-depth slot.
        if slot.epoch != self.epoch || depth >= slot.depth {
            *slot = Slot {
                key: *key,
                value,
                depth,
                epoch: self.epoch,
            };
        }
    }

    #[inline]
    fn clear(&mut self) {
        self.epoch = self.epoch.wrapping_add(1);
        if self.epoch == 0 {
            for slot in self.slots.iter_mut() {
                slot.epoch = 0;
            }
            self.epoch = 1;
        }
        self.hits = 0;
    }

    fn bytes(&self) -> usize {
        self.slots.len() * std::mem::size_of::<Slot>()
    }

    fn hits(&self) -> u64 {
        self.hits
    }
}

#[derive(Clone, Copy)]
pub struct SearchParams {
    pub depth: i32,
    pub chance_samples: i32,
    pub terminal_utility: f64,
    pub maximum_work: u64,
    pub policy_seed: u32,
}

impl Default for SearchParams {
    fn default() -> Self {
        SearchParams {
            depth: 4,
            chance_samples: 5,
            terminal_utility: -1_000_000.0,
            maximum_work: 3_200_000,
            policy_seed: 0xd707_5eed,
        }
    }
}

#[derive(Default, Clone, Copy, Debug)]
pub struct SearchMetrics {
    pub action: i32,
    pub completed_depth: i32,
    pub nodes: u64,
    pub work: u64,
    pub leaf_calls: u64,
    pub move_calls: u64,
    pub cache_hits: u64,
}

/// Raised when the work budget is spent; unwinds to the iterative-deepening
/// driver, which keeps the last completed depth's action.  Public so the
/// decide binary can name the result of a single-column evaluation.
#[derive(Debug)]
pub struct WorkLimitReached;

pub type SearchResult<T> = Result<T, WorkLimitReached>;

pub struct Searcher<L: Leaf, T: TranspositionTable> {
    params: SearchParams,
    pub leaf: L,
    table: T,
    nodes: u64,
    work: u64,
    leaf_calls: u64,
    move_calls: u64,
    last: SearchMetrics,
}

impl<L: Leaf, T: TranspositionTable> Searcher<L, T> {
    pub fn new(params: SearchParams, leaf: L, table: T) -> Searcher<L, T> {
        Searcher {
            params,
            leaf,
            table,
            nodes: 0,
            work: 0,
            leaf_calls: 0,
            move_calls: 0,
            last: SearchMetrics::default(),
        }
    }

    pub fn table_bytes(&self) -> usize {
        self.table.bytes()
    }

    #[inline]
    fn check_budget(&self) -> SearchResult<()> {
        if self.work >= self.params.maximum_work {
            return Err(WorkLimitReached);
        }
        Ok(())
    }

    fn evaluate_action(
        &mut self,
        state: &State,
        column: usize,
        depth: i32,
    ) -> SearchResult<f64> {
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
            next.next_disc =
                sampled_next_disc(state_seed, sample, self.params.chance_samples);
            let next = canonical_state(&next).0;
            value += score_delta + self.best_future_value(&next, depth - 1)?;
        }
        Ok(value / self.params.chance_samples as f64)
    }

    fn evaluate_leaf(&mut self, state: &State) -> SearchResult<f64> {
        self.check_budget()?;
        self.work += 1;
        self.leaf_calls += 1;
        let value = self.leaf.value(state);
        if !value.is_finite() {
            panic!("leaf evaluator returned a non-finite value");
        }
        Ok(value)
    }

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
        let mut best = f64::NEG_INFINITY;
        for &column in COLUMN_ORDER.iter() {
            if state.board.get(0, column) != EMPTY {
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

    fn root_decision(&mut self, canonical: &State, depth: i32) -> SearchResult<i32> {
        let mut action = -1i32;
        let mut best_value = f64::NEG_INFINITY;
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) != EMPTY {
                continue;
            }
            let value = self.evaluate_action(canonical, column, depth)?;
            if value > best_value {
                best_value = value;
                action = column as i32;
            }
        }
        Ok(action)
    }

    /// Decide-binary support: evaluate one root column of an
    /// already-canonical state at a fixed depth, exactly as root_decision
    /// does, and record the work the evaluation cost.  The table persists
    /// across calls within a decision, as it does across the columns of one
    /// root_decision; the value returned is the same bits either way (the
    /// table is cache-independent).
    pub fn evaluate_root_column(
        &mut self,
        canonical: &State,
        column: usize,
        depth: i32,
    ) -> SearchResult<f64> {
        self.nodes = 0;
        self.work = 0;
        self.leaf_calls = 0;
        self.move_calls = 0;
        let value = self.evaluate_action(canonical, column, depth)?;
        self.last = SearchMetrics {
            action: column as i32,
            completed_depth: depth,
            nodes: self.nodes,
            work: self.work,
            leaf_calls: self.leaf_calls,
            move_calls: self.move_calls,
            cache_hits: self.table.hits(),
        };
        Ok(value)
    }

    /// The metrics recorded by the most recent evaluate_root_column call.
    pub fn last_metrics(&self) -> &SearchMetrics {
        &self.last
    }

    /// Gate support: evaluate every legal column of `state` at a fixed depth
    /// (no iterative deepening, no work limit) on the canonical state, and
    /// return the per-column values in COLUMN_ORDER plus the chosen
    /// (unmirrored) action.  Values are table-independent.
    pub fn column_values(&mut self, state: &State, depth: i32) -> (Vec<(usize, f64)>, i32) {
        let (canonical, mirrored) = canonical_state(state);
        let mut values = Vec::new();
        let mut action = -1i32;
        let mut best_value = f64::NEG_INFINITY;
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) != EMPTY {
                continue;
            }
            let value = self
                .evaluate_action(&canonical, column, depth)
                .expect("unbounded work");
            values.push((column, value));
            if value > best_value {
                best_value = value;
                action = column as i32;
            }
        }
        if mirrored && action >= 0 {
            action = BOARD_SIZE as i32 - 1 - action;
        }
        (values, action)
    }

    /// Choose a column.  Mirrors FastSearch::chooseAction: canonicalise,
    /// iterative deepening with a work budget, center-first fallback, and
    /// unmirror the answer.
    pub fn choose_action(&mut self, source: &State) -> (i32, SearchMetrics) {
        let mut metrics = SearchMetrics::default();
        if source.game_over {
            return (-1, metrics);
        }
        let (canonical, mirrored) = canonical_state(source);
        self.table.clear();
        self.nodes = 0;
        self.work = 0;
        self.leaf_calls = 0;
        self.move_calls = 0;
        let mut action = -1i32;
        let mut completed_depth = 0i32;
        for depth in 1..=self.params.depth {
            match self.root_decision(&canonical, depth) {
                Ok(candidate) => {
                    if candidate < 0 {
                        break;
                    }
                    action = candidate;
                    completed_depth = depth;
                }
                Err(WorkLimitReached) => break,
            }
        }
        if action < 0 {
            action = center_first_move(&canonical.board).map(|c| c as i32).unwrap_or(-1);
        }
        metrics.completed_depth = completed_depth;
        metrics.nodes = self.nodes;
        metrics.work = self.work;
        metrics.leaf_calls = self.leaf_calls;
        metrics.move_calls = self.move_calls;
        metrics.cache_hits = self.table.hits();
        metrics.action = if mirrored && action >= 0 {
            BOARD_SIZE as i32 - 1 - action
        } else {
            action
        };
        (metrics.action, metrics)
    }
}

/// Canonicalise under horizontal reflection, zeroing the score, exactly as
/// the C++ canonicalStateFast.
pub fn canonical_state(state: &State) -> (State, bool) {
    let mirrored = state.board.mirrored_is_smaller();
    let mut result = *state;
    result.score = 0;
    if mirrored {
        result.board = state.board.mirrored();
    }
    (result, mirrored)
}

/// The work bound that guarantees a configuration completes, copied from the
/// C++ bench's workBoundFor.
pub fn work_bound_for(depth: i32, strata: i32) -> u64 {
    let branches = BOARD_SIZE as u64 * strata as u64;
    let mut total = 0u64;
    for level in 1..=depth {
        let mut power = 1u64;
        for _ in 0..level {
            power *= branches;
        }
        for inner in 1..=level {
            let mut inner_power = 1u64;
            for _ in 0..inner {
                inner_power *= branches;
            }
            total += inner_power;
        }
        total += power;
    }
    total
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::State;

    /// A second, deliberately trivial leaf: proves the searcher is generic
    /// over the evaluator (the pluggable-leaf contract) and that a different
    /// leaf changes the chosen action without touching the search.
    struct ZeroLeaf;
    impl Leaf for ZeroLeaf {
        fn value(&mut self, _state: &State) -> f64 {
            0.0
        }
    }

    #[test]
    fn the_searcher_accepts_a_pluggable_leaf() {
        let params = SearchParams {
            depth: 2,
            chance_samples: 3,
            ..SearchParams::default()
        };
        let state = State::initial_headless(0xa527_7001);
        let mut searcher = Searcher::new(params, ZeroLeaf, NoTable);
        let (action, metrics) = searcher.choose_action(&state);
        assert!((0..7).contains(&action));
        assert_eq!(metrics.completed_depth, 2);
        assert!(metrics.work > 0);
    }

    /// The decide binary's contract: with a completion-guaranteeing budget,
    /// the per-column depth-D argmax (what the root-parallel decide computes)
    /// is exactly the action choose_action returns after iterative deepening.
    #[test]
    fn root_parallel_argmax_matches_choose_action() {
        for seed in [0xa527_7003u32, 0xa527_7004, 0xa527_7005] {
            let state = State::initial_headless(seed);
            let params = SearchParams {
                depth: 4,
                chance_samples: 5,
                maximum_work: work_bound_for(4, 5) + 1,
                ..SearchParams::default()
            };
            let mut sequential = Searcher::new(params, FairLeaf::default(), NoTable);
            let (expected, metrics) = sequential.choose_action(&state);
            assert_eq!(metrics.completed_depth, 4, "the budget must complete");

            let (canonical, mirrored) = canonical_state(&state);
            let mut parallel = Searcher::new(params, FairLeaf::default(), NoTable);
            let mut action = -1i32;
            let mut best = f64::NEG_INFINITY;
            for &column in COLUMN_ORDER.iter() {
                if canonical.board.get(0, column) == EMPTY {
                    let value = parallel
                        .evaluate_root_column(&canonical, column, 4)
                        .expect("budget covers one column");
                    if value > best {
                        best = value;
                        action = column as i32;
                    }
                }
            }
            if mirrored && action >= 0 {
                action = BOARD_SIZE as i32 - 1 - action;
            }
            assert_eq!(action, expected, "seed {seed:#x}");
        }
    }

    #[test]
    fn depth_gated_table_matches_no_table_values() {
        // Cache-independence, the property the values gate checks against the
        // C++ search, checked here between the two Rust arms.
        let params = SearchParams {
            depth: 3,
            chance_samples: 5,
            maximum_work: 1u64 << 62,
            ..SearchParams::default()
        };
        let state = State::initial_headless(0xa527_7002);
        let mut plain = Searcher::new(params, FairLeaf::default(), NoTable);
        let mut cached = Searcher::new(
            params,
            FairLeaf::default(),
            DepthTable::new(1024, 1),
        );
        let (plain_values, plain_action) = plain.column_values(&state, 3);
        let (cached_values, cached_action) = cached.column_values(&state, 3);
        assert_eq!(plain_action, cached_action);
        for ((pc, pv), (cc, cv)) in plain_values.iter().zip(cached_values.iter()) {
            assert_eq!(pc, cc);
            assert_eq!(pv.to_bits(), cv.to_bits());
        }
    }
}
