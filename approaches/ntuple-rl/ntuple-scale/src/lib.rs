// drop7-ntuple-scale: a row-and-column n-tuple network trained by
// temporal-coherence TD(0) on chance-state values, deployed as the leaf of the
// depth-3 seven-stratum fair expectimax search.
//
// Approach: approaches/ntuple-rl/ntuple-scale
// Theory:   TH-20260905-ntuple-line-tuples-tc-td-leaf-bcb25133
//
// The crate reuses the proven drop7-rs engine and fair search unchanged (path
// dependency; the reference crate is not modified).  Everything the approach
// adds lives here: pattern indexing straight from the engine's packed column
// words (tuples.rs), the lookup-table model and its temporal-coherence update
// (model.rs), the one-ply chance-state policy that generates training play
// (policy.rs), the training-time search that mirrors the engine's fair
// expectimax and collects its internal nodes as targets (search_train.rs),
// the complete-game harness and population artifacts (game.rs), and the
// train / gate / screen binaries.
//
// INFORMATION BOUNDARY.  The value reads only the public board and the moves
// remaining until the rise; the visible next disc is deliberately not an
// input (the value is a chance-state value, taken after a move resolves and
// before the next disc is known).  Score, level, absolute move number, seed
// and history never reach the tables; the gate binary proves states differing
// only in those fields produce identical values.

pub mod game;
pub mod model;
pub mod policy;
pub mod search_train;
pub mod tuples;

use drop7_rs::search::{work_bound_for, SearchParams};

/// The deployed candidate configuration: depth 3, seven chance strata, the
/// completion-guaranteeing work bound, and the reference policy seed and
/// terminal utility (identical to the nnue-evolution deployment, so the two
/// leaf studies are directly comparable on the same search).
pub fn deployment_params() -> SearchParams {
    SearchParams {
        depth: 3,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(3, 7) + 1,
        policy_seed: 0xd707_5eed,
    }
}

/// The program's standing reference: the fair leaf at depth 4, seven strata.
pub fn reference_d4_params() -> SearchParams {
    SearchParams {
        depth: 4,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(4, 7) + 1,
        policy_seed: 0xd707_5eed,
    }
}

/// Transposition-table capacity for the deployment search (64k entries, the
/// direct-mapped shape the Rust engine benchmark drove to; the table is
/// proven value-neutral, so this choice affects only speed and memory).
pub const DEPLOYMENT_TABLE: usize = 65_536;

/// Reference d4s7 table capacity (1M entries).
pub const REFERENCE_TABLE: usize = 1_048_576;
