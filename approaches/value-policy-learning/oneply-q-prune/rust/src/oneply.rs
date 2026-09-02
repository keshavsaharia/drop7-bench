// The 32 one-ply features of dropping the visible disc into a column, under
// exactly the chance scenarios the fair search would use at that node.
//
// Layout (see `feature_names`):
//   0..18   seven-stratum mean of the eighteen fair-leaf terms of the
//           canonical afterstate (terminal strata contribute 0);
//   18      mean score delta over the strata (terminal strata included);
//   19      fraction of strata that end the game;
//   20..26  the six Klein-Friedmann drop features (absent keys read 0);
//   26..31  rise clock one-hot: moves remaining 1..5;
//   31      bias 1.
//
// `d1_value` is the exact depth-1 search value of the column under the same
// scenarios: sum over strata of (score delta + frozen leaf), with the
// terminal utility for strata that die, divided by the stratum count -- the
// same expression in the same order as drop7-rs `Searcher::evaluate_action`
// at depth 1, so it is bit-identical to that value (a test in `prune`
// checks it).

use drop7_kf_linear_q::features::{features, FEATURE_COUNT, FEATURE_NAMES as KF_NAMES};
use drop7_kf_linear_q::view::PublicView;
use drop7_rs::engine::{play_move_sampled, MinimalWaveSink, State};
use drop7_rs::leaf::{fair_leaf_terms, LeafScratch, FROZEN_LEAF_WEIGHTS, LEAF_TERM_NAMES};
use drop7_rs::rng::{sampled_next_disc, scenario_seed_for_state, StratifiedRandom};
use drop7_rs::search::{canonical_state, SearchParams};

pub const LEAF_TERMS: usize = 18;
pub const DELTA_MEAN: usize = LEAF_TERMS;
pub const TERMINAL_FRAC: usize = LEAF_TERMS + 1;
pub const KF_OFFSET: usize = LEAF_TERMS + 2;
pub const RISE_OFFSET: usize = KF_OFFSET + FEATURE_COUNT;
pub const BIAS: usize = RISE_OFFSET + 5;
pub const ONEPLY_COUNT: usize = BIAS + 1;

