//! Klein & Friedmann's six-feature linear Q-learner (Stanford CS221 final
//! report "Drop7"; code github.com/ekreate/cs221-final-project @ 8cc8a0e) on
//! the drop7-rs bitboard engine.
//!
//! What is ported exactly: the feature extractor (`features`) and the
//! Q-learning update (`Learner::update`), both gated against the upstream
//! Python on exported data (`parity_features`, `parity_update`).  What is
//! necessarily different, and disclosed in
//! EX-20260902-kf-linear-q-rust-transfer-4328a730: the rules engine is the
//! repository's proven Hardcore engine rather than the upstream simulator, and
//! the argmax and exploration draw range over LEGAL columns only (upstream
//! lets the agent drop onto a full column and die).
//!
//! The deployable policy reads a `PublicView` and nothing else.

pub mod features;
pub mod game;
pub mod learn;
pub mod policy;
pub mod view;

pub use features::{features, Features, FEATURE_COUNT, FEATURE_NAMES};
pub use learn::{greedy_legal, q_of, ExploreSchedule, Learner, Reward, StepSchedule};
pub use view::PublicView;
