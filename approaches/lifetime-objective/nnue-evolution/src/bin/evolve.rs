// Stage C: evolutionary refinement.  A mutation-only genetic algorithm over
// the NNUE weights whose fitness is the deployed quantity itself: the mean
// score of complete depth-3 seven-stratum games, every candidate in a
// generation playing the SAME fresh block of training seeds (common random
// numbers), with the frozen fair leaf and the unevolved init playing every
// block as paired controls.
//
// This design is the direct answer to the diagnosed failure of the
// 18-weight CMA-ES leaf evolution (RS-20260822T120736Z-662b39ca: "fresh
// seeds per generation removed overfitting, as designed — and left selection
// noise alone to steer").  Here selection noise is controlled three ways:
// paired blocks (the only variance that matters for ranking candidates
// within a generation is the paired differences), a population an order of
// magnitude larger than the CMA-ES lambda with tournament selection (an
// ordinal, scale-free rule), and a final elite re-selection on a fresh
// 128-game block before the candidate is frozen.
//
// Continuation runs (added after the first 60-generation run,
// RS-20260903T025751Z-6577b33e) may resume from a checkpointed population of
// an earlier run (--resume-population), carry a third paired control
// (--baseline, e.g. the earlier run's frozen candidate), anneal the mutation
// size (--sigma-decay-tau, --sigma-floor; see schedule.rs) and stop on a
// preregistered plateau rule (--plateau-window, --plateau-check-every,
// --plateau-min-generations): every check is appended to plateau.jsonl and a
// stop writes the PLATEAU marker.  With none of those flags the binary
// behaves exactly as it did for the first run.
//
// Per generation the binary writes:
//   gen-NNN.json         population artifact (compare.py-compatible),
//                        controls included as named individuals
//   population-NNN.bin   the population that plays generation NNN (and the
//                        successor NNN+1 is saved before gen-NNN.json marks
//                        the generation complete — see the checkpoint
//                        contract in run_evolution)
//   progress.jsonl       one summary line per generation
// and on --select:
//   candidate-weights.bin, selection.json
//
// Usage:
//   evolve --init FILE --lease-start 0x... --out DIR [--population 32]
//          [--games 32] [--generations 60] [--elites 4] [--tournament 3]
//          [--sigma-rel 0.05] [--seed 0x...] [--threads 16]
//          [--wall-seconds N] [--move-cap 2000]
//          [--resume-population FILE] [--baseline FILE]
//          [--sigma-decay-tau G] [--sigma-floor S]
//          [--plateau-window W] [--plateau-check-every K] [--plateau-min-generations M]
//          [--experiment-id EX-...]
//   evolve --select --lease-start 0x... --out DIR [--select-games 128]

use drop7_nnue_evolution::game::{evaluate_tasks, population_artifact_json, EvalLeaf, EvalTask, Individual, MOVE_CAP};
use drop7_nnue_evolution::json::Json;
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::schedule::{plateau_check, sigma_for};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::rng::{mix32, Mulberry32};
use drop7_rs::search::FairLeaf;
use std::io::Write;

const DEFAULT_EXPERIMENT_ID: &str = "EX-20260902-nnue-evolution-d3-v2-49c18bc2";

const CONTROL_FAIR: &str = "control-fair-d3s7";
const CONTROL_INIT: &str = "control-init-d3s7";
const CONTROL_BASELINE: &str = "control-baseline-d3s7";

struct Config {
    init: String,
    lease_start: u32,
    out: String,
    population: usize,
    games: usize,
    generations: usize,
    elites: usize,
    tournament: usize,
    sigma_rel: f32,
    seed: u32,
    threads: usize,
    wall_seconds: u64,
    move_cap: i32,
    select: bool,
    select_games: usize,
    // continuation options
    resume_population: Option<String>,
    baseline: Option<String>,
    sigma_decay_tau: f32,
    sigma_floor: f32,
    plateau_window: usize,
    plateau_check_every: usize,
    plateau_min_generations: usize,
    experiment_id: String,
}