pub fn feature_names() -> Vec<String> {
    let mut names: Vec<String> = LEAF_TERM_NAMES.iter().map(|n| format!("leaf_{n}")).collect();
    names.push("delta_mean".to_string());
    names.push("terminal_frac".to_string());
    for n in KF_NAMES.iter() {
        names.push(format!("kf_{n}"));
    }
    for m in 1..=5 {
        names.push(format!("rise_{m}"));
    }
    names.push("bias".to_string());
    assert_eq!(names.len(), ONEPLY_COUNT);
    names
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct OnePly {
    pub values: [f64; ONEPLY_COUNT],
    pub d1_value: f64,
    /// Logical work spent: one per move call, one per leaf-term call.
    pub work: u64,
}

/// One-ply features of `column` from the (canonical) `state`, under the
/// scenario seed the search derives for this state at remaining depth
/// `depth`.
pub fn oneply(
    state: &State,
    column: usize,
    params: &SearchParams,
    depth: i32,
    scratch: &mut LeafScratch,
) -> OnePly {
    let state_seed = scenario_seed_for_state(
        &state.board,
        state.next_disc,
        state.moves_remaining,
        params.policy_seed,
        depth,
    );
    let mut values = [0.0f64; ONEPLY_COUNT];
    let mut term_sum = [0.0f64; LEAF_TERMS];
    let mut delta_sum = 0.0f64;
    let mut terminal = 0usize;
    let mut d1 = 0.0f64;
    let mut work = 0u64;
    for sample in 0..params.chance_samples {
        let mut random = StratifiedRandom {
            seed: state_seed,
            sample,
            count: params.chance_samples,
            event: 0,
        };
        let mut sink = MinimalWaveSink::default();
        let played = play_move_sampled(state, column, &mut random, &mut sink);
        work += 1;
        let Some(move_result) = played else {
            d1 += params.terminal_utility;
            terminal += 1;
            continue;
        };
        let score_delta = move_result.score_delta as f64;
        if move_result.state.game_over {
            d1 += score_delta + params.terminal_utility;
            delta_sum += score_delta;
            terminal += 1;
            continue;
        }
        let mut next = move_result.state;
        next.score = 0;
        next.next_disc = sampled_next_disc(state_seed, sample, params.chance_samples);
        let next = canonical_state(&next).0;
        let terms = fair_leaf_terms(&next, scratch);
        work += 1;
        let mut leaf = 0.0f64;
        for j in 0..LEAF_TERMS {
            leaf += FROZEN_LEAF_WEIGHTS[j] * terms[j];
        }
        d1 += score_delta + leaf;
        delta_sum += score_delta;
        for j in 0..LEAF_TERMS {
            term_sum[j] += terms[j];
        }
    }
    let count = params.chance_samples as f64;
    for j in 0..LEAF_TERMS {
        values[j] = term_sum[j] / count;
    }
    values[DELTA_MEAN] = delta_sum / count;
    values[TERMINAL_FRAC] = terminal as f64 / count;
    let view = PublicView::from_state(state);
    let kf = features(&view, column);
    for i in 0..FEATURE_COUNT {
        values[KF_OFFSET + i] = if kf.present[i] { kf.values[i] as f64 } else { 0.0 };
    }
    let clock = (state.moves_remaining - 1).clamp(0, 4) as usize;
    values[RISE_OFFSET + clock] = 1.0;
    values[BIAS] = 1.0;
    OnePly {
        values,
        d1_value: d1 / count,
        work,
    }
}

/// Weights file: one `name value` line per feature in `feature_names`
/// order; every name must appear exactly once.
pub fn load_oneply_weights(path: &str) -> Result<[f64; ONEPLY_COUNT], String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("cannot read {path}: {e}"))?;
    let names = feature_names();
    let mut weights = [0.0f64; ONEPLY_COUNT];
    let mut seen = [false; ONEPLY_COUNT];
    for (line_number, source) in text.lines().enumerate() {
        let line = source.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        let fields: Vec<&str> = line.split_whitespace().collect();
        if fields.len() != 2 {
            return Err(format!("{path}:{}: expected `name value`", line_number + 1));
        }
        let Some(index) = names.iter().position(|n| n == fields[0]) else {
            return Err(format!("{path}:{}: unknown feature {}", line_number + 1, fields[0]));
        };
        if seen[index] {
            return Err(format!("{path}:{}: duplicate feature {}", line_number + 1, fields[0]));
        }
        let value: f64 = fields[1]
            .parse()
            .map_err(|_| format!("{path}:{}: bad value for {}", line_number + 1, fields[0]))?;
        if !value.is_finite() {
            return Err(format!("{path}:{}: non-finite {}", line_number + 1, fields[0]));
        }
        weights[index] = value;
        seen[index] = true;
    }
    if let Some(index) = seen.iter().position(|s| !s) {
        return Err(format!("{path}: missing feature {}", names[index]));
    }
    Ok(weights)
}

pub fn save_oneply_weights(path: &str, weights: &[f64; ONEPLY_COUNT]) -> std::io::Result<()> {
    let names = feature_names();
    let mut text = String::new();
    for i in 0..ONEPLY_COUNT {
        text.push_str(&format!("{} {:?}\n", names[i], weights[i]));
    }
    std::fs::write(path, text)
}

#[inline]
pub fn dot(weights: &[f64; ONEPLY_COUNT], values: &[f64; ONEPLY_COUNT]) -> f64 {
    let mut total = 0.0f64;
    for i in 0..ONEPLY_COUNT {
        total += weights[i] * values[i];
    }
    total
}
