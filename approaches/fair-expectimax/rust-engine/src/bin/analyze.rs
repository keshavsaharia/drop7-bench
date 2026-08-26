//! Fixed-work depth × strata × leaf matrix over public Drop7 roots.
//!
//! Output is canonical JSONL: one manifest followed by one decision record for
//! every requested root/leaf/strata/depth cell.  Each decision retains all
//! legal sibling values (decimal and exact f64 bits), the selected action,
//! logical work, scheduler timing, memory projections, and per-worker load.

use drop7_rs::board::{Board, BOARD_SIZE, MOVES_PER_LEVEL};
use drop7_rs::engine::State;
use drop7_rs::leaf::LeafWeights;
use drop7_rs::parallel::{
    choose_action_frontier_parallel_with_leaf, choose_action_root_parallel_with_leaf,
    ParallelConfig, ParallelDecision, DEFAULT_MAX_FRONTIER_TASKS, DEFAULT_MAX_HOST_BYTES,
};
use drop7_rs::search::{canonical_state, work_bound_for, SearchParams, WeightedLeaf, COLUMN_ORDER};

use std::env;
use std::fs::{self, File};
use std::io::{self, BufWriter, Write};

#[derive(Clone)]
struct NamedLeaf {
    name: String,
    source: Option<String>,
    weights: LeafWeights,
}

fn json_string(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 2);
    out.push('"');
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            ch if ch < ' ' => {
                use std::fmt::Write as _;
                write!(&mut out, "\\u{:04x}", ch as u32).unwrap();
            }
            ch => out.push(ch),
        }
    }
    out.push('"');
    out
}

fn parse_list<T>(text: &str, name: &str) -> Result<Vec<T>, String>
where
    T: std::str::FromStr + Ord,
{
    let mut values = Vec::new();
    for field in text.split(',') {
        let value = field
            .trim()
            .parse()
            .map_err(|_| format!("{name} contains invalid value {field}"))?;
        values.push(value);
    }
    values.sort();
    values.dedup();
    if values.is_empty() {
        return Err(format!("{name} must not be empty"));
    }
    Ok(values)
}

fn parse_leaf(spec: &str) -> Result<NamedLeaf, String> {
    if spec == "fair" {
        return Ok(NamedLeaf {
            name: "fair".into(),
            source: None,
            weights: LeafWeights::frozen(),
        });
    }
    let Some((name, path)) = spec.split_once('=') else {
        return Err("--leaf must be fair or NAME=WEIGHTS_FILE".into());
    };
    if name.is_empty() || path.is_empty() {
        return Err("--leaf NAME=WEIGHTS_FILE needs both fields".into());
    }
    Ok(NamedLeaf {
        name: name.into(),
        source: Some(path.into()),
        weights: LeafWeights::read_named(path)?,
    })
}

fn read_roots(path: &str, limit: usize) -> Result<Vec<State>, String> {
    let text = fs::read_to_string(path).map_err(|error| format!("cannot read {path}: {error}"))?;
    let mut roots = Vec::new();
    for (line_number, line) in text.lines().enumerate() {
        let Some(rest) = line.strip_prefix("s ") else {
            continue;
        };
        let fields: Vec<&str> = rest.split_whitespace().collect();
        if fields.len() != 5 {
            return Err(format!("{path}:{}: malformed root", line_number + 1));
        }
        let board = Board::from_serialized(fields[0])
            .ok_or_else(|| format!("{path}:{}: invalid board", line_number + 1))?;
        let next_disc: u8 = fields[1]
            .parse()
            .map_err(|_| format!("{path}:{}: invalid next disc", line_number + 1))?;
        let moves_remaining: i32 = fields[2]
            .parse()
            .map_err(|_| format!("{path}:{}: invalid rise clock", line_number + 1))?;
        let level: i32 = fields[3]
            .parse()
            .map_err(|_| format!("{path}:{}: invalid level", line_number + 1))?;
        let game_over = match fields[4] {
            "0" => false,
            "1" => true,
            _ => return Err(format!("{path}:{}: invalid terminal flag", line_number + 1)),
        };
        if !(1..=BOARD_SIZE as u8).contains(&next_disc)
            || !(0..=MOVES_PER_LEVEL).contains(&moves_remaining)
            || level < 1
        {
            return Err(format!(
                "{path}:{}: root field out of range",
                line_number + 1
            ));
        }
        if !game_over {
            roots.push(State {
                board,
                next_disc,
                score: 0,
                level,
                moves_remaining,
                moves_played: 0,
                game_over,
            });
        }
        if roots.len() == limit {
            break;
        }
    }
    if roots.is_empty() {
        return Err(format!("{path} contains no non-terminal `s ` roots"));
    }
    Ok(roots)
}