fn parse_args() -> Result<Config, String> {
    let mut config = Config {
        init: String::new(),
        lease_start: 0,
        out: String::new(),
        population: 32,
        games: 32,
        generations: 60,
        elites: 4,
        tournament: 3,
        sigma_rel: 0.05,
        seed: 0x0e70_1e58,
        threads: 16,
        wall_seconds: 43_200,
        move_cap: MOVE_CAP,
        select: false,
        select_games: 128,
        resume_population: None,
        baseline: None,
        sigma_decay_tau: 0.0,
        sigma_floor: 0.0,
        plateau_window: 0,
        plateau_check_every: 50,
        plateau_min_generations: 100,
        experiment_id: DEFAULT_EXPERIMENT_ID.to_string(),
    };
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let key = args[i].as_str();
        if key == "--select" {
            config.select = true;
            i += 1;
            continue;
        }
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match key {
            "--init" => config.init = value.to_string(),
            "--lease-start" => {
                config.lease_start = u32::from_str_radix(value.trim_start_matches("0x"), 16)
                    .map_err(|_| "bad --lease-start")?
            }
            "--out" => config.out = value.to_string(),
            "--population" => config.population = value.parse().map_err(|_| "bad --population")?,
            "--games" => config.games = value.parse().map_err(|_| "bad --games")?,
            "--generations" => {
                config.generations = value.parse().map_err(|_| "bad --generations")?
            }
            "--elites" => config.elites = value.parse().map_err(|_| "bad --elites")?,
            "--tournament" => {
                config.tournament = value.parse().map_err(|_| "bad --tournament")?
            }
            "--sigma-rel" => {
                config.sigma_rel = value.parse().map_err(|_| "bad --sigma-rel")?
            }
            "--seed" => {
                config.seed = u32::from_str_radix(value.trim_start_matches("0x"), 16)
                    .map_err(|_| "bad --seed")?
            }
            "--threads" => config.threads = value.parse().map_err(|_| "bad --threads")?,
            "--wall-seconds" => {
                config.wall_seconds = value.parse().map_err(|_| "bad --wall-seconds")?
            }
            "--move-cap" => config.move_cap = value.parse().map_err(|_| "bad --move-cap")?,
            "--select-games" => {
                config.select_games = value.parse().map_err(|_| "bad --select-games")?
            }
            "--resume-population" => config.resume_population = Some(value.to_string()),
            "--baseline" => config.baseline = Some(value.to_string()),
            "--sigma-decay-tau" => {
                config.sigma_decay_tau = value.parse().map_err(|_| "bad --sigma-decay-tau")?
            }
            "--sigma-floor" => {
                config.sigma_floor = value.parse().map_err(|_| "bad --sigma-floor")?
            }
            "--plateau-window" => {
                config.plateau_window = value.parse().map_err(|_| "bad --plateau-window")?
            }
            "--plateau-check-every" => {
                config.plateau_check_every =
                    value.parse().map_err(|_| "bad --plateau-check-every")?
            }
            "--plateau-min-generations" => {
                config.plateau_min_generations =
                    value.parse().map_err(|_| "bad --plateau-min-generations")?
            }
            "--experiment-id" => config.experiment_id = value.to_string(),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    if config.out.is_empty() {
        return Err("--out required".into());
    }
    if config.lease_start == 0 {
        return Err("--lease-start required".into());
    }
    if !config.select && config.init.is_empty() {
        return Err("--init required".into());
    }
    if config.sigma_floor > config.sigma_rel {
        return Err("--sigma-floor must not exceed --sigma-rel".into());
    }
    Ok(config)
}

/// Per-tensor Gaussian mutation with a relative sigma: sigma_tensor =
/// sigma_rel * std(parent tensor), floored so zero tensors still move.
/// Deterministic given (evolution seed, generation, child index).
fn mutate(parent: &Nnue, seed: u32, generation: usize, child: usize, sigma_rel: f32) -> Nnue {
    let mut rng = Mulberry32::new(mix32(
        seed ^ (generation as u32).wrapping_mul(0x9e37_79b9) ^ (child as u32).wrapping_add(1),
    ));
    let next = |rng: &mut Mulberry32| -> f32 {
        let u1 = (rng.next_bits() as f64 + 1.0) / 4_294_967_297.0;
        let u2 = (rng.next_bits() as f64 + 1.0) / 4_294_967_297.0;
        ((-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos()) as f32
    };
    let mut child_net = parent.clone();
    for (_, tensor) in child_net.tensors_mut() {
        let n = tensor.len() as f64;
        let mean = tensor.iter().map(|&v| v as f64).sum::<f64>() / n;
        let var = tensor
            .iter()
            .map(|&v| {
                let d = v as f64 - mean;
                d * d
            })
            .sum::<f64>()
            / n;
        let sigma = (sigma_rel as f64 * var.sqrt()).max(1e-4) as f32;
        for value in tensor.iter_mut() {
            *value += sigma * next(&mut rng);
        }
    }
    let b2_sigma = (sigma_rel * parent.b2.abs()).max(1e-4);
    child_net.b2 += b2_sigma * next(&mut rng);
    child_net
}

fn flat_population(population: &[Nnue]) -> Vec<f32> {
    let mut flat = Vec::with_capacity(population.len() * Nnue::parameter_count());
    for net in population {
        flat.extend_from_slice(&net.to_flat());
    }
    flat
}

fn unflat_population(flat: &[f32], population: usize) -> Vec<Nnue> {
    let stride = Nnue::parameter_count();
    (0..population)
        .map(|i| Nnue::from_flat(&flat[i * stride..(i + 1) * stride]))
        .collect()
}

fn save_population(path: &str, population: &[Nnue]) -> Result<(), String> {
    let flat = flat_population(population);
    let mut bytes = Vec::with_capacity(16 + flat.len() * 4);
    bytes.extend_from_slice(b"D7EVPOP1");
    bytes.extend_from_slice(&(population.len() as u32).to_le_bytes());
    for value in &flat {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    std::fs::write(path, bytes).map_err(|e| e.to_string())
}

fn load_population(path: &str) -> Result<Vec<Nnue>, String> {
    let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
    if bytes.len() < 12 || &bytes[..8] != b"D7EVPOP1" {
        return Err("bad population magic".into());
    }
    let count = u32::from_le_bytes(bytes[8..12].try_into().unwrap()) as usize;
    let stride = Nnue::parameter_count();
    if bytes.len() != 12 + count * stride * 4 {
        return Err("population length mismatch".into());
    }
    let mut flat = Vec::with_capacity(count * stride);
    for chunk in bytes[12..].chunks_exact(4) {
        flat.push(f32::from_le_bytes(chunk.try_into().unwrap()));
    }
    Ok(unflat_population(&flat, count))
}

/// Hex SHA-256 of a file, for recording which population a run resumed from.
/// Std-only: a compact SHA-256 so the crate keeps its no-dependency build.
fn sha256_hex(bytes: &[u8]) -> String {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];
    let mut data = bytes.to_vec();
    let bit_len = (bytes.len() as u64).wrapping_mul(8);
    data.push(0x80);
    while data.len() % 64 != 56 {
        data.push(0);
    }
    data.extend_from_slice(&bit_len.to_be_bytes());
    for chunk in data.chunks_exact(64) {
        let mut w = [0u32; 64];
        for (i, word) in chunk.chunks_exact(4).enumerate() {
            w[i] = u32::from_be_bytes(word.try_into().unwrap());
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut a = h;
        for i in 0..64 {
            let s1 = a[4].rotate_right(6) ^ a[4].rotate_right(11) ^ a[4].rotate_right(25);
            let ch = (a[4] & a[5]) ^ (!a[4] & a[6]);
            let t1 = a[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(K[i]).wrapping_add(w[i]);
            let s0 = a[0].rotate_right(2) ^ a[0].rotate_right(13) ^ a[0].rotate_right(22);
            let maj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
            let t2 = s0.wrapping_add(maj);
            a = [t1.wrapping_add(t2), a[0], a[1], a[2], a[3].wrapping_add(t1), a[4], a[5], a[6]];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(a[i]);
        }
    }
    h.iter().map(|v| format!("{v:08x}")).collect()
}

/// Evaluate one generation: every candidate plus the controls on the same
/// seed block.  Returns the per-individual mean scores and the population
/// artifact as a string; the caller decides when to write it (the checkpoint
/// contract orders the writes, see run_evolution).  Control order after the
/// population: fair, init, then the optional baseline.
fn evaluate_generation(
    config: &Config,
    population: &[Nnue],
    init: &Nnue,
    baseline: Option<&Nnue>,
    generation: usize,
    block_start: u32,
    sigma_rel: f32,
) -> Result<(Vec<f64>, String), String> {
    let params = deployment_params();
    let controls = 2 + usize::from(baseline.is_some());
    let individuals = population.len() + controls;
    let mut tasks = Vec::with_capacity(individuals * config.games);
    for individual in 0..individuals {
        for game in 0..config.games {
            tasks.push(EvalTask {
                individual,
                seed: block_start + game as u32,
            });
        }
    }
    let leaf_for = |index: usize| -> EvalLeaf {
        if index < population.len() {
            EvalLeaf::Nnue(NnueLeaf {
                net: population[index].clone(),
            })
        } else if index == population.len() {
            EvalLeaf::Fair(FairLeaf::default())
        } else if index == population.len() + 1 {
            EvalLeaf::Nnue(NnueLeaf { net: init.clone() })
        } else {
            EvalLeaf::Nnue(NnueLeaf {
                net: baseline.expect("baseline control present").clone(),
            })
        }
    };
    let records = evaluate_tasks(
        &leaf_for,
        &params,
        DEPLOYMENT_TABLE,
        &tasks,
        config.threads,
        config.move_cap,
    );

    // Assemble individuals in task order.
    let mut assembled: Vec<Individual> = Vec::with_capacity(individuals);
    for index in 0..individuals {
        let name = if index < population.len() {
            format!("candidate-{index:02}")
        } else if index == population.len() {
            CONTROL_FAIR.to_string()
        } else if index == population.len() + 1 {
            CONTROL_INIT.to_string()
        } else {
            CONTROL_BASELINE.to_string()
        };
        let start = index * config.games;
        assembled.push(Individual {
            name,
            games: records[start..start + config.games].to_vec(),
        });
    }
    let config_json = format!(
        "{{\"experiment\":\"{}\",\"generation\":{generation},\"depth\":3,\"strata\":7,\"games\":{},\"moveCap\":{},\"sigmaRel\":{sigma_rel}}}",
        config.experiment_id, config.games, config.move_cap
    );
    let artifact = population_artifact_json(&config_json, block_start, &assembled);
    let means: Vec<f64> = assembled
        .iter()
        .map(|individual| {
            individual.games.iter().map(|g| g.score as f64).sum::<f64>()
                / individual.games.len().max(1) as f64
        })
        .collect();
    Ok((means, artifact))
}

/// The tracked plateau statistic of one completed generation, rebuilt from
/// progress.jsonl on resume so the rule sees the whole run.
fn load_margin_series(out: &str) -> Result<Vec<f64>, String> {
    let path = format!("{out}/progress.jsonl");
    if !std::path::Path::new(&path).exists() {
        return Ok(Vec::new());
    }
    let text = std::fs::read_to_string(&path).map_err(|e| e.to_string())?;
    let mut series = Vec::new();
    for line in text.lines().filter(|l| !l.trim().is_empty()) {
        let row = drop7_nnue_evolution::json::parse(line)?;
        let mean = row.get("mean").and_then(Json::as_f64).ok_or("progress row missing mean")?;
        let fair = row
            .get("controlFair")
            .and_then(Json::as_f64)
            .ok_or("progress row missing controlFair")?;
        series.push(mean - fair);
    }
    Ok(series)
}

fn run_evolution(config: &Config) -> Result<(), String> {
    let init = Nnue::load(std::path::Path::new(&config.init))?;
    let baseline = match &config.baseline {
        Some(path) => Some(Nnue::load(std::path::Path::new(path))?),
        None => None,
    };
    std::fs::create_dir_all(&config.out).map_err(|e| e.to_string())?;
    let resume_sha = match &config.resume_population {
        Some(path) => {
            let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
            Some(sha256_hex(&bytes))
        }
        None => None,
    };
    let baseline_sha = match &config.baseline {
        Some(path) => Some(sha256_hex(&std::fs::read(path).map_err(|e| e.to_string())?)),
        None => None,
    };
    std::fs::write(
        format!("{}/config.json", config.out),
        format!(
            "{{\"experiment\":\"{}\",\"population\":{},\"games\":{},\"generations\":{},\"elites\":{},\"tournament\":{},\"sigmaRel\":{},\"sigmaDecayTau\":{},\"sigmaFloor\":{},\"seed\":\"0x{:08x}\",\"leaseStart\":\"0x{:08x}\",\"moveCap\":{},\"wallSeconds\":{},\"resumePopulation\":{},\"resumePopulationSha256\":{},\"baseline\":{},\"baselineSha256\":{},\"plateauWindow\":{},\"plateauCheckEvery\":{},\"plateauMinGenerations\":{}}}\n",
            config.experiment_id,
            config.population, config.games, config.generations, config.elites,
            config.tournament, config.sigma_rel, config.sigma_decay_tau, config.sigma_floor,
            config.seed, config.lease_start, config.move_cap, config.wall_seconds,
            json_string_or_null(config.resume_population.as_deref()),
            json_string_or_null(resume_sha.as_deref()),
            json_string_or_null(config.baseline.as_deref()),
            json_string_or_null(baseline_sha.as_deref()),
            config.plateau_window, config.plateau_check_every, config.plateau_min_generations
        ),
    )
    .map_err(|e| e.to_string())?;

    if std::path::Path::new(&format!("{}/PLATEAU", config.out)).exists() {
        println!("PLATEAU marker present; the run already stopped on the plateau rule");
        return Ok(());
    }

    // Checkpoint contract: population-{g}.bin holds the population that PLAYS
    // generation g.  Within an iteration the writes are ordered so a kill or
    // stop can never strand a completed generation without its successor:
    //   1. evaluate the population (means in memory only),
    //   2. select and save population-{g+1}.bin,
    //   3. write gen-{g}.json (the completion marker) and progress.jsonl.
    // A kill before step 3 replays generation g (training seeds are
    // deterministic: the replay recomputes identical fitness and an identical
    // successor).  A stop between iterations leaves population-{g+1}.bin
    // already saved, so resume always finds it.
    let mut start_generation = 0usize;
    let mut population: Vec<Nnue> = {
        let mut last_complete: Option<usize> = None;
        for generation in (0..config.generations).rev() {
            if std::path::Path::new(&format!("{}/gen-{generation:03}.json", config.out)).exists() {
                last_complete = Some(generation);
                break;
            }
        }
        match last_complete {
            Some(generation) if generation + 1 < config.generations => {
                let path = format!("{}/population-{:03}.bin", config.out, generation + 1);
                if !std::path::Path::new(&path).exists() {
                    return Err(format!(
                        "generation {generation} completed but {path} is missing; cannot resume"
                    ));
                }
                println!("resuming from {path}");
                start_generation = generation + 1;
                load_population(&path)?
            }
            Some(generation) => {
                println!("all {} generations already complete", generation + 1);
                start_generation = config.generations;
                Vec::new()
            }
            None => {
                let initial = match &config.resume_population {
                    // Continuation: generation 0 plays the earlier run's
                    // checkpointed population exactly as saved.
                    Some(path) => {
                        let loaded = load_population(path)?;
                        if loaded.len() != config.population {
                            return Err(format!(
                                "{path} holds {} vectors but --population is {}",
                                loaded.len(),
                                config.population
                            ));
                        }
                        println!("generation 0 is the resumed population {path}");
                        loaded
                    }
                    // Fresh start: candidate 0 is the exact init; the rest are
                    // a 2-sigma cloud around it.
                    None => {
                        let mut initial = Vec::with_capacity(config.population);
                        initial.push(init.clone());
                        for child in 1..config.population {
                            initial.push(mutate(&init, config.seed, usize::MAX, child, 2.0 * config.sigma_rel));
                        }
                        initial
                    }
                };
                // Checkpoint it before any game so the contract holds from
                // the first iteration.
                save_population(&format!("{}/population-000.bin", config.out), &initial)?;
                initial
            }
        }
    };
    let mut margin_series = load_margin_series(&config.out)?;
    if margin_series.len() != start_generation {
        return Err(format!(
            "progress.jsonl holds {} rows but {start_generation} generations are complete",
            margin_series.len()
        ));
    }

    let started = std::time::Instant::now();
    let mut last_fitness: Vec<f64> = Vec::new();
    for generation in start_generation..config.generations {
        if started.elapsed().as_secs() > config.wall_seconds {
            println!("wall budget spent; stopping at generation {generation}");
            break;
        }
        if std::path::Path::new(&format!("{}/STOP", config.out)).exists() {
            println!("STOP file found; stopping at generation {generation}");
            break;
        }
        let sigma_rel = sigma_for(generation, config.sigma_rel, config.sigma_decay_tau, config.sigma_floor);
        let block_start = config.lease_start + (generation * config.games) as u32;
        let (means, artifact) = evaluate_generation(
            config,
            &population,
            &init,
            baseline.as_ref(),
            generation,
            block_start,
            sigma_rel,
        )?;
        last_fitness = means.clone();

        // Selection: elites cloned, the rest filled by tournament-selected,
        // mutated children.  Tournaments are ordinal, so heavy-tailed
        // outliers move ranks, not magnitudes.
        let mut ranking: Vec<usize> = (0..config.population).collect();
        ranking.sort_by(|&a, &b| means[b].partial_cmp(&means[a]).unwrap());
        let mut next: Vec<Nnue> = Vec::with_capacity(config.population);
        for &elite in ranking.iter().take(config.elites) {
            next.push(population[elite].clone());
        }
        let mut rng = Mulberry32::new(mix32(config.seed ^ (generation as u32).wrapping_mul(0x85eb_ca6b)));
        while next.len() < config.population {
            let mut winner = (rng.next_bits() as usize) % config.population;
            for _ in 1..config.tournament {
                let challenger = (rng.next_bits() as usize) % config.population;
                if means[challenger] > means[winner] {
                    winner = challenger;
                }
            }
            next.push(mutate(
                &population[winner],
                config.seed,
                generation,
                next.len(),
                sigma_rel,
            ));
        }
        // Checkpoint contract step 2: the successor population is durable
        // BEFORE this generation is marked complete.
        save_population(
            &format!("{}/population-{:03}.bin", config.out, generation + 1),
            &next,
        )?;
        // Step 3: only now write the completion marker and the progress line.
        std::fs::write(format!("{}/gen-{generation:03}.json", config.out), artifact)
            .map_err(|e| e.to_string())?;
        let best = means[..config.population]
            .iter()
            .cloned()
            .fold(f64::NEG_INFINITY, f64::max);
        let mean_all: f64 =
            means[..config.population].iter().sum::<f64>() / config.population as f64;
        let control_fair = means[config.population];
        let control_init = means[config.population + 1];
        let control_baseline = baseline.as_ref().map(|_| means[config.population + 2]);
        println!(
            "generation {generation}: best {best:.0} mean {mean_all:.0} | fair {control_fair:.0} init {control_init:.0}{} sigma {sigma_rel:.4} ({:.0}s)",
            control_baseline
                .map(|b| format!(" baseline {b:.0}"))
                .unwrap_or_default(),
            started.elapsed().as_secs_f64()
        );
        let progress = format!(
            "{{\"generation\":{generation},\"blockStart\":\"0x{block_start:08x}\",\"best\":{best},\"mean\":{mean_all},\"controlFair\":{control_fair},\"controlInit\":{control_init},\"controlBaseline\":{},\"sigmaRel\":{sigma_rel},\"fitness\":[{}]}}\n",
            control_baseline
                .map(|b| format!("{b}"))
                .unwrap_or_else(|| "null".to_string()),
            means[..config.population]
                .iter()
                .map(|v| format!("{v}"))
                .collect::<Vec<_>>()
                .join(",")
        );
        let mut file = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(format!("{}/progress.jsonl", config.out))
            .map_err(|e| e.to_string())?;
        file.write_all(progress.as_bytes()).map_err(|e| e.to_string())?;
        population = next;

        // Preregistered plateau rule on the paired margin over the fair
        // control.  Every check is recorded whether or not it stops the run.
        margin_series.push(mean_all - control_fair);
        if let Some(check) = plateau_check(
            &margin_series,
            config.plateau_window,
            config.plateau_check_every,
            config.plateau_min_generations,
        ) {
            let line = format!(
                "{{\"generation\":{},\"window\":{},\"slopePerGeneration\":{},\"standardError\":{},\"lowerBound95\":{},\"windowMeanFirstHalf\":{},\"windowMeanSecondHalf\":{},\"stop\":{}}}\n",
                check.generation,
                check.window,
                check.slope_per_generation,
                check.standard_error,
                check.lower_bound_95,
                check.window_mean_first_half,
                check.window_mean_second_half,
                check.stop
            );
            let mut file = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(format!("{}/plateau.jsonl", config.out))
                .map_err(|e| e.to_string())?;
            file.write_all(line.as_bytes()).map_err(|e| e.to_string())?;
            println!(
                "plateau check after generation {}: slope {:.1}/gen, se {:.1}, lower bound {:.1} -> {}",
                check.generation,
                check.slope_per_generation,
                check.standard_error,
                check.lower_bound_95,
                if check.stop { "STOP (no detectable improvement)" } else { "continue" }
            );
            if check.stop {
                std::fs::write(format!("{}/PLATEAU", config.out), line).map_err(|e| e.to_string())?;
                break;
            }
        }
    }

    // Persist the last evaluated fitness for human inspection.  This file is
    // informational only — --select re-derives fitness from the completed
    // generation's gen-{g}.json, so a kill that strands this file at an
    // earlier generation cannot mis-rank the pool.  Written only when this
    // process actually evaluated a generation; a no-op resume must not
    // clobber the completing run's file.
    if !last_fitness.is_empty() {
        std::fs::write(
            format!("{}/final-fitness.json", config.out),
            format!(
                "{{\"fitness\":[{}]}}\n",
                last_fitness
                    .iter()
                    .map(|v| format!("{v}"))
                    .collect::<Vec<_>>()
                    .join(",")
            ),
        )
        .map_err(|e| e.to_string())?;
    }
    println!("evolution complete; run with --select to pick and freeze the candidate");
    Ok(())
}

fn json_string_or_null(value: Option<&str>) -> String {
    match value {
        Some(text) => format!("\"{}\"", text.replace('\\', "\\\\").replace('"', "\\\"")),
        None => "null".to_string(),
    }
}

fn run_select(config: &Config) -> Result<(), String> {
    // The candidate pool is the population that played the last COMPLETED
    // generation (gen-{g}.json exists).  Under the checkpoint contract the
    // successor checkpoint population-{g+1}.bin also exists but has never
    // been evaluated, so discovery keys on the generation artifacts, not on
    // population files.
    let mut found: Option<usize> = None;
    for generation in (0..config.generations).rev() {
        if std::path::Path::new(&format!("{}/gen-{generation:03}.json", config.out)).exists() {
            found = Some(generation);
            break;
        }
    }
    let generation = found.ok_or("no completed generation found")?;
    let population = load_population(&format!("{}/population-{generation:03}.bin", config.out))?;
    // Fitness comes from the completed generation's own artifact, never
    // from a sidecar file: gen-{g}.json records every candidate's games,
    // so the ranking is atomically consistent with the completion marker.
    // (final-fitness.json is informational only — a kill after gen-{g}.json
    // leaves it absent or holding an earlier generation's means.)
    let artifact_text =
        std::fs::read_to_string(format!("{}/gen-{generation:03}.json", config.out))
            .map_err(|e| e.to_string())?;
    let artifact = drop7_nnue_evolution::json::parse(&artifact_text)?;
    let individuals = artifact
        .get("individuals")
        .and_then(Json::as_array)
        .ok_or("gen artifact missing individuals")?;
    let mut fitness = vec![None::<f64>; population.len()];
    for individual in individuals {
        let name = individual.get("name").and_then(Json::as_str).unwrap_or("");
        // Controls (control-*) rank nothing here.
        let Some(index) = name
            .strip_prefix("candidate-")
            .and_then(|n| n.parse::<usize>().ok())
        else {
            continue;
        };
        if index >= population.len() {
            return Err(format!(
                "gen artifact names {name} but population-{generation:03}.bin holds {}",
                population.len()
            ));
        }
        let games = individual
            .get("games")
            .and_then(Json::as_array)
            .ok_or_else(|| format!("gen artifact {name} missing games"))?;
        let mut sum = 0.0;
        for game in games {
            sum += game
                .get("score")
                .and_then(Json::as_f64)
                .ok_or_else(|| format!("gen artifact {name} has a game without a score"))?;
        }
        fitness[index] = Some(sum / games.len().max(1) as f64);
    }
    let fitness: Vec<f64> = fitness
        .into_iter()
        .enumerate()
        .map(|(index, mean)| {
            mean.ok_or(format!("gen artifact is missing candidate-{index:02}"))
        })
        .collect::<Result<_, _>>()?;

    // Top 8 by final-generation fitness enter the re-selection block: the
    // `select_games` seeds immediately after the last fitness block that was
    // played, so an early (plateau or budget) stop still re-selects on fresh
    // seeds inside the lease.  For a run that used every generation this is
    // the same block as lease_start + generations * games.
    let mut ranking: Vec<usize> = (0..population.len()).collect();
    ranking.sort_by(|&a, &b| fitness[b].partial_cmp(&fitness[a]).unwrap());
    let finalists: Vec<usize> = ranking.into_iter().take(8).collect();
    let block_start = config.lease_start + ((generation + 1) * config.games) as u32;
    println!(
        "re-selecting among {:?} on {} fresh games from {block_start:#010x}",
        finalists, config.select_games
    );

    let params = deployment_params();
    let mut tasks = Vec::new();
    for (slot, _) in finalists.iter().enumerate() {
        for game in 0..config.select_games {
            tasks.push(EvalTask {
                individual: slot,
                seed: block_start + game as u32,
            });
        }
    }
    let leaf_for = |index: usize| -> EvalLeaf {
        EvalLeaf::Nnue(NnueLeaf {
            net: population[finalists[index]].clone(),
        })
    };
    let records = evaluate_tasks(
        &leaf_for,
        &params,
        DEPLOYMENT_TABLE,
        &tasks,
        config.threads,
        config.move_cap,
    );
    let mut best_slot = 0usize;
    let mut best_mean = f64::NEG_INFINITY;
    let mut lines = String::new();
    for (slot, &candidate) in finalists.iter().enumerate() {
        let start = slot * config.select_games;
        let games = &records[start..start + config.select_games];
        let mean = games.iter().map(|g| g.score as f64).sum::<f64>() / games.len() as f64;
        lines.push_str(&format!(
            "candidate-{candidate:02}: mean {mean:.0} over {} games\n",
            games.len()
        ));
        if mean > best_mean {
            best_mean = mean;
            best_slot = slot;
        }
    }
    print!("{lines}");
    let winner = population[finalists[best_slot]].clone();
    winner
        .save(std::path::Path::new(&format!("{}/candidate-weights.bin", config.out)))
        .map_err(|e| e.to_string())?;
    std::fs::write(
        format!("{}/selection.json", config.out),
        format!(
            "{{\"finalGeneration\":{generation},\"finalists\":{:?},\"blockStart\":\"0x{block_start:08x}\",\"selectGames\":{},\"winner\":\"candidate-{:02}\",\"winnerMean\":{}}}\n",
            finalists, config.select_games, finalists[best_slot], best_mean
        ),
    )
    .map_err(|e| e.to_string())?;
    println!(
        "froze candidate-{:02} (mean {best_mean:.0}) to {}/candidate-weights.bin",
        finalists[best_slot], config.out
    );
    Ok(())
}

fn main() -> Result<(), String> {
    let config = parse_args()?;
    if config.select {
        run_select(&config)
    } else {
        run_evolution(&config)
    }
}
