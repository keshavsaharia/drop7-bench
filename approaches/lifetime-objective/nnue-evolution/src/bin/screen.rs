// Stage D: the held-out screen.  Plays the preregistered never-read cohort
// once, with four arms on identical seeds: the frozen evolved candidate, the
// unevolved supervised init (the ablation that isolates the evolutionary
// stage), the fair leaf at the identical d3s7 configuration (the primary
// comparator), and the fair leaf at d4s7 (the program's standing reference,
// diagnostic only).  Emits one population artifact; the unchanged compare.py
// of the prior leaf-evolution experiment computes the paired statistics.
//
// Usage: screen --candidate FILE --init FILE --seeds-start 0x... --games 64
//               --threads T --out FILE [--move-cap 2000]

use drop7_nnue_evolution::game::{evaluate_tasks, population_artifact_json, EvalLeaf, EvalTask, Individual, MOVE_CAP};
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::search::{work_bound_for, FairLeaf, SearchParams};

fn main() -> Result<(), String> {
    let mut candidate = None;
    let mut init = None;
    let mut seeds_start = None;
    let mut games = 64usize;
    let mut threads = 16usize;
    let mut out = None;
    let mut move_cap = MOVE_CAP;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--candidate" => candidate = Some(value.to_string()),
            "--init" => init = Some(value.to_string()),
            "--seeds-start" => {
                seeds_start = Some(
                    u32::from_str_radix(value.trim_start_matches("0x"), 16)
                        .map_err(|_| "bad --seeds-start")?,
                )
            }
            "--games" => games = value.parse().map_err(|_| "bad --games")?,
            "--threads" => threads = value.parse().map_err(|_| "bad --threads")?,
            "--move-cap" => move_cap = value.parse().map_err(|_| "bad --move-cap")?,
            "--out" => out = Some(value.to_string()),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let candidate = Nnue::load(std::path::Path::new(&candidate.ok_or("--candidate required")?))?;
    let init = Nnue::load(std::path::Path::new(&init.ok_or("--init required")?))?;
    let seeds_start = seeds_start.ok_or("--seeds-start required")?;
    let out = out.ok_or("--out required")?;

    // Arm order: candidate, init, fair-d3s7, fair-d4s7.  The d4 arm carries
    // its own search parameters; evaluate_tasks takes one params value, so
    // the d4 arm is evaluated in a second pass and merged.
    let names = ["candidate", "init-d3s7", "fair-d3s7", "fair-d4s7"];
    let d3 = deployment_params();
    let d4 = SearchParams {
        depth: 4,
        chance_samples: 7,
        terminal_utility: d3.terminal_utility,
        maximum_work: work_bound_for(4, 7) + 1,
        policy_seed: d3.policy_seed,
    };

    let mut individuals: Vec<Individual> = Vec::new();
    for (arm, name) in names.iter().enumerate() {
        let (params, table) = if arm == 3 {
            (d4, 1_048_576usize)
        } else {
            (d3, DEPLOYMENT_TABLE)
        };
        let tasks: Vec<EvalTask> = (0..games)
            .map(|game| EvalTask {
                individual: 0,
                seed: seeds_start + game as u32,
            })
            .collect();
        let leaf_for = |_index: usize| -> EvalLeaf {
            match arm {
                0 => EvalLeaf::Nnue(NnueLeaf {
                    net: candidate.clone(),
                }),
                1 => EvalLeaf::Nnue(NnueLeaf { net: init.clone() }),
                _ => EvalLeaf::Fair(FairLeaf::default()),
            }
        };
        let records = evaluate_tasks(&leaf_for, &params, table, &tasks, threads, move_cap);
        individuals.push(Individual {
            name: name.to_string(),
            games: records,
        });
        let last = &individuals[arm];
        let mean = last.games.iter().map(|g| g.score as f64).sum::<f64>() / games as f64;
        eprintln!("arm {name}: mean {mean:.0} over {games} games");
    }

    let config_json = format!(
        "{{\"experiment\":\"EX-20260825-nnue-evolution-d3-bca7f330\",\"screen\":true,\"games\":{games},\"moveCap\":{move_cap}}}"
    );
    let artifact = population_artifact_json(&config_json, seeds_start, &individuals);
    std::fs::write(&out, artifact).map_err(|e| e.to_string())?;
    eprintln!("wrote {out}");
    Ok(())
}
