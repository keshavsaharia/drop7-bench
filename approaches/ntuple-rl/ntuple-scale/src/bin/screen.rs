// The paired screen (and the pilot's arm comparison).  Plays one seed block
// once with every arm on identical seeds: the frozen candidate tables as the
// leaf of the deployment search (candidate-d3s7), the same tables played
// directly one ply (candidate-1ply), the frozen fair leaf at the identical
// d3s7 configuration (fair-d3s7, the comparator) and, unless skipped, the fair
// leaf at d4s7 (the program's standing reference, diagnostic only).  Extra
// --arm NAME=FILE tables add NAME-d3s7 and NAME-1ply arms, and --arm-d4
// NAME=FILE adds NAME-d4s7 (the same tables as the leaf of the reference
// depth-4 search).  A table file named by more than one option is loaded
// once and shared.  Emits one population artifact; the unchanged compare.py
// of the leaf-evolution experiment computes the paired statistics.
//
// Usage: screen [--candidate FILE] --seeds-start 0x... --games N --threads T
//               --out FILE [--move-cap 2000] [--skip-d4] [--skip-direct]
//               [--arm NAME=FILE]... [--arm-d4 NAME=FILE]... [--experiment-id EX-...]

use std::collections::HashMap;
use std::sync::Arc;

use drop7_ntuple_scale::game::{atomic_write, evaluate_arm, mean_moves, mean_score, population_artifact_json, Arm, Individual, MOVE_CAP};
use drop7_ntuple_scale::model::Model;

const DEFAULT_EXPERIMENT_ID: &str = "EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620";

fn main() -> Result<(), String> {
    let mut candidate = None;
    let mut seeds_start = None;
    let mut games = 256usize;
    let mut threads = 32usize;
    let mut out = None;
    let mut move_cap = MOVE_CAP;
    let mut skip_d4 = false;
    let mut skip_direct = false;
    let mut extra: Vec<(String, String)> = Vec::new();
    let mut extra_d4: Vec<(String, String)> = Vec::new();
    let mut experiment_id = DEFAULT_EXPERIMENT_ID.to_string();
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--skip-d4" => {
                skip_d4 = true;
                i += 1;
                continue;
            }
            "--skip-direct" => {
                skip_direct = true;
                i += 1;
                continue;
            }
            _ => {}
        }
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--candidate" => candidate = Some(value.to_string()),
            "--seeds-start" => {
                seeds_start = Some(u32::from_str_radix(value.trim_start_matches("0x"), 16).map_err(|_| "bad --seeds-start")?)
            }
            "--games" => games = value.parse().map_err(|_| "bad --games")?,
            "--threads" => threads = value.parse().map_err(|_| "bad --threads")?,
            "--move-cap" => move_cap = value.parse().map_err(|_| "bad --move-cap")?,
            "--out" => out = Some(value.to_string()),
            "--arm" | "--arm-d4" => {
                let (name, path) = value.split_once('=').ok_or("--arm expects NAME=FILE")?;
                if name.is_empty() || name.contains('"') || name.contains(char::is_whitespace) {
                    return Err(format!("bad --arm name {name:?}"));
                }
                if args[i] == "--arm" {
                    extra.push((name.to_string(), path.to_string()));
                } else {
                    extra_d4.push((name.to_string(), path.to_string()));
                }
            }
            "--experiment-id" => experiment_id = value.to_string(),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let seeds_start = seeds_start.ok_or("--seeds-start required")?;
    let out = out.ok_or("--out required")?;
    let seeds: Vec<u32> = (0..games as u32).map(|g| seeds_start.wrapping_add(g)).collect();

    let mut loaded: HashMap<String, Arc<Model>> = HashMap::new();
    let mut load = |path: &str| -> Result<Arc<Model>, String> {
        if let Some(model) = loaded.get(path) {
            return Ok(model.clone());
        }
        let model = Arc::new(Model::load(std::path::Path::new(path), false)?);
        loaded.insert(path.to_string(), model.clone());
        Ok(model)
    };
    let mut arms: Vec<(String, Arm)> = Vec::new();
    if let Some(path) = candidate {
        let model = load(&path)?;
        arms.push(("candidate-d3s7".into(), Arm::NTupleD3(model.clone())));
        if !skip_direct {
            arms.push(("candidate-1ply".into(), Arm::Direct(model)));
        }
    }
    for (name, path) in &extra {
        if ["candidate", "fair-d3s7", "fair-d4s7"].contains(&name.as_str()) {
            return Err(format!("arm name {name} is reserved"));
        }
        let model = load(path)?;
        arms.push((format!("{name}-d3s7"), Arm::NTupleD3(model.clone())));
        if !skip_direct {
            arms.push((format!("{name}-1ply"), Arm::Direct(model)));
        }
    }
    for (name, path) in &extra_d4 {
        if ["candidate", "fair-d3s7", "fair-d4s7"].contains(&name.as_str()) {
            return Err(format!("arm name {name} is reserved"));
        }
        let model = load(path)?;
        arms.push((format!("{name}-d4s7"), Arm::NTupleD4(model)));
    }
    arms.push(("fair-d3s7".into(), Arm::FairD3));
    if !skip_d4 {
        arms.push(("fair-d4s7".into(), Arm::FairD4));
    }

    let mut individuals: Vec<Individual> = Vec::new();
    for (name, arm) in &arms {
        let started = std::time::Instant::now();
        let records = evaluate_arm(arm, &seeds, threads, move_cap);
        eprintln!(
            "arm {name}: mean {:.0} / {:.2} moves over {games} games ({:.0} s)",
            mean_score(&records),
            mean_moves(&records),
            started.elapsed().as_secs_f64()
        );
        individuals.push(Individual { name: name.clone(), games: records });
    }
    let config_json = format!(
        "{{\"experiment\":\"{experiment_id}\",\"screen\":true,\"games\":{games},\"moveCap\":{move_cap},\"arms\":[{}]}}",
        arms.iter().map(|(name, _)| format!("\"{name}\"")).collect::<Vec<_>>().join(",")
    );
    let artifact = population_artifact_json(&config_json, seeds_start, &individuals);
    atomic_write(std::path::Path::new(&out), artifact.as_bytes())?;
    eprintln!("wrote {out}");
    Ok(())
}
