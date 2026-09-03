//! One-ply linear action values inside fair expectimax.
//!
//! `oneply` computes, for one (state, column) pair, the exact quantities the
//! depth-1 fair search averages over its seven chance strata (the eighteen
//! fair-leaf terms of the afterstate, the score delta and the terminal
//! fraction) plus the six Klein-Friedmann drop features, the rise clock and a
//! bias: 32 numbers whose frozen-weight dot product is the exact d1 value.
//! `prior` ranks a node's legal siblings with one of four priors (centre
//! order, the six-feature CEM Q, the exact d1 value, or a fitted linear Q
//! over the 32 features).  `prune` is the drop7-rs fair search with one
//! change: an interior max node with two or more plies remaining expands
//! only the prior's top-w siblings.  With every width at seven it is the
//! unchanged search, bit for bit, which the tests check.  `panel` plays
//! complete fair-d4s7 games and records every root with exact column values
//! at depths one to four and the 32 one-ply features of every legal sibling.
//!
//! Everything a deployable policy reads here is the public state.

pub mod leaf;
pub mod oneply;
pub mod panel;
pub mod prior;
pub mod prune;

pub use leaf::{LinearLeaf, LinearLeafWeights};
pub use oneply::{oneply, OnePly, ONEPLY_COUNT};
pub use prior::Prior;
pub use prune::{PrunedSearcher, RootDecision, Widths};