fn write_manifest(
    out: &mut dyn Write,
    roots_path: &str,
    roots: usize,
    depths: &[i32],
    strata: &[i32],
    leaves: &[NamedLeaf],
    scheduler: &str,
    config: ParallelConfig,
) -> io::Result<()> {
    write!(
        out,
        "{{\"format\":\"drop7-search-matrix-v1\",\"recordType\":\"manifest\",\"rootsPath\":{},\"roots\":{},\"scheduler\":{},\"threads\":{},\"cacheEntriesPerWorker\":{},\"maxFrontierTasks\":{},\"maxHostBytes\":{},\"splitPlies\":",
        json_string(roots_path),
        roots,
        json_string(scheduler),
        config.threads,
        config.table_capacity_per_worker,
        config.max_frontier_tasks,
        config.max_host_bytes,
    )?;
    match config.split_plies {
        Some(split) => write!(out, "{split}")?,
        None => write!(out, "\"auto\"")?,
    }
    write!(out, ",\"depths\":[")?;
    for (index, depth) in depths.iter().enumerate() {
        write!(out, "{}{}", if index == 0 { "" } else { "," }, depth)?;
    }
    write!(out, "],\"strata\":[")?;
    for (index, samples) in strata.iter().enumerate() {
        write!(out, "{}{}", if index == 0 { "" } else { "," }, samples)?;
    }
    write!(out, "],\"leaves\":[")?;
    for (index, leaf) in leaves.iter().enumerate() {
        write!(
            out,
            "{}{{\"name\":{},\"source\":{},\"frozen\":{}}}",
            if index == 0 { "" } else { "," },
            json_string(&leaf.name),
            leaf.source
                .as_deref()
                .map(json_string)
                .unwrap_or_else(|| "null".into()),
            leaf.weights.is_frozen(),
        )?;
    }
    writeln!(out, "]}}")
}

