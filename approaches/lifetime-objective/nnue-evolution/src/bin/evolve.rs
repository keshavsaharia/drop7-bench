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
// Per generation the binary writes:
//   gen-NNN.json    population artifact (compare.py-compatible), controls
//                   included as named individuals
//   population.bin  all genomes, P x parameter_count little-endian f32
//   progress.jsonl  one summary line per generation
// and on --select:
//   candidate-weights.bin, selection.json
//
// Usage:
//   evolve --init FILE --lease-start 0x... --out DIR [--population 32]
//          [--games 32] [--generations 60] [--elites 4] [--tournament 3]
//          [--sigma-rel 0.05] [--seed 0x...] [--threads 16]
//          [--wall-seconds N] [--move-cap 2000]
//   evolve --select --lease-start 0x... --out DIR [--select-games 128]

use drop7_nnue_evolution::game::{evaluate_tasks, population_artifact_json, EvalLeaf, EvalTask, Individual, MOVE_CAP};
use drop7_nnue_evolution::json::Json;
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::rng::{mix32, Mulberry32};
use drop7_rs::search::FairLeaf;
use std::io::Write;

const EXPERIMENT_ID: &str = "EX-20260825-nnue-evolution-d3-bca7f330";

const CONTROL_FAIR: &str = "control-fair-d3s7";
const CONTROL_INIT: &str = "control-init-d3s7";

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

/// Evaluate one generation: every candidate plus the two controls on the
/// same seed block.  Returns per-individual mean scores and writes the
/// population artifact.
#[allow(clippy::too_many_arguments)]
fn evaluate_generation(
    config: &Config,
    population: &[Nnue],
    init: &Nnue,
    generation: usize,
    block_start: u32,
) -> Result<Vec<f64>, String> {
    let params = deployment_params();
    let individuals = population.len() + 2;
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
        } else {
            EvalLeaf::Nnue(NnueLeaf { net: init.clone() })
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
        } else {
            CONTROL_INIT.to_string()
        };
        let start = index * config.games;
        assembled.push(Individual {
            name,
            games: records[start..start + config.games].to_vec(),
        });
    }
    let config_json = format!(
        "{{\"experiment\":\"{EXPERIMENT_ID}\",\"generation\":{generation},\"depth\":3,\"strata\":7,\"games\":{},\"moveCap\":{}}}",
        config.games, config.move_cap
    );
    let artifact = population_artifact_json(&config_json, block_start, &assembled);
    std::fs::write(format!("{}/gen-{generation:03}.json", config.out), artifact)
        .map_err(|e| e.to_string())?;

    let means: Vec<f64> = assembled
        .iter()
        .map(|individual| {
            individual.games.iter().map(|g| g.score as f64).sum::<f64>()
                / individual.games.len().max(1) as f64
        })
        .collect();
    Ok(means)
}

fn run_evolution(config: &Config) -> Result<(), String> {
    let init = Nnue::load(std::path::Path::new(&config.init))?;
    std::fs::create_dir_all(&config.out).map_err(|e| e.to_string())?;
    std::fs::write(
        format!("{}/config.json", config.out),
        format!(
            "{{\"population\":{},\"games\":{},\"generations\":{},\"elites\":{},\"tournament\":{},\"sigmaRel\":{},\"seed\":\"0x{:08x}\",\"leaseStart\":\"0x{:08x}\",\"moveCap\":{},\"wallSeconds\":{}}}\n",
            config.population, config.games, config.generations, config.elites,
            config.tournament, config.sigma_rel, config.seed, config.lease_start,
            config.move_cap, config.wall_seconds
        ),
    )
    .map_err(|e| e.to_string())?;

    // Checkpoint contract: population-{g}.bin holds the population that PLAYS
    // generation g, saved before the first game of g.  A generation is
    // complete once its gen-{g}.json artifact exists.  Resume therefore loads
    // the population after the last completed generation; a population whose
    // generation never finished is simply replayed (training seeds are
    // deterministic, so a replayed block is wasted compute, not new data).
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
                // Generation 0: candidate 0 is the exact init; the rest are
                // a 2-sigma cloud around it.
                let mut initial = Vec::with_capacity(config.population);
                initial.push(init.clone());
                for child in 1..config.population {
                    initial.push(mutate(&init, config.seed, usize::MAX, child, 2.0 * config.sigma_rel));
                }
                initial
            }
        }
    };

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
        // Save the population that is about to play this generation, first.
        save_population(
            &format!("{}/population-{generation:03}.bin", config.out),
            &population,
        )?;
        let block_start = config.lease_start + (generation * config.games) as u32;
        let means = evaluate_generation(config, &population, &init, generation, block_start)?;
        last_fitness = means.clone();
        let best = means[..config.population]
            .iter()
            .cloned()
            .fold(f64::NEG_INFINITY, f64::max);
        let mean_all: f64 =
            means[..config.population].iter().sum::<f64>() / config.population as f64;
        let control_fair = means[config.population];
        let control_init = means[config.population + 1];
        println!(
            "generation {generation}: best {best:.0} mean {mean_all:.0} | fair {control_fair:.0} init {control_init:.0} ({:.0}s)",
            started.elapsed().as_secs_f64()
        );
        let progress = format!(
            "{{\"generation\":{generation},\"blockStart\":\"0x{block_start:08x}\",\"best\":{best},\"mean\":{mean_all},\"controlFair\":{control_fair},\"controlInit\":{control_init},\"fitness\":[{}]}}\n",
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
                config.sigma_rel,
            ));
        }
        population = next;
        // The selected population is checkpointed at the start of the next
        // iteration, before it plays a game (see the checkpoint contract).
    }

    // Persist the final fitness for --select.  Only when this process
    // actually evaluated a generation; a no-op resume must not clobber the
    // completing run's file.
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

fn run_select(config: &Config) -> Result<(), String> {
    // The final population is the last checkpoint written by the run.
    let mut found: Option<usize> = None;
    for generation in (0..config.generations).rev() {
        if std::path::Path::new(&format!("{}/population-{generation:03}.bin", config.out)).exists() {
            found = Some(generation);
            break;
        }
    }
    let generation = found.ok_or("no population checkpoint found")?;
    let population = load_population(&format!("{}/population-{generation:03}.bin", config.out))?;
    let fitness_text = std::fs::read_to_string(format!("{}/final-fitness.json", config.out))
        .map_err(|e| e.to_string())?;
    let fitness_json = drop7_nnue_evolution::json::parse(&fitness_text)?;
    let fitness: Vec<f64> = fitness_json
        .get("fitness")
        .and_then(Json::as_array)
        .ok_or("final-fitness.json missing fitness")?
        .iter()
        .map(|v| v.as_f64().unwrap_or(f64::NEG_INFINITY))
        .collect();

    // Top 8 by final-generation fitness enter the re-selection block.
    let mut ranking: Vec<usize> = (0..population.len()).collect();
    ranking.sort_by(|&a, &b| fitness[b].partial_cmp(&fitness[a]).unwrap());
    let finalists: Vec<usize> = ranking.into_iter().take(8).collect();
    let block_start = config.lease_start + (config.generations * config.games) as u32;
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
