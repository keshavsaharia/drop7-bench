// Temporal-coherence TD(0) training of the n-tuple tables on the Rust engine.
//
// The learner plays complete games with the one-ply chance-state policy
// (policy.rs) over the current tables, and after every move nudges the
// entries of the previous chance state toward the move's points plus the
// value of the new chance state (zero after a terminal move).  Workers run
// asynchronously on shared tables; training is chunked so the driver can
// write a progress row, a validation artifact and a checkpoint at fixed
// move counts.
//
// OUTPUT (all under --out):
//   config.json          the configuration, written once and re-validated on resume
//   progress.jsonl       one row per chunk (moves, games, training means, TD statistics)
//   val-<moves>.json     population artifact of the validation line-up at that point
//                        (ntuple-d3s7, ntuple-1ply, fair-d3s7 on the validation block)
//   best-weights.bin     the tables at the validation point with the largest paired
//                        margin of ntuple-d3s7 over fair-d3s7 (weights only)
//   best.json            which validation point best-weights.bin came from
//   latest-weights.bin   the tables at the last completed chunk (weights only)
//   checkpoint.bin       weights plus coherence accumulators, for --resume
//   stop.json            why the run stopped (moves, wall, plateau, stop-file)
//   DONE                 written when the run stops
//
// STOPPING.  The run stops when the move budget or the wall budget is spent,
// when a file named STOP appears in --out, or, with --plateau-window W, when
// the validation margins have plateaued: at the first validation point k
// (counting from 1) with k >= --plateau-min-points and k >= 2W at which the
// mean paired margin of the last W points is not above the mean of the W
// points before them.  Checkpoints are written every --checkpoint-every
// validation points and at the end.
//
// SEEDS.  Training games take seeds in order from [--seeds-start,
// --seeds-start + --seeds-count), wrapping to the start when the block is
// exhausted (a training-role block may be re-read; the wrap count is
// recorded).  Validation games read [--validate-start, +--validate-games).

use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering::Relaxed};
use std::sync::{Arc, Mutex};
use std::time::Instant;

use drop7_ntuple_scale::game::{
    atomic_write, evaluate_arm, mean_moves, mean_score, population_artifact_json, Arm, GameRecord,
    Individual, MOVE_CAP,
};
use drop7_ntuple_scale::model::{Layout, Model, UpdateStats, MAX_ACTIVE, VALUE_SCALE};
use drop7_ntuple_scale::policy::{choose, PolicyParams};
use drop7_rs::engine::{play_headless_move, FullWaveSink, State};
use drop7_rs::rng::Mulberry32;

struct Config {
    layout: String,
    seeds_start: u32,
    seeds_count: u32,
    moves: u64,
    chunk_moves: u64,
    threads: usize,
    alpha: f32,
    epsilon: f32,
    optimistic: f32,
    delta_clamp: f32,
    reveal_samples: i32,
    out: PathBuf,
    resume: bool,
    validate_start: u32,
    validate_games: usize,
    validate_every: u64,
    quick_every: u64,
    wall_seconds: u64,
    move_cap: i32,
    train_seed: u32,
    experiment_id: String,
    /// Write checkpoint.bin (weights plus coherence accumulators) every
    /// `checkpoint_every` validation points and at the end.  Off for pilot
    /// and smoke arms, whose tables are never resumed.
    checkpoint: bool,
    checkpoint_every: usize,
    /// Plateau rule window W (0 = no plateau rule) and the minimum number of
    /// validation points before the rule may fire.
    plateau_window: usize,
    plateau_min_points: usize,
}

fn parse_hex(text: &str, what: &str) -> Result<u32, String> {
    u32::from_str_radix(text.trim_start_matches("0x"), 16).map_err(|_| format!("bad {what}: {text}"))
}

