// Online Q-learning on the repository engine over a leased training seed range.
//
//   train --seeds-start 0xHEX --games N [--move-cap 2000]
//         [--step upstream|pergame|harmonic:TAU|const:ETA]
//         [--explore upstream|const:EPS|none] [--lambda 0.1] [--gamma 1]
//         [--reward survival|score:SCALE] [--policy-seed 0xHEX]
//         --weights OUT.txt --log OUT.json
//
// Game g uses environment seed seeds-start + g (headless driver).  Exploration
// draws come from the policy-sampling domain seeded by --policy-seed; the
// policy never sees the environment seed.  The log records per-1,000-game
// block means of moves and corrected score (training curve), the final
// weights, the update count and throughput.

use std::time::Instant;

use drop7_kf_linear_q::features::features;
use drop7_kf_linear_q::learn::{save_weights, ExploreSchedule, Learner, Reward, StepSchedule};
use drop7_kf_linear_q::view::PublicView;
use drop7_rs::engine::{play_headless_move, MinimalWaveSink, State};

fn parse_hex(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut seeds_start: Option<u32> = None;
    let mut games = 50_000u32;
    let mut move_cap = 2_000i32;
    let mut step = StepSchedule::Upstream;
    let mut explore = ExploreSchedule::Upstream;
    let mut lambda = 0.1f64;
    let mut gamma = 1.0f64;
    let mut reward = Reward::Survival;
    let mut policy_seed = 0x6b66_0000u32;
    let mut weights_out = String::new();
    let mut log_out = String::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--seeds-start" => seeds_start = Some(parse_hex(v)),
            "--games" => games = v.parse().expect("games"),
            "--move-cap" => move_cap = v.parse().expect("move-cap"),
            "--step" => {
                step = match v {
                    "upstream" => StepSchedule::Upstream,
                    "pergame" => StepSchedule::PerGame,
                    s if s.starts_with("harmonic:") => StepSchedule::Harmonic(s[9..].parse().expect("tau")),
                    s if s.starts_with("const:") => StepSchedule::Constant(s[6..].parse().expect("eta")),
                    _ => panic!("bad --step"),
                }
            }
            "--explore" => {
                explore = match v {
                    "upstream" => ExploreSchedule::Upstream,
                    "none" => ExploreSchedule::None,
                    s if s.starts_with("const:") => ExploreSchedule::Constant(s[6..].parse().expect("eps")),
                    _ => panic!("bad --explore"),
                }
            }
            "--lambda" => lambda = v.parse().expect("lambda"),
            "--gamma" => gamma = v.parse().expect("gamma"),
            "--reward" => {
                reward = match v {
                    "survival" => Reward::Survival,
                    s if s.starts_with("score:") => Reward::ScoreDelta(s[6..].parse().expect("scale")),
                    _ => panic!("bad --reward"),
                }
            }
            "--policy-seed" => policy_seed = parse_hex(v),
            "--weights" => weights_out = v.to_string(),
            "--log" => log_out = v.to_string(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}: every option takes a value", args[i]);
    }
    let seeds_start = seeds_start.expect("--seeds-start required");
    assert!(!weights_out.is_empty() && !log_out.is_empty(), "--weights and --log required");
    let seeds_end = seeds_start as u64 + games as u64;
    eprintln!(
        "train: seeds 0x{seeds_start:08x}..0x{seeds_end:08x} (exclusive) games {games} cap {move_cap} step {step:?} explore {explore:?} lambda {lambda} gamma {gamma} reward {reward:?} policy-seed 0x{policy_seed:08x}"
    );

    let started = Instant::now();
    let mut learner = Learner::new(step, explore, lambda, gamma, policy_seed);
    let mut sink = MinimalWaveSink::default();
    let mut block_moves = 0f64;
    let mut block_score = 0f64;
    let mut block_games = 0u32;
    let mut blocks_moves: Vec<f64> = Vec::new();
    let mut blocks_score: Vec<f64> = Vec::new();
    let mut total_moves = 0u64;
    let mut censored = 0u32;
    for g in 0..games {
        let seed = seeds_start.wrapping_add(g);
        learner.games_started += 1;
        let mut state = State::initial_headless(seed);
        while !state.game_over && state.moves_played < move_cap {
            let view = PublicView::from_state(&state);
            let action = learner.act(&view);
            let phi = features(&view, action);
            let before = state.score;
            play_headless_move(&mut state, seed, action, &mut sink).expect("legal move");
            let delta = state.score - before;
            let r = match (reward, state.game_over) {
                (Reward::Survival, false) => 1.0,
                (Reward::Survival, true) => 0.0,
                (Reward::ScoreDelta(scale), _) => delta as f64 / scale,
            };
            if state.game_over {
                learner.update(&phi, r, None);
            } else {
                let next = PublicView::from_state(&state);
                learner.update(&phi, r, Some(&next));
            }
        }
        if !state.game_over {
            censored += 1;
        }
        total_moves += state.moves_played as u64;
        block_moves += state.moves_played as f64;
        block_score += state.score as f64;
        block_games += 1;
        if block_games == 1000 || g + 1 == games {
            blocks_moves.push(block_moves / block_games as f64);
            blocks_score.push(block_score / block_games as f64);
            block_moves = 0.0;
            block_score = 0.0;
            block_games = 0;
        }
    }
    let wall = started.elapsed().as_secs_f64();
    save_weights(&weights_out, &learner.weights).expect("write weights");
    let fmt = |xs: &[f64]| xs.iter().map(|x| format!("{x}")).collect::<Vec<_>>().join(",");
    let log = format!(
        "{{\"format\":\"drop7-kf-linear-q-train-v1\",\"seedsStartHex\":\"0x{seeds_start:08x}\",\"seedsEndExclusiveHex\":\"0x{seeds_end:08x}\",\"games\":{games},\"moveCap\":{move_cap},\"step\":\"{step:?}\",\"explore\":\"{explore:?}\",\"lambda\":{lambda},\"gamma\":{gamma},\"reward\":\"{reward:?}\",\"policySeedHex\":\"0x{policy_seed:08x}\",\"numIters\":{},\"totalMoves\":{total_moves},\"censoredGames\":{censored},\"weights\":[{}],\"blockMeanMoves1000\":[{}],\"blockMeanScore1000\":[{}],\"wallSeconds\":{wall},\"movesPerSecond\":{}}}\n",
        learner.num_iters,
        fmt(&learner.weights),
        fmt(&blocks_moves),
        fmt(&blocks_score),
        total_moves as f64 / wall.max(1e-9)
    );
    std::fs::write(&log_out, log).expect("write log");
    eprintln!(
        "train: done in {wall:.1}s, {} moves ({:.0} moves/s), last block mean moves {:.2} score {:.0}, weights {:?}",
        total_moves,
        total_moves as f64 / wall.max(1e-9),
        blocks_moves.last().copied().unwrap_or(0.0),
        blocks_score.last().copied().unwrap_or(0.0),
        learner.weights
    );
}
