// Policies the evaluation binary can field on identical seeds.  Every policy
// reads a `PublicView` only.

use drop7_rs::board::BOARD_SIZE;
use drop7_rs::rng::{mix32, Mulberry32};
use drop7_rs::search::{work_bound_for, DepthTable, FairLeaf, SearchParams, Searcher};

use crate::features::FEATURE_COUNT;
use crate::learn::greedy_legal;
use crate::view::PublicView;

/// Policy-sampling RNG domain for the random arm; derived from the policy
/// seed and the game's cohort ORDINAL, never from the environment seed.
pub const RANDOM_POLICY_DOMAIN: u32 = 0x504f_4c59; // "POLY"

pub struct Decision {
    pub column: usize,
    pub work: u64,
    pub complete: bool,
}

pub trait Policy: Send {
    fn name(&self) -> &str;
    fn start_game(&mut self, _ordinal: u32) {}
    fn choose(&mut self, view: &PublicView) -> Decision;
}

/// Uniform over legal columns.
pub struct RandomLegal {
    policy_seed: u32,
    rng: Mulberry32,
}

impl RandomLegal {
    pub fn new(policy_seed: u32) -> RandomLegal {
        RandomLegal {
            policy_seed,
            rng: Mulberry32::new(0),
        }
    }
}

impl Policy for RandomLegal {
    fn name(&self) -> &str {
        "random"
    }
    fn start_game(&mut self, ordinal: u32) {
        self.rng = Mulberry32::new(mix32(
            self.policy_seed ^ ordinal.wrapping_add(1).wrapping_mul(0x9e37_79b9) ^ RANDOM_POLICY_DOMAIN,
        ));
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        let legal: Vec<usize> = (0..BOARD_SIZE).filter(|&c| view.is_legal(c)).collect();
        let pick = ((self.rng.next_bits() as u64 * legal.len() as u64) >> 32) as usize;
        Decision {
            column: legal[pick],
            work: 0,
            complete: true,
        }
    }
}

/// The engine's own fallback order: 3, 2, 4, 1, 5, 0, 6.
pub struct CenterFirst;

impl Policy for CenterFirst {
    fn name(&self) -> &str {
        "center"
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        const ORDER: [usize; 7] = [3, 2, 4, 1, 5, 0, 6];
        let column = ORDER.into_iter().find(|&c| view.is_legal(c)).expect("legal column");
        Decision {
            column,
            work: 0,
            complete: true,
        }
    }
}

/// The six-feature linear Q policy with frozen weights.
pub struct LinearQ {
    name: String,
    pub weights: [f64; FEATURE_COUNT],
}

impl LinearQ {
    pub fn new(name: &str, weights: [f64; FEATURE_COUNT]) -> LinearQ {
        LinearQ {
            name: name.to_string(),
            weights,
        }
    }
}

impl Policy for LinearQ {
    fn name(&self) -> &str {
        &self.name
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        Decision {
            column: greedy_legal(&self.weights, view),
            work: 7,
            complete: true,
        }
    }
}

/// Fair expectimax from drop7-rs with the frozen fair leaf: the program's
/// reference family.  Terminal utility and policy seed match the repository's
/// deployment parameters (approaches/lifetime-objective/nnue-evolution).
pub struct FairSearch {
    name: String,
    depth: i32,
    searcher: Searcher<FairLeaf, DepthTable>,
}

impl FairSearch {
    pub fn new(depth: i32, strata: i32, table_entries: usize) -> FairSearch {
        let params = SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        };
        FairSearch {
            name: format!("fair-d{depth}s{strata}"),
            depth,
            searcher: Searcher::new(params, FairLeaf::default(), DepthTable::new(table_entries, 1)),
        }
    }
}

impl Policy for FairSearch {
    fn name(&self) -> &str {
        &self.name
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        let state = view.as_search_state();
        let (action, metrics) = self.searcher.choose_action(&state);
        Decision {
            column: if action < 0 { BOARD_SIZE } else { action as usize },
            work: metrics.work,
            complete: metrics.completed_depth >= self.depth,
        }
    }
}