#[allow(clippy::too_many_arguments)]
fn write_decision(
    out: &mut dyn Write,
    root_index: usize,
    state: &State,
    leaf: &NamedLeaf,
    depth: i32,
    strata: i32,
    previous_depth: Option<i32>,
    previous_action: Option<i32>,
    decision: &ParallelDecision,
) -> io::Result<()> {
    let metrics = &decision.metrics;
    // ParallelDecision retains canonical columns so scheduler parity can be
    // checked directly. Analytics are an operator-facing public-position
    // artifact, so map siblings back to the input board's coordinates.
    let mirrored = canonical_state(state).1;
    let mut source_columns: Vec<(usize, f64)> = decision
        .column_values
        .iter()
        .map(|&(column, value)| {
            (
                if mirrored {
                    BOARD_SIZE - 1 - column
                } else {
                    column
                },
                value,
            )
        })
        .collect();
    source_columns.sort_by_key(|(column, _)| {
        COLUMN_ORDER
            .iter()
            .position(|candidate| candidate == column)
            .unwrap_or(BOARD_SIZE)
    });
    write!(
        out,
        "{{\"format\":\"drop7-search-matrix-v1\",\"recordType\":\"decision\",\"root\":{},\"board\":{},\"nextDisc\":{},\"movesUntilRise\":{},\"leaf\":{},\"leafSource\":{},\"leafFrozen\":{},\"depth\":{},\"strata\":{},\"previousDepth\":{},\"previousAction\":{},\"actionChangedFromPreviousDepth\":{},\"selectedAction\":{},\"columns\":[",
        root_index,
        json_string(&state.board.serialize()),
        state.next_disc,
        state.moves_remaining,
        json_string(&leaf.name),
        leaf.source
            .as_deref()
            .map(json_string)
            .unwrap_or_else(|| "null".into()),
        leaf.weights.is_frozen(),
        depth,
        strata,
        previous_depth
            .map(|value| value.to_string())
            .unwrap_or_else(|| "null".into()),
        previous_action
            .map(|value| value.to_string())
            .unwrap_or_else(|| "null".into()),
        previous_action
            .map(|value| value != decision.action)
            .unwrap_or(false),
        decision.action,
    )?;
    for (index, (column, value)) in source_columns.iter().enumerate() {
        write!(
            out,
            "{}{{\"column\":{},\"value\":{:.17e},\"valueBits\":\"{:016x}\",\"selected\":{}}}",
            if index == 0 { "" } else { "," },
            column,
            value,
            value.to_bits(),
            *column as i32 == decision.action,
        )?;
    }
    write!(
        out,
        "],\"metrics\":{{\"workerThreads\":{},\"splitPlies\":{},\"registeredTasks\":{},\"completedTasks\":{},\"plannerNodes\":{},\"plannerWork\":{},\"plannerCacheHits\":{},\"work\":{},\"nodes\":{},\"leafCalls\":{},\"moveCalls\":{},\"cacheHits\":{},\"tableBytesPerWorker\":{},\"projectedTableBytes\":{},\"projectedPlanBytes\":{},\"initializationSeconds\":{:.9},\"planningSeconds\":{:.9},\"executionSeconds\":{:.9},\"reductionSeconds\":{:.9},\"wallSeconds\":{:.9},\"workerBusyFraction\":{:.9},\"tailIdleCoreSeconds\":{:.9}}},\"workers\":[",
        metrics.worker_threads,
        metrics.split_plies,
        metrics.frontier_tasks,
        metrics.completed_tasks,
        metrics.planner_nodes,
        metrics.planner_work,
        metrics.planner_cache_hits,
        metrics.work,
        metrics.nodes,
        metrics.leaf_calls,
        metrics.move_calls,
        metrics.cache_hits,
        metrics.table_bytes_per_worker,
        metrics.projected_table_bytes,
        metrics.projected_plan_bytes,
        metrics.initialization_seconds,
        metrics.planning_seconds,
        metrics.execution_seconds,
        metrics.reduction_seconds,
        metrics.wall_seconds,
        metrics.worker_busy_fraction,
        metrics.tail_idle_core_seconds,
    )?;
    for (index, worker) in metrics.workers.iter().enumerate() {
        write!(
            out,
            "{}{{\"worker\":{},\"tasks\":{},\"busySeconds\":{:.9},\"work\":{},\"nodes\":{},\"leafCalls\":{},\"moveCalls\":{},\"cacheHits\":{}}}",
            if index == 0 { "" } else { "," },
            worker.worker,
            worker.tasks,
            worker.busy_seconds,
            worker.work,
            worker.nodes,
            worker.leaf_calls,
            worker.move_calls,
            worker.cache_hits,
        )?;
    }
    writeln!(out, "]}}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use drop7_rs::parallel::{ParallelMetrics, ParallelScheduler};

    #[test]
    fn mirrored_analytics_mark_the_source_column() {
        let board = Board::from_serialized("0000000000000000000000000000000000000000001000000")
            .expect("board");
        let state = State {
            board,
            next_disc: 4,
            score: 0,
            level: 1,
            moves_remaining: 5,
            moves_played: 0,
            game_over: false,
        };
        assert!(canonical_state(&state).1);
        let decision = ParallelDecision {
            action: 4,
            column_values: vec![(3, 1.0), (2, 2.0)],
            metrics: ParallelMetrics {
                scheduler: ParallelScheduler::CentralFrontier,
                action: 4,
                completed_depth: 1,
                requested_threads: 1,
                worker_threads: 1,
                split_plies: 0,
                frontier_tasks: 1,
                completed_tasks: 1,
                planner_nodes: 0,
                planner_work: 0,
                planner_move_calls: 0,
                planner_leaf_calls: 0,
                planner_cache_hits: 0,
                work: 0,
                nodes: 0,
                leaf_calls: 0,
                move_calls: 0,
                cache_hits: 0,
                table_bytes_per_worker: 0,
                projected_table_bytes: 0,
                projected_plan_bytes: 0,
                initialization_seconds: 0.0,
                planning_seconds: 0.0,
                execution_seconds: 0.0,
                reduction_seconds: 0.0,
                wall_seconds: 0.0,
                worker_busy_fraction: 0.0,
                tail_idle_core_seconds: 0.0,
                workers: Vec::new(),
            },
        };
        let leaf = NamedLeaf {
            name: "fair".into(),
            source: None,
            weights: LeafWeights::frozen(),
        };
        let mut output = Vec::new();
        write_decision(&mut output, 0, &state, &leaf, 1, 1, None, None, &decision).expect("write");
        let text = String::from_utf8(output).expect("utf8");
        assert!(text.contains("\"column\":4,\"value\":2.00000000000000000e0"));
        assert!(text.contains("\"selected\":true"));
    }
}

fn usage() {
    eprintln!(
        "usage: analyze --roots FILE --output FILE|- --depths 4,5 --strata 5,7 [--leaf fair|NAME=WEIGHTS_FILE] [--scheduler frontier|root] [--threads N] [--cache N] [--split-plies auto|N] [--root-limit N] [--max-frontier-tasks N] [--max-host-bytes N]"
    );
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    let mut roots_path = String::new();
    let mut output_path = String::new();
    let mut depths = vec![4i32];
    let mut strata = vec![7i32];
    let mut leaves = Vec::new();
    let mut scheduler = String::from("frontier");
    let mut threads = std::thread::available_parallelism().map_or(1, |n| n.get());
    let mut cache = 262_144usize;
    let mut split_plies = None;
    let mut root_limit = usize::MAX;
    let mut max_frontier_tasks = DEFAULT_MAX_FRONTIER_TASKS;
    let mut max_host_bytes = DEFAULT_MAX_HOST_BYTES;
    let mut index = 1usize;
    while index < args.len() {
        if args[index] == "--help" {
            usage();
            return Ok(());
        }
        if index + 1 >= args.len() {
            return Err(format!("{} needs a value", args[index]));
        }
        let value = &args[index + 1];
        match args[index].as_str() {
            "--roots" => roots_path = value.clone(),
            "--output" => output_path = value.clone(),
            "--depths" => depths = parse_list(value, "--depths")?,
            "--strata" => strata = parse_list(value, "--strata")?,
            "--leaf" => leaves.push(parse_leaf(value)?),
            "--scheduler" => scheduler = value.clone(),
            "--threads" => threads = value.parse().map_err(|_| "invalid --threads")?,
            "--cache" => cache = value.parse().map_err(|_| "invalid --cache")?,
            "--split-plies" => {
                split_plies = if value == "auto" {
                    None
                } else {
                    Some(value.parse().map_err(|_| "invalid --split-plies")?)
                }
            }
            "--root-limit" => root_limit = value.parse().map_err(|_| "invalid --root-limit")?,
            "--max-frontier-tasks" => {
                max_frontier_tasks = value.parse().map_err(|_| "invalid --max-frontier-tasks")?
            }
            "--max-host-bytes" => {
                max_host_bytes = value.parse().map_err(|_| "invalid --max-host-bytes")?
            }
            option => return Err(format!("unknown option {option}")),
        }
        index += 2;
    }
    if roots_path.is_empty() || output_path.is_empty() {
        usage();
        return Err("--roots and --output are required".into());
    }
    if scheduler != "frontier" && scheduler != "root" {
        return Err("--scheduler must be frontier or root".into());
    }
    if depths.iter().any(|depth| *depth < 1) || strata.iter().any(|count| *count < 1) {
        return Err("depths and strata must be positive".into());
    }
    if leaves.is_empty() {
        leaves.push(parse_leaf("fair")?);
    }
    let roots = read_roots(&roots_path, root_limit)?;
    let config = ParallelConfig {
        threads,
        table_capacity_per_worker: cache,
        split_plies,
        max_frontier_tasks,
        max_host_bytes,
        ..ParallelConfig::default()
    };
    let mut output: Box<dyn Write> = if output_path == "-" {
        Box::new(BufWriter::new(io::stdout().lock()))
    } else {
        Box::new(BufWriter::new(File::create(&output_path).map_err(
            |error| format!("cannot create {output_path}: {error}"),
        )?))
    };
    write_manifest(
        &mut output,
        &roots_path,
        roots.len(),
        &depths,
        &strata,
        &leaves,
        &scheduler,
        config,
    )
    .map_err(|error| format!("cannot write output: {error}"))?;
    let mut records = 0usize;
    for (root_index, state) in roots.iter().enumerate() {
        for leaf in &leaves {
            for &samples in &strata {
                let mut previous_depth = None;
                let mut previous_action = None;
                for &depth in &depths {
                    let params = SearchParams {
                        depth,
                        chance_samples: samples,
                        terminal_utility: -1_000_000.0,
                        maximum_work: work_bound_for(depth, samples) + 1,
                        policy_seed: 0xd707_5eed,
                    };
                    let weights = leaf.weights;
                    let decision = if scheduler == "frontier" {
                        choose_action_frontier_parallel_with_leaf(
                            state,
                            params,
                            config,
                            move || WeightedLeaf::new(weights),
                        )
                    } else {
                        choose_action_root_parallel_with_leaf(state, params, config, move || {
                            WeightedLeaf::new(weights)
                        })
                    }
                    .map_err(|error| {
                        format!(
                            "root {root_index} leaf {} depth {depth} strata {samples}: {error}",
                            leaf.name
                        )
                    })?;
                    write_decision(
                        &mut output,
                        root_index,
                        state,
                        leaf,
                        depth,
                        samples,
                        previous_depth,
                        previous_action,
                        &decision,
                    )
                    .map_err(|error| format!("cannot write output: {error}"))?;
                    previous_depth = Some(depth);
                    previous_action = Some(decision.action);
                    records += 1;
                }
            }
        }
    }
    output
        .flush()
        .map_err(|error| format!("cannot flush output: {error}"))?;
    eprintln!(
        "analyze completed: {} roots x {} leaves x {} strata x {} depths = {} decision records",
        roots.len(),
        leaves.len(),
        strata.len(),
        depths.len(),
        records
    );
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("analyze failed: {error}");
        std::process::exit(2);
    }
}
