// drop7-rs: a semantics-preserving bit-packed Drop7 engine and fair
// expectimax search.
//
// EQUIVALENCE CONTRACT.  Boards, scores, wave lists, reveal streams, leaf
// value bit patterns, per-column search values and chosen actions are
// required to be identical to the frozen C++ reference
// (src/core/native/engine.hpp), the proven C++ fast engine
// (approaches/lifetime-objective/fast-engine) and the TypeScript engine
// (src/core/typescript/engine.ts).  The differences are representation and
// allocation only; see board.rs for the packing contract.
//
// This crate is an engineering artifact at the CHECK tier: it makes no
// strength claim and consumes no cohort data.

pub mod board;
pub mod engine;
pub mod leaf;
pub mod parallel;
pub mod rng;
pub mod search;
pub mod tables;

pub use board::{Board, Scan};
pub use engine::{FullWaveSink, MoveResult, State, Wave};
pub use parallel::{
    choose_action_frontier_parallel, choose_action_frontier_parallel_with_leaf,
    choose_action_root_parallel, choose_action_root_parallel_with_leaf, plan_parallel_resources,
    ParallelConfig, ParallelDecision, ParallelMetrics, ParallelResourcePlan, ParallelScheduler,
    WorkerMetrics,
};
pub use search::{
    canonical_state, DepthTable, FairLeaf, Leaf, NoTable, SearchMetrics, SearchParams, Searcher,
    TranspositionTable, WeightedLeaf,
};
