// drop7-nnue-evolution: a depth-5-distilled, whole-game-evolved NNUE leaf
// for the depth-3 fair expectimax search.
//
// Approach: approaches/lifetime-objective/nnue-evolution
// Theory:   TH-20260825-evolved-nnue-leaf-d3-0f47e46c
//
// The crate reuses the proven drop7-rs engine and fair search unchanged (path
// dependency; the reference crate is not modified).  Everything the approach
// adds — the NNUE leaf, the teacher-corpus generator, the supervised
// pretrainer, and the evolutionary driver — lives here.
//
// INFORMATION BOUNDARY.  The NNUE reads only the public state (visible board,
// visible next disc, moves until the rise); see features.rs.  The deployed
// policy is the stock fair expectimax at depth 3 / 7 strata with the NNUE as
// its leaf: no seed identity, score, level, or history reaches any decision.

pub mod features;
pub mod game;
pub mod json;
pub mod nnue;
pub mod schedule;

use drop7_rs::search::{work_bound_for, SearchParams};

/// The deployed candidate configuration: depth 3, seven chance strata, the
/// completion-guaranteeing work bound, and the reference policy seed and
/// terminal utility (the same constants the leaf-evolution CMA-ES experiment
/// froze, so the two evolution studies are directly comparable).
pub fn deployment_params() -> SearchParams {
    SearchParams {
        depth: 3,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(3, 7) + 1,
        policy_seed: 0xd707_5eed,
    }
}

/// The teacher configuration: depth 5, seven strata — one full five-move
/// rise cycle of lookahead beyond the root, so rise-boundary effects the
/// depth-3 deployment cannot see are visible to the teacher and distillable
/// into the leaf.
pub fn teacher_params() -> SearchParams {
    SearchParams {
        depth: 5,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(5, 7) + 1,
        policy_seed: 0xd707_5eed,
    }
}

/// Transposition-table capacity for the deployment search (64k entries, the
/// cheap direct-mapped shape the Rust engine benchmark drove to; the table is
/// proven value-neutral, so this choice affects only speed and memory).
pub const DEPLOYMENT_TABLE: usize = 65_536;

/// Teacher table capacity (1M entries; the measured fastest d5s7 arm).
pub const TEACHER_TABLE: usize = 1_048_576;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::game::play_game;
    use crate::nnue::{Nnue, NnueLeaf};
    use drop7_rs::search::{DepthTable, FairLeaf, Searcher};

    /// The deployed shape plays a legal game with a random-weight NNUE leaf:
    /// no illegal or incomplete decisions, finished depth 3 everywhere, and a
    /// replay reproduces the score exactly.  A 60-move cap keeps the debug
    /// build's test fast; the gates exercise full games in release.
    #[test]
    fn random_nnue_leaf_plays_a_legal_deterministic_game() {
        let net = Nnue::random(0x0e701_e52);
        let params = deployment_params();
        let mut searcher = Searcher::new(
            params,
            NnueLeaf { net: net.clone() },
            DepthTable::new(DEPLOYMENT_TABLE, 1),
        );
        let game = play_game(&mut searcher, 0xa527_7006, 60, params.depth);
        assert_eq!(game.illegal_decisions, 0);
        assert_eq!(game.incomplete_decisions, 0);
        assert!(game.moves > 0);

        let mut again = Searcher::new(
            params,
            NnueLeaf { net },
            DepthTable::new(DEPLOYMENT_TABLE, 1),
        );
        let replay = play_game(&mut again, 0xa527_7006, 60, params.depth);
        assert_eq!(game.score, replay.score);
        assert_eq!(game.moves, replay.moves);
        assert_eq!(game.work, replay.work);
    }

    /// The fair leaf at the deployment configuration is the ablation
    /// comparator; pin its determinism here too.
    #[test]
    fn fair_leaf_d3s7_is_deterministic() {
        let params = deployment_params();
        let mut a = Searcher::new(params, FairLeaf::default(), DepthTable::new(DEPLOYMENT_TABLE, 1));
        let mut b = Searcher::new(params, FairLeaf::default(), DepthTable::new(DEPLOYMENT_TABLE, 1));
        let ga = play_game(&mut a, 0xa527_7007, 60, params.depth);
        let gb = play_game(&mut b, 0xa527_7007, 60, params.depth);
        assert_eq!(ga.score, gb.score);
        assert_eq!(ga.moves, gb.moves);
    }
}