fn parse_args() -> Result<Config, String> {
    let mut config = Config {
        layout: "rows,cols,win23,win32,phase=cols".into(),
        seeds_start: 0,
        seeds_count: 0,
        moves: 1_000_000_000,
        chunk_moves: 5_000_000,
        threads: 32,
        alpha: 1.0,
        epsilon: 0.0,
        optimistic: 20.0,
        delta_clamp: 30.0,
        reveal_samples: 7,
        out: PathBuf::new(),
        resume: false,
        validate_start: 0,
        validate_games: 64,
        validate_every: 100_000_000,
        quick_every: 25_000_000,
        wall_seconds: 0,
        move_cap: MOVE_CAP,
        train_seed: 0x7d0c_5eed,
        experiment_id: "EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620".into(),
        checkpoint: true,
        checkpoint_every: 1,
        plateau_window: 0,
        plateau_min_points: 8,
    };
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let flag = args[i].as_str();
        if flag == "--resume" {
            config.resume = true;
            i += 1;
            continue;
        }
        if flag == "--no-checkpoint" {
            config.checkpoint = false;
            i += 1;
            continue;
        }
        let value = args.get(i + 1).map(|s| s.as_str()).ok_or(format!("{flag} needs a value"))?;
        match flag {
            "--layout" => config.layout = value.to_string(),
            "--seeds-start" => config.seeds_start = parse_hex(value, "--seeds-start")?,
            "--seeds-count" => config.seeds_count = value.parse().map_err(|_| "bad --seeds-count")?,
            "--moves" => config.moves = value.parse().map_err(|_| "bad --moves")?,
            "--chunk-moves" => config.chunk_moves = value.parse().map_err(|_| "bad --chunk-moves")?,
            "--threads" => config.threads = value.parse().map_err(|_| "bad --threads")?,
            "--alpha" => config.alpha = value.parse().map_err(|_| "bad --alpha")?,
            "--epsilon" => config.epsilon = value.parse().map_err(|_| "bad --epsilon")?,
            "--optimistic" => config.optimistic = value.parse().map_err(|_| "bad --optimistic")?,
            "--delta-clamp" => config.delta_clamp = value.parse().map_err(|_| "bad --delta-clamp")?,
            "--reveal-samples" => config.reveal_samples = value.parse().map_err(|_| "bad --reveal-samples")?,
            "--out" => config.out = PathBuf::from(value),
            "--validate-start" => config.validate_start = parse_hex(value, "--validate-start")?,
            "--validate-games" => config.validate_games = value.parse().map_err(|_| "bad --validate-games")?,
            "--validate-every" => config.validate_every = value.parse().map_err(|_| "bad --validate-every")?,
            "--quick-every" => config.quick_every = value.parse().map_err(|_| "bad --quick-every")?,
            "--wall-seconds" => config.wall_seconds = value.parse().map_err(|_| "bad --wall-seconds")?,
            "--move-cap" => config.move_cap = value.parse().map_err(|_| "bad --move-cap")?,
            "--train-seed" => config.train_seed = parse_hex(value, "--train-seed")?,
            "--experiment-id" => config.experiment_id = value.to_string(),
            "--checkpoint-every" => config.checkpoint_every = value.parse().map_err(|_| "bad --checkpoint-every")?,
            "--plateau-window" => config.plateau_window = value.parse().map_err(|_| "bad --plateau-window")?,
            "--plateau-min-points" => config.plateau_min_points = value.parse().map_err(|_| "bad --plateau-min-points")?,
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    if config.out.as_os_str().is_empty() {
        return Err("--out is required".into());
    }
    if config.seeds_count == 0 {
        return Err("--seeds-count must be positive".into());
    }
    if config.validate_every > 0 && config.validate_start == 0 {
        return Err("--validate-start is required when validation is on".into());
    }
    if config.checkpoint_every == 0 {
        return Err("--checkpoint-every must be positive".into());
    }
    if config.plateau_window > 0 && config.validate_every == 0 {
        return Err("--plateau-window needs validation".into());
    }
    Ok(config)
}

fn config_json(config: &Config, layout: &Layout) -> String {
    format!(
        "{{\"format\":\"drop7-ntuple-scale-train-config-v1\",\"experiment\":\"{}\",\"layout\":\"{}\",\"entries\":{},\"activePerState\":{},\"seedsStartHex\":\"0x{:08x}\",\"seedsCount\":{},\"moves\":{},\"chunkMoves\":{},\"threads\":{},\"alpha\":{},\"epsilon\":{},\"optimisticRiseUnits\":{},\"deltaClampRiseUnits\":{},\"revealSamples\":{},\"validateStartHex\":\"0x{:08x}\",\"validateGames\":{},\"validateEvery\":{},\"quickEvery\":{},\"wallSeconds\":{},\"moveCap\":{},\"trainSeedHex\":\"0x{:08x}\",\"valueUnitPoints\":{},\"checkpoint\":{},\"checkpointEvery\":{},\"plateauWindow\":{},\"plateauMinPoints\":{}}}\n",
        config.experiment_id,
        layout.spec(),
        layout.total_entries(),
        layout.active_count(),
        config.seeds_start,
        config.seeds_count,
        config.moves,
        config.chunk_moves,
        config.threads,
        config.alpha,
        config.epsilon,
        config.optimistic,
        config.delta_clamp,
        config.reveal_samples,
        config.validate_start,
        config.validate_games,
        config.validate_every,
        config.quick_every,
        config.wall_seconds,
        config.move_cap,
        config.train_seed,
        VALUE_SCALE,
        config.checkpoint,
        config.checkpoint_every,
        config.plateau_window,
        config.plateau_min_points,
    )
}

/// The plateau rule on the validation margins so far: (recent window mean,
/// previous window mean, stop).  None while the rule cannot fire yet.
fn plateau_check(margins: &[f64], window: usize, min_points: usize) -> Option<(f64, f64, bool)> {
    if window == 0 || margins.len() < min_points || margins.len() < 2 * window {
        return None;
    }
    let k = margins.len();
    let recent = margins[k - window..].iter().sum::<f64>() / window as f64;
    let previous = margins[k - 2 * window..k - window].iter().sum::<f64>() / window as f64;
    Some((recent, previous, recent <= previous))
}

/// Minimal JSON number reader for our own progress rows: the value after
/// `"key":`.
fn json_number(line: &str, key: &str) -> Option<f64> {
    let needle = format!("\"{key}\":");
    let start = line.find(&needle)? + needle.len();
    let rest = &line[start..];
    let end = rest
        .find(|c: char| !(c.is_ascii_digit() || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+'))
        .unwrap_or(rest.len());
    rest[..end].parse().ok()
}

#[derive(Default, Clone, Copy)]
struct ChunkStats {
    games: u64,
    moves: u64,
    score_sum: f64,
    max_score: i64,
    clears: u64,
    reveals: u64,
    updates: UpdateStats,
}

struct Shared {
    moves: AtomicU64,
    games: AtomicU64,
    cursor: AtomicU64,
}

/// Play training games with TD updates until the shared move counter reaches
/// `chunk_end`.
#[allow(clippy::too_many_arguments)]
fn worker(
    model: &Model,
    config: &Config,
    params: &PolicyParams,
    shared: &Shared,
    chunk_end: u64,
    worker_index: usize,
) -> ChunkStats {
    let mut stats = ChunkStats::default();
    let mut scratch_prev = [0u64; MAX_ACTIVE];
    let mut scratch_next = [0u64; MAX_ACTIVE];
    let mut scratch_policy = [0u64; MAX_ACTIVE];
    let mut sink = FullWaveSink::new();
    let mut rng = Mulberry32::new(
        config.train_seed ^ (worker_index as u32).wrapping_mul(0x9e37_79b9),
    );
    while shared.moves.load(Relaxed) < chunk_end {
        let index = shared.cursor.fetch_add(1, Relaxed);
        let seed = config.seeds_start.wrapping_add((index % config.seeds_count as u64) as u32);
        let mut state = State::initial_headless(seed);
        let mut n_prev = model.features(&state.board, state.moves_remaining, &mut scratch_prev);
        let mut game_moves = 0u64;
        while !state.game_over && state.moves_played < config.move_cap {
            let decision = choose(model, &state, params, &mut scratch_policy);
            let mut action = decision.action;
            if action < 0 {
                break;
            }
            if config.epsilon > 0.0 && rng.next_unit() < config.epsilon as f64 {
                // Uniform over legal columns.
                let legal: Vec<usize> = (0..7).filter(|&c| state.board.is_legal(c)).collect();
                if !legal.is_empty() {
                    action = legal[(rng.next_bits() as usize) % legal.len()] as i32;
                }
            }
            sink.clear();
            let Some(result) = play_headless_move(&mut state, seed, action as usize, &mut sink) else {
                break;
            };
            for wave in sink.waves.iter().take(sink.count) {
                stats.clears += wave.cleared as u64;
                stats.reveals += wave.revealed as u64;
            }
            let reward = result.score_delta as f32 / VALUE_SCALE as f32;
            let target = if state.game_over {
                reward
            } else {
                let n = model.features(&state.board, state.moves_remaining, &mut scratch_next);
                reward + model.value_of(&scratch_next[..n])
            };
            model.update(
                &scratch_prev[..n_prev],
                target,
                config.alpha,
                config.delta_clamp,
                &mut stats.updates,
            );
            if !state.game_over {
                std::mem::swap(&mut scratch_prev, &mut scratch_next);
                n_prev = model.layout.active_count();
            }
            game_moves += 1;
        }
        shared.moves.fetch_add(game_moves, Relaxed);
        shared.games.fetch_add(1, Relaxed);
        stats.games += 1;
        stats.moves += game_moves;
        stats.score_sum += state.score as f64;
        stats.max_score = stats.max_score.max(state.score);
    }
    stats
}

struct Validation {
    moves: u64,
    artifact: String,
    ntuple_d3_mean: f64,
    direct_mean: f64,
    fair_d3_mean: f64,
    paired_delta_d3: f64,
    wins_d3: usize,
    losses_d3: usize,
    paired_delta_direct: f64,
    touched: u64,
    is_best: bool,
}

fn paired(cand: &[GameRecord], reference: &[GameRecord]) -> (f64, usize, usize) {
    let mut sum = 0.0;
    let mut wins = 0;
    let mut losses = 0;
    for (a, b) in cand.iter().zip(reference.iter()) {
        let d = a.score as f64 - b.score as f64;
        sum += d;
        if d > 0.0 {
            wins += 1;
        } else if d < 0.0 {
            losses += 1;
        }
    }
    (sum / cand.len().max(1) as f64, wins, losses)
}

fn main() -> Result<(), String> {
    let config = parse_args()?;
    let layout = Layout::parse(&config.layout)?;
    std::fs::create_dir_all(&config.out).map_err(|e| e.to_string())?;
    let config_path = config.out.join("config.json");
    let config_text = config_json(&config, &layout);
    if config_path.exists() {
        let existing = std::fs::read_to_string(&config_path).map_err(|e| e.to_string())?;
        if existing != config_text {
            return Err("config.json differs from this invocation; refusing (a run's configuration is fixed)".into());
        }
    } else {
        atomic_write(&config_path, config_text.as_bytes())?;
    }
    let started = Instant::now();
    eprintln!(
        "layout {} : {} entries ({:.2} GB weights, {:.2} GB with coherence), {} active per state",
        layout.spec(),
        layout.total_entries(),
        layout.total_entries() as f64 * 4.0 / 1e9,
        layout.total_entries() as f64 * 12.0 / 1e9,
        layout.active_count()
    );

    // Resume state.
    let progress_path = config.out.join("progress.jsonl");
    let mut chunk_index = 0u64;
    let mut moves_total = 0u64;
    let mut games_total = 0u64;
    let mut wall_offset = 0.0f64;
    let mut cursor_start = 0u64;
    let mut best_margin = f64::NEG_INFINITY;
    let mut margins: Vec<f64> = Vec::new();
    let checkpoint_path = config.out.join("checkpoint.bin");
    let model = if config.resume {
        let text = std::fs::read_to_string(&progress_path).unwrap_or_default();
        if let Some(last) = text.lines().filter(|l| !l.trim().is_empty()).last() {
            chunk_index = json_number(last, "chunk").unwrap_or(0.0) as u64 + 1;
            moves_total = json_number(last, "movesTotal").unwrap_or(0.0) as u64;
            games_total = json_number(last, "gamesTotal").unwrap_or(0.0) as u64;
            wall_offset = json_number(last, "wallSeconds").unwrap_or(0.0);
            cursor_start = json_number(last, "seedCursor").unwrap_or(0.0) as u64;
        }
        for line in text.lines() {
            if line.contains("\"validation\":{") {
                if let Some(margin) = json_number(line, "pairedDeltaD3") {
                    margins.push(margin);
                }
            }
        }
        if !margins.is_empty() {
            eprintln!("resume: {} validation points on record", margins.len());
        }
        if let Ok(best) = std::fs::read_to_string(config.out.join("best.json")) {
            best_margin = json_number(&best, "pairedDeltaD3").unwrap_or(f64::NEG_INFINITY);
        }
        if checkpoint_path.exists() {
            eprintln!("resuming from {} at {moves_total} moves", checkpoint_path.display());
            Model::load(&checkpoint_path, true)?
        } else {
            eprintln!("no checkpoint; starting fresh tables");
            Model::new(layout, config.optimistic, true)
        }
    } else {
        if progress_path.exists() {
            return Err("progress.jsonl exists; pass --resume to continue this run".into());
        }
        Model::new(layout, config.optimistic, true)
    };
    let model = Arc::new(model);
    let params = PolicyParams {
        reveal_samples: config.reveal_samples,
        policy_seed: 0xd707_5eed,
    };
    let shared = Shared {
        moves: AtomicU64::new(moves_total),
        games: AtomicU64::new(games_total),
        cursor: AtomicU64::new(cursor_start),
    };

    // The validation block's fair-d3s7 control is a deterministic function of
    // the seeds; compute it once per process.
    let validate_seeds: Vec<u32> = (0..config.validate_games as u32)
        .map(|g| config.validate_start.wrapping_add(g))
        .collect();
    let fair_records = if config.validate_every > 0 || config.quick_every > 0 {
        eprintln!("fair-d3s7 control on the validation block ({} games)...", validate_seeds.len());
        let records = evaluate_arm(&Arm::FairD3, &validate_seeds, config.threads, config.move_cap);
        eprintln!("fair-d3s7 mean {:.0} / {:.2} moves", mean_score(&records), mean_moves(&records));
        records
    } else {
        Vec::new()
    };

    let mut next_validate = if config.validate_every > 0 {
        (moves_total / config.validate_every + 1) * config.validate_every
    } else {
        u64::MAX
    };
    let mut next_quick = if config.quick_every > 0 {
        (moves_total / config.quick_every + 1) * config.quick_every
    } else {
        u64::MAX
    };

    let stop_path = config.out.join("STOP");
    #[allow(unused_assignments)]
    let mut stop_reason: Option<String> = None;
    let mut plateau_state: Option<(f64, f64, bool)> = None;
    loop {
        let elapsed = wall_offset + started.elapsed().as_secs_f64();
        if moves_total >= config.moves {
            eprintln!("move budget spent at {moves_total} moves");
            stop_reason = Some("moves".into());
            break;
        }
        if config.wall_seconds > 0 && elapsed >= config.wall_seconds as f64 {
            eprintln!("wall budget spent at {moves_total} moves");
            stop_reason = Some("wall".into());
            break;
        }
        if stop_path.exists() {
            eprintln!("STOP file present at {moves_total} moves");
            stop_reason = Some("stop-file".into());
            break;
        }
        let chunk_end = (moves_total + config.chunk_moves).min(config.moves);
        let chunk_started = Instant::now();
        let stats: Mutex<Vec<ChunkStats>> = Mutex::new(Vec::new());
        std::thread::scope(|scope| {
            for worker_index in 0..config.threads.max(1) {
                let model = &model;
                let config = &config;
                let params = &params;
                let shared = &shared;
                let stats = &stats;
                scope.spawn(move || {
                    let result = worker(model, config, params, shared, chunk_end, worker_index);
                    stats.lock().unwrap().push(result);
                });
            }
        });
        let chunk_seconds = chunk_started.elapsed().as_secs_f64();
        let mut total = ChunkStats::default();
        for s in stats.lock().unwrap().iter() {
            total.games += s.games;
            total.moves += s.moves;
            total.score_sum += s.score_sum;
            total.max_score = total.max_score.max(s.max_score);
            total.clears += s.clears;
            total.reveals += s.reveals;
            total.updates.updates += s.updates.updates;
            total.updates.entry_updates += s.updates.entry_updates;
            total.updates.abs_delta_sum += s.updates.abs_delta_sum;
            total.updates.beta_sum += s.updates.beta_sum;
        }
        moves_total = shared.moves.load(Relaxed);
        games_total = shared.games.load(Relaxed);
        let seed_cursor = shared.cursor.load(Relaxed);
        let wall_now = wall_offset + started.elapsed().as_secs_f64();

        // Quick validation: direct play only.
        let mut quick_json = "null".to_string();
        if moves_total >= next_quick {
            let direct = evaluate_arm(&Arm::Direct(model.clone()), &validate_seeds, config.threads, config.move_cap);
            let (delta, wins, losses) = paired(&direct, &fair_records);
            quick_json = format!(
                "{{\"directMean\":{},\"directMeanMoves\":{},\"pairedDeltaDirectVsFairD3\":{},\"wins\":{},\"losses\":{}}}",
                mean_score(&direct),
                mean_moves(&direct),
                delta,
                wins,
                losses
            );
            eprintln!(
                "[{:>7.0}s] {:>12} moves  quick: 1-ply {:.0} ({:.1} moves) vs fair-d3s7 {:.0}: {:+.0}",
                wall_now,
                moves_total,
                mean_score(&direct),
                mean_moves(&direct),
                mean_score(&fair_records),
                delta
            );
            next_quick += config.quick_every;
        }

        // Full validation: the tables inside the deployment search.
        let mut validation_json = "null".to_string();
        let mut plateau_stop = false;
        if moves_total >= next_validate {
            let validation = validate(&config, &model, &validate_seeds, &fair_records, moves_total, &mut best_margin)?;
            margins.push(validation.paired_delta_d3);
            plateau_state = plateau_check(&margins, config.plateau_window, config.plateau_min_points);
            let plateau_json = match plateau_state {
                Some((recent, previous, stop)) => format!(
                    "{{\"points\":{},\"window\":{},\"recentMean\":{},\"previousMean\":{},\"stop\":{}}}",
                    margins.len(),
                    config.plateau_window,
                    recent,
                    previous,
                    stop
                ),
                None => "null".to_string(),
            };
            plateau_stop = matches!(plateau_state, Some((_, _, true)));
            validation_json = format!(
                "{{\"moves\":{},\"artifact\":\"{}\",\"ntupleD3Mean\":{},\"directMean\":{},\"fairD3Mean\":{},\"pairedDeltaD3\":{},\"winsD3\":{},\"lossesD3\":{},\"pairedDeltaDirect\":{},\"touchedEntries\":{},\"isBest\":{},\"point\":{},\"plateau\":{}}}",
                validation.moves,
                validation.artifact,
                validation.ntuple_d3_mean,
                validation.direct_mean,
                validation.fair_d3_mean,
                validation.paired_delta_d3,
                validation.wins_d3,
                validation.losses_d3,
                validation.paired_delta_direct,
                validation.touched,
                validation.is_best,
                margins.len(),
                plateau_json
            );
            eprintln!(
                "[{:>7.0}s] {:>12} moves  VALIDATION: ntuple-d3s7 {:.0} vs fair-d3s7 {:.0}: {:+.0} (W-L {}-{}), 1-ply {:.0}, touched {} entries{}",
                wall_now,
                moves_total,
                validation.ntuple_d3_mean,
                validation.fair_d3_mean,
                validation.paired_delta_d3,
                validation.wins_d3,
                validation.losses_d3,
                validation.direct_mean,
                validation.touched,
                if validation.is_best { "  [best]" } else { "" }
            );
            if let Some((recent, previous, stop)) = plateau_state {
                eprintln!(
                    "plateau rule at point {}: last {} points mean {recent:+.0}, previous {} points mean {previous:+.0}{}",
                    margins.len(),
                    config.plateau_window,
                    config.plateau_window,
                    if stop { "  -> PLATEAU, stopping after this chunk" } else { "" }
                );
            }
            if config.checkpoint && (margins.len() % config.checkpoint_every == 0 || plateau_stop) {
                eprintln!("checkpoint...");
                model.save(&checkpoint_path, true)?;
            }
            next_validate += config.validate_every;
        }

        let row = format!(
            "{{\"chunk\":{},\"movesTotal\":{},\"gamesTotal\":{},\"wallSeconds\":{:.1},\"chunkMoves\":{},\"chunkGames\":{},\"chunkSeconds\":{:.1},\"movesPerSecond\":{:.0},\"trainMeanScore\":{:.1},\"trainMeanMoves\":{:.3},\"trainMaxScore\":{},\"trainClearsPerMove\":{:.4},\"trainRevealsPerMove\":{:.4},\"meanAbsDelta\":{:.5},\"meanBeta\":{:.5},\"seedCursor\":{},\"seedWraps\":{},\"quick\":{},\"validation\":{}}}\n",
            chunk_index,
            moves_total,
            games_total,
            wall_now,
            total.moves,
            total.games,
            chunk_seconds,
            total.moves as f64 / chunk_seconds.max(1e-9),
            total.score_sum / total.games.max(1) as f64,
            total.moves as f64 / total.games.max(1) as f64,
            total.max_score,
            total.clears as f64 / total.moves.max(1) as f64,
            total.reveals as f64 / total.moves.max(1) as f64,
            total.updates.abs_delta_sum / total.updates.updates.max(1) as f64,
            total.updates.beta_sum / total.updates.entry_updates.max(1) as f64,
            seed_cursor,
            seed_cursor / config.seeds_count as u64,
            quick_json,
            validation_json,
        );
        {
            use std::io::Write;
            let mut file = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(&progress_path)
                .map_err(|e| e.to_string())?;
            file.write_all(row.as_bytes()).map_err(|e| e.to_string())?;
        }
        if chunk_index % 4 == 0 || moves_total >= next_quick {
            eprintln!(
                "[{:>7.0}s] chunk {:>4}  {:>12} moves  {:>9} games  {:>8.0} moves/s  train mean {:>8.0} / {:>6.1} moves  |delta| {:.3}  beta {:.3}",
                wall_now,
                chunk_index,
                moves_total,
                games_total,
                total.moves as f64 / chunk_seconds.max(1e-9),
                total.score_sum / total.games.max(1) as f64,
                total.moves as f64 / total.games.max(1) as f64,
                total.updates.abs_delta_sum / total.updates.updates.max(1) as f64,
                total.updates.beta_sum / total.updates.entry_updates.max(1) as f64,
            );
        }
        chunk_index += 1;
        if plateau_stop {
            stop_reason = Some("plateau".into());
            break;
        }
    }

    eprintln!("final checkpoint and latest weights...");
    if config.checkpoint {
        model.save(&checkpoint_path, true)?;
    }
    model.save(&config.out.join("latest-weights.bin"), false)?;
    let wall_now = wall_offset + started.elapsed().as_secs_f64();
    let (recent, previous) = match plateau_state {
        Some((r, p, _)) => (r.to_string(), p.to_string()),
        None => ("null".into(), "null".into()),
    };
    atomic_write(
        &config.out.join("stop.json"),
        format!(
            "{{\"reason\":\"{}\",\"movesTotal\":{moves_total},\"gamesTotal\":{games_total},\"wallSeconds\":{wall_now:.1},\"validationPoints\":{},\"plateauWindow\":{},\"recentWindowMean\":{recent},\"previousWindowMean\":{previous},\"bestMargin\":{}}}\n",
            stop_reason.as_deref().unwrap_or("unknown"),
            margins.len(),
            config.plateau_window,
            if best_margin.is_finite() { best_margin.to_string() } else { "null".into() }
        )
        .as_bytes(),
    )?;
    atomic_write(
        &config.out.join("DONE"),
        format!("movesTotal {moves_total} gamesTotal {games_total} reason {}\n", stop_reason.as_deref().unwrap_or("unknown")).as_bytes(),
    )?;
    Ok(())
}

fn validate(
    config: &Config,
    model: &Arc<Model>,
    seeds: &[u32],
    fair_records: &[GameRecord],
    moves_total: u64,
    best_margin: &mut f64,
) -> Result<Validation, String> {
    let ntuple = evaluate_arm(&Arm::NTupleD3(model.clone()), seeds, config.threads, config.move_cap);
    let direct = evaluate_arm(&Arm::Direct(model.clone()), seeds, config.threads, config.move_cap);
    let (delta_d3, wins, losses) = paired(&ntuple, fair_records);
    let (delta_direct, _, _) = paired(&direct, fair_records);
    let touched = model.touched_entries();
    let artifact = format!("val-{moves_total:012}.json");
    let config_json = format!(
        "{{\"experiment\":\"{}\",\"validation\":true,\"movesTrained\":{},\"games\":{},\"moveCap\":{},\"arms\":[\"ntuple-d3s7\",\"ntuple-1ply\",\"fair-d3s7\"]}}",
        config.experiment_id,
        moves_total,
        seeds.len(),
        config.move_cap
    );
    let individuals = vec![
        Individual { name: "ntuple-d3s7".into(), games: ntuple.clone() },
        Individual { name: "ntuple-1ply".into(), games: direct.clone() },
        Individual { name: "fair-d3s7".into(), games: fair_records.to_vec() },
    ];
    let text = population_artifact_json(&config_json, seeds[0], &individuals);
    atomic_write(&config.out.join(&artifact), text.as_bytes())?;
    let is_best = delta_d3 > *best_margin;
    if is_best {
        *best_margin = delta_d3;
        model.save(&config.out.join("best-weights.bin"), false)?;
        atomic_write(
            &config.out.join("best.json"),
            format!(
                "{{\"moves\":{},\"artifact\":\"{}\",\"pairedDeltaD3\":{},\"ntupleD3Mean\":{},\"fairD3Mean\":{}}}\n",
                moves_total,
                artifact,
                delta_d3,
                mean_score(&ntuple),
                mean_score(fair_records)
            )
            .as_bytes(),
        )?;
    }
    Ok(Validation {
        moves: moves_total,
        artifact,
        ntuple_d3_mean: mean_score(&ntuple),
        direct_mean: mean_score(&direct),
        fair_d3_mean: mean_score(fair_records),
        paired_delta_d3: delta_d3,
        wins_d3: wins,
        losses_d3: losses,
        paired_delta_direct: delta_direct,
        touched,
        is_best,
    })
}
