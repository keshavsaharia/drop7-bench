// Stage D: the held-out screen.  Plays the preregistered never-read cohort
// once, with arms on identical seeds: the frozen evolved candidate, any
// extra NNUE arms named on the command line (for the continuation
// experiment: the earlier run's frozen candidate), the unevolved supervised
// init (the ablation that isolates the evolutionary stage), the fair leaf at
// the identical d3s7 configuration (the primary comparator), and the fair
// leaf at d4s7 (the program's standing reference, diagnostic only).  Emits
// one population artifact; the unchanged compare.py of the prior
// leaf-evolution experiment computes the paired statistics.
//
// Usage: screen --candidate FILE --init FILE --seeds-start 0x... --games 64
//               --threads T --out FILE [--move-cap 2000]
//               [--arm NAME=FILE]... [--experiment-id EX-...]

use drop7_nnue_evolution::game::{evaluate_tasks, population_artifact_json, EvalLeaf, EvalTask, Individual, MOVE_CAP};
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::search::{work_bound_for, FairLeaf, SearchParams};

const DEFAULT_EXPERIMENT_ID: &str = "EX-20260902-nnue-evolution-d3-v2-49c18bc2";

enum Arm {
    Net(Nnue),
    FairD3,
    FairD4,
}

fn main() -> Result<(), String> {
    let mut candidate = None;
    let mut init = None;
    let mut seeds_start = None;
    let mut games = 64usize;
    let mut threads = 16usize;
    let mut out = None;
    let mut move_cap = MOVE_CAP;
    let mut extra: Vec<(String, String)> = Vec::new();
    let mut experiment_id = DEFAULT_EXPERIMENT_ID.to_string();
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
            "--arm" => {
                let (name, path) = value
                    .split_once('=')
                    .ok_or("--arm expects NAME=FILE")?;
                if name.is_empty() || name.contains('"') || name.contains(char::is_whitespace) {
                    return Err(format!("bad --arm name {name:?}"));
                }
                extra.push((name.to_string(), path.to_string()));
            }
            "--experiment-id" => experiment_id = value.to_string(),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let candidate = Nnue::load(std::path::Path::new(&candidate.ok_or("--candidate required")?))?;
    let init = Nnue::load(std::path::Path::new(&init.ok_or("--init required")?))?;
    let seeds_start = seeds_start.ok_or("--seeds-start required")?;
    let out = out.ok_or("--out required")?;

    // Arm order: candidate, extra arms in command-line order, init, fair-d3s7,
    // fair-d4s7.  The d4 arm carries its own search parameters; evaluate_tasks
    // takes one params value, so each arm is evaluated in its own pass.
    let mut arms: Vec<(String, Arm)> = vec![("candidate".to_string(), Arm::Net(candidate))];
    for (name, path) in &extra {
        for (existing, _) in &arms {
            if existing == name {
                return Err(format!("duplicate arm name {name}"));
            }
        }
        if ["init-d3s7", "fair-d3s7", "fair-d4s7"].contains(&name.as_str()) {
            return Err(format!("arm name {name} is reserved"));
        }
        arms.push((name.clone(), Arm::Net(Nnue::load(std::path::Path::new(path))?)));
    }
    arms.push(("init-d3s7".to_string(), Arm::Net(init)));
    arms.push(("fair-d3s7".to_string(), Arm::FairD3));
    arms.push(("fair-d4s7".to_string(), Arm::FairD4));

    let d3 = deployment_params();
    let d4 = SearchParams {
        depth: 4,
        chance_samples: 7,
        terminal_utility: d3.terminal_utility,
        maximum_work: work_bound_for(4, 7) + 1,
        policy_seed: d3.policy_seed,
    };

    let mut individuals: Vec<Individual> = Vec::new();
    for (name, arm) in &arms {
        let (params, table) = match arm {
            Arm::FairD4 => (d4, 1_048_576usize),
            _ => (d3, DEPLOYMENT_TABLE),
        };
        let tasks: Vec<EvalTask> = (0..games)
            .map(|game| EvalTask {
                individual: 0,
                seed: seeds_start + game as u32,
            })
            .collect();
        let leaf_for = |_index: usize| -> EvalLeaf {
            match arm {
                Arm::Net(net) => EvalLeaf::Nnue(NnueLeaf { net: net.clone() }),
                Arm::FairD3 | Arm::FairD4 => EvalLeaf::Fair(FairLeaf::default()),
            }
        };
        let records = evaluate_tasks(&leaf_for, &params, table, &tasks, threads, move_cap);
        let mean = records.iter().map(|g| g.score as f64).sum::<f64>() / games as f64;
        eprintln!("arm {name}: mean {mean:.0} over {games} games");
        individuals.push(Individual {
            name: name.clone(),
            games: records,
        });
    }

    let config_json = format!(
        "{{\"experiment\":\"{experiment_id}\",\"screen\":true,\"games\":{games},\"moveCap\":{move_cap},\"arms\":[{}]}}",
        arms.iter()
            .map(|(name, _)| format!("\"{name}\""))
            .collect::<Vec<_>>()
            .join(",")
    );
    let artifact = population_artifact_json(&config_json, seeds_start, &individuals);
    std::fs::write(&out, artifact).map_err(|e| e.to_string())?;
    eprintln!("wrote {out}");
    Ok(())
}
