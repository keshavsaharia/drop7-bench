// Upstream QLearningAlgorithm (Drop7QLearning.py lines 26-80), with the three
// schedules the report describes exposed as parameters and two additions the
// transfer experiment needs: legal-only action selection and a score-delta
// reward option.  The default construction (`StepSchedule::Upstream`,
// `ExploreSchedule::Upstream`, lambda 0.1, gamma 1) is the shipped code.

use drop7_rs::board::BOARD_SIZE;
use drop7_rs::rng::{mix32, Mulberry32};

use crate::features::{features, Features, FEATURE_COUNT, FEATURE_NAMES};
use crate::view::PublicView;

/// Policy-sampling RNG domain for exploration draws; never the environment.
pub const EXPLORE_DOMAIN: u32 = 0x4558_504c; // "EXPL"

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum StepSchedule {
    /// eta = 1 / t with t counted per MOVE (upstream getStepSize).
    Upstream,
    /// eta = 1 / g with g the number of games started so far.
    PerGame,
    /// eta = 1 / (1 + t / tau), t per move.
    Harmonic(f64),
    Constant(f64),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ExploreSchedule {
    /// epsilon = t^-0.25 with t counted per move (upstream getAction).
    Upstream,
    Constant(f64),
    None,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Reward {
    /// +1 per surviving move, 0 on the terminal transition (upstream).
    Survival,
    /// Corrected-score delta of the move divided by the scale, on every
    /// transition including the terminal one.
    ScoreDelta(f64),
}

/// Q(s, a) = w . phi(s, a), accumulated in the upstream list order so the
/// floating-point result is bit-identical to the Python sum.
#[inline]
pub fn q_of(weights: &[f64; FEATURE_COUNT], f: &Features) -> f64 {
    let mut q = 0.0f64;
    for i in 0..FEATURE_COUNT {
        if f.present[i] {
            q += weights[i] * f.values[i] as f64;
        }
    }
    q
}

/// argmax over LEGAL columns; ties go to the highest column index, which is
/// what Python's `max((Q, action) for action in actions)[1]` does.
pub fn greedy_legal(weights: &[f64; FEATURE_COUNT], view: &PublicView) -> usize {
    let mut best: Option<(f64, usize)> = None;
    for a in 0..BOARD_SIZE {
        if !view.is_legal(a) {
            continue;
        }
        let q = q_of(weights, &features(view, a));
        match best {
            Some((bq, _)) if q < bq => {}
            _ => best = Some((q, a)),
        }
    }
    best.expect("a non-terminal position has a legal column").1
}

pub struct Learner {
    pub weights: [f64; FEATURE_COUNT],
    /// Upstream `numIters`: incremented once per action request.
    pub num_iters: u64,
    pub games_started: u64,
    pub lambda: f64,
    pub gamma: f64,
    pub step: StepSchedule,
    pub explore: ExploreSchedule,
    rng: Mulberry32,
}

impl Learner {
    pub fn new(
        step: StepSchedule,
        explore: ExploreSchedule,
        lambda: f64,
        gamma: f64,
        policy_seed: u32,
    ) -> Learner {
        Learner {
            weights: [0.0; FEATURE_COUNT],
            num_iters: 0,
            games_started: 0,
            lambda,
            gamma,
            step,
            explore,
            rng: Mulberry32::new(mix32(policy_seed ^ EXPLORE_DOMAIN)),
        }
    }

    /// The shipped upstream configuration.
    pub fn upstream(policy_seed: u32) -> Learner {
        Learner::new(StepSchedule::Upstream, ExploreSchedule::Upstream, 0.1, 1.0, policy_seed)
    }

    #[inline]
    pub fn q(&self, f: &Features) -> f64 {
        q_of(&self.weights, f)
    }

    pub fn step_size(&self) -> f64 {
        match self.step {
            StepSchedule::Upstream => 1.0 / self.num_iters as f64,
            StepSchedule::PerGame => 1.0 / self.games_started.max(1) as f64,
            StepSchedule::Harmonic(tau) => 1.0 / (1.0 + self.num_iters as f64 / tau),
            StepSchedule::Constant(eta) => eta,
        }
    }

    pub fn epsilon(&self) -> f64 {
        match self.explore {
            ExploreSchedule::Upstream => 1.0 / (self.num_iters as f64).powf(0.25),
            ExploreSchedule::Constant(eps) => eps,
            ExploreSchedule::None => 0.0,
        }
    }

    /// Upstream getAction: count the request, then epsilon-greedy.  Both the
    /// random draw and the argmax are restricted to legal columns.
    pub fn act(&mut self, view: &PublicView) -> usize {
        self.num_iters += 1;
        let eps = self.epsilon();
        if eps > 0.0 && self.rng.next_unit() < eps {
            let legal: Vec<usize> = (0..BOARD_SIZE).filter(|&c| view.is_legal(c)).collect();
            let pick = ((self.rng.next_bits() as u64 * legal.len() as u64) >> 32) as usize;
            legal[pick]
        } else {
            greedy_legal(&self.weights, view)
        }
    }

    /// Upstream incorporateFeedback with the successor's seven action feature
    /// vectors already computed (the parity gate feeds these from Python).
    /// `next` is None on the terminal transition.
    pub fn update_with(&mut self, phi: &Features, reward: f64, next: Option<&[Features; BOARD_SIZE]>) {
        let eta = self.step_size();
        let q = self.q(phi);
        let v_opt = match next {
            Some(successors) => {
                // Upstream starts the running max at -1, not -inf.
                let mut v_opt = -1.0f64;
                for f in successors.iter() {
                    let v = self.q(f);
                    v_opt = v_opt.max(v);
                }
                v_opt
            }
            None => 0.0,
        };
        let target = reward + self.gamma * v_opt;
        for i in 0..FEATURE_COUNT {
            if phi.present[i] {
                let w = self.weights[i];
                self.weights[i] = w - eta * ((q - target) * phi.values[i] as f64 + self.lambda * w);
            }
        }
    }

    /// Update from the successor public view: computes phi(s', a') for all
    /// seven columns, full ones included, as upstream does.
    pub fn update(&mut self, phi: &Features, reward: f64, next: Option<&PublicView>) {
        match next {
            Some(view) => {
                let mut successors = [Features::default(); BOARD_SIZE];
                for a in 0..BOARD_SIZE {
                    successors[a] = features(view, a);
                }
                self.update_with(phi, reward, Some(&successors));
            }
            None => self.update_with(phi, reward, None),
        }
    }
}

/// Weights file: one `key value` line per feature, upstream keys, Rust `{}`
/// float formatting (round-trips exactly).  Missing keys read as 0.
pub fn save_weights(path: &str, weights: &[f64; FEATURE_COUNT]) -> std::io::Result<()> {
    let mut text = String::new();
    for i in 0..FEATURE_COUNT {
        text.push_str(&format!("{} {:?}\n", FEATURE_NAMES[i], weights[i]));
    }
    std::fs::write(path, text)
}

pub fn load_weights(path: &str) -> Result<[f64; FEATURE_COUNT], String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    let mut weights = [0.0f64; FEATURE_COUNT];
    for (line_no, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut parts = line.split_whitespace();
        let key = parts.next().unwrap();
        let value: f64 = parts
            .next()
            .ok_or_else(|| format!("{path}:{}: missing value", line_no + 1))?
            .parse()
            .map_err(|_| format!("{path}:{}: bad float", line_no + 1))?;
        let index = FEATURE_NAMES
            .iter()
            .position(|&n| n == key)
            .ok_or_else(|| format!("{path}:{}: unknown feature {key}", line_no + 1))?;
        weights[index] = value;
    }
    Ok(weights)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upstream_first_step_moves_weights_by_full_error() {
        // t = 1: eta = 1.  phi = row_dets 2, col_dets 1 (present), reward 1,
        // terminal successor: target 1, Q 0, so each weight becomes
        // 0 - 1 * ((0 - 1) * v + 0) = v.
        let mut learner = Learner::upstream(7);
        learner.num_iters = 1;
        let mut phi = Features::default();
        phi.present[1] = true;
        phi.values[1] = 2;
        phi.present[2] = true;
        phi.values[2] = 1;
        learner.update_with(&phi, 1.0, None);
        assert_eq!(learner.weights[1], 2.0);
        assert_eq!(learner.weights[2], 1.0);
        assert_eq!(learner.weights[0], 0.0);
    }

    #[test]
    fn bootstrap_max_floor_is_minus_one() {
        let mut learner = Learner::upstream(7);
        learner.num_iters = 1;
        learner.weights = [-5.0; FEATURE_COUNT];
        let mut phi = Features::default();
        phi.present[1] = true;
        phi.values[1] = 1;
        let mut succ = Features::default();
        succ.present[1] = true;
        succ.values[1] = 1; // every successor Q = -5, below the -1 floor
        let successors = [succ; BOARD_SIZE];
        learner.update_with(&phi, 1.0, Some(&successors));
        // Q = -5, target = 1 + (-1) = 0, w1 = -5 - 1*((-5 - 0)*1 + 0.1*-5) = -5 + 5.5 = 0.5
        assert!((learner.weights[1] - 0.5).abs() < 1e-12);
    }

    #[test]
    fn weights_round_trip() {
        let dir = std::env::temp_dir().join(format!("kf-weights-{}", std::process::id()));
        let path = dir.to_string_lossy().to_string();
        let w = [1.5, -2.25, 3.0e-7, 478.80700000000002, 0.0, 1.0 / 3.0];
        save_weights(&path, &w).unwrap();
        assert_eq!(load_weights(&path).unwrap(), w);
        std::fs::remove_file(&path).ok();
    }
}
