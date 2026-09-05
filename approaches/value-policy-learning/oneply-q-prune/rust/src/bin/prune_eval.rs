// Decide every selected panel root under several search configurations.
//
//   prune_eval --panel panel.ndjson [--game-min A] [--game-max B]
//              [--every K] [--limit N] [--threads T] --out FILE
//              --config SPEC [--config SPEC ...]
//
// SPEC is
//   exact:D:S                 full-width drop7-rs Searcher (evaluate_root_column
//                             per legal column, fresh table per root)
//   pruned:D:S:W:PRIOR        PrunedSearcher; W = widths for plies remaining
//                             D-1..2 (e.g. "3,3" at depth 4), PRIOR = center |
//                             kf=FILE | d1 | lq=FILE
//   leaf:D:S:FILE             full-width drop7-rs Searcher with the LinearLeaf
//                             weights in FILE (analysis/fit_leaf.py output)
// One NDJSON line per root: game, move, and per config the canonical-frame
// action, the legal columns with their root values, and the logical work.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;
use std::time::Instant;

use drop7_oneply_q::leaf::{LinearLeaf, LinearLeafWeights};
use drop7_oneply_q::prior::Prior;
use drop7_oneply_q::prune::{PrunedSearcher, Widths};
use drop7_rs::board::{Board, EMPTY};
use drop7_rs::engine::State;
use drop7_rs::search::{
    canonical_state, work_bound_for, DepthTable, FairLeaf, Leaf, SearchParams, Searcher,
    TranspositionTable, COLUMN_ORDER,
};

struct Root {
    game: i64,
    move_index: i64,
    state: State,
}

fn json_int(line: &str, key: &str) -> i64 {
    let pattern = format!("\"{key}\":");
    let start = line.find(&pattern).unwrap_or_else(|| panic!("missing {key}")) + pattern.len();
    let rest = &line[start..];
    let end = rest
        .find(|c: char| !(c.is_ascii_digit() || c == '-'))
        .unwrap_or(rest.len());
    rest[..end].parse().unwrap_or_else(|_| panic!("bad integer for {key}"))
}

fn json_str(line: &str, key: &str) -> String {
    let pattern = format!("\"{key}\":\"");
    let start = line.find(&pattern).unwrap_or_else(|| panic!("missing {key}")) + pattern.len();
    let rest = &line[start..];
    let end = rest.find('"').expect("closing quote");
    rest[..end].to_string()
}

fn parse_root(line: &str) -> Root {
    let board = Board::from_serialized(&json_str(line, "board")).expect("board");
    let state = State {
        board,
        next_disc: json_int(line, "next") as u8,
        score: 0,
        level: 1,
        moves_remaining: json_int(line, "movesRemaining") as i32,
        moves_played: 0,
        game_over: false,
    };
    Root {
        game: json_int(line, "game"),
        move_index: json_int(line, "move"),
        state,
    }
}

enum Config {
    Exact { depth: i32, strata: i32 },
    Pruned { depth: i32, strata: i32, widths: Widths, prior: Prior },
    Leaf { depth: i32, strata: i32, weights: LinearLeafWeights },
}

fn parse_config(spec: &str) -> Config {
    if let Some(rest) = spec.strip_prefix("exact:") {
        let parts: Vec<&str> = rest.split(':').collect();
        assert_eq!(parts.len(), 2, "exact:D:S");
        return Config::Exact {
            depth: parts[0].parse().expect("depth"),
            strata: parts[1].parse().expect("strata"),
        };
    }
    if let Some(rest) = spec.strip_prefix("pruned:") {
        let parts: Vec<&str> = rest.splitn(4, ':').collect();
        assert_eq!(parts.len(), 4, "pruned:D:S:W:PRIOR");
        let depth: i32 = parts[0].parse().expect("depth");
        return Config::Pruned {
            depth,
            strata: parts[1].parse().expect("strata"),
            widths: Widths::parse(parts[2], depth).unwrap_or_else(|e| panic!("{e}")),
            prior: Prior::parse(parts[3]).unwrap_or_else(|e| panic!("{e}")),
        };
    }
    if let Some(rest) = spec.strip_prefix("leaf:") {
        let parts: Vec<&str> = rest.splitn(3, ':').collect();
        assert_eq!(parts.len(), 3, "leaf:D:S:FILE");
        return Config::Leaf {
            depth: parts[0].parse().expect("depth"),
            strata: parts[1].parse().expect("strata"),
            weights: LinearLeafWeights::load(parts[2]).unwrap_or_else(|e| panic!("{e}")),
        };
    }
    panic!("unknown config {spec}");
}

fn exact_params(depth: i32, strata: i32) -> SearchParams {
    SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(depth, strata) + 1,
        policy_seed: 0xd707_5eed,
    }
}

enum Runner {
    Exact { depth: i32, searcher: Searcher<FairLeaf, DepthTable> },
    Pruned { depth: i32, searcher: PrunedSearcher<DepthTable> },
    Leaf { depth: i32, searcher: Searcher<LinearLeaf, DepthTable> },
}

fn make_runner(config: &Config, table: usize) -> Runner {
    match config {
        Config::Exact { depth, strata } => Runner::Exact {
            depth: *depth,
            searcher: Searcher::new(exact_params(*depth, *strata), FairLeaf::default(), DepthTable::new(table, 1)),
        },
        Config::Leaf { depth, strata, weights } => Runner::Leaf {
            depth: *depth,
            searcher: Searcher::new(
                exact_params(*depth, *strata),
                LinearLeaf::new(weights.clone()),
                DepthTable::new(table, 1),
            ),
        },
        Config::Pruned { depth, strata, widths, prior } => Runner::Pruned {
            depth: *depth,
            searcher: PrunedSearcher::deployment(*depth, *strata, *widths, prior.clone(), table),
        },
    }
}

struct Outcome {
    action: i32,
    columns: Vec<usize>,
    values: Vec<f64>,
    work: u64,
    prior_work: u64,
    pruned_nodes: u64,
    wall_ms: f64,
}

fn full_width<L: Leaf, T: TranspositionTable>(
    searcher: &mut Searcher<L, T>,
    canonical: &State,
    depth: i32,
    started: Instant,
) -> Outcome {
    searcher.begin_parallel_decision();
    let mut columns = Vec::new();
    let mut values = Vec::new();
    let mut work = 0u64;
    let mut action = -1i32;
    let mut best = f64::NEG_INFINITY;
    for &column in COLUMN_ORDER.iter() {
        if canonical.board.get(0, column) != EMPTY {
            continue;
        }
        let value = searcher
            .evaluate_root_column(canonical, column, depth)
            .expect("completion budget");
        work += searcher.last_metrics().work;
        columns.push(column);
        values.push(value);
        if value > best {
            best = value;
            action = column as i32;
        }
    }
    Outcome {
        action,
        columns,
        values,
        work,
        prior_work: 0,
        pruned_nodes: 0,
        wall_ms: started.elapsed().as_secs_f64() * 1000.0,
    }
}

fn run_one(runner: &mut Runner, state: &State) -> Outcome {
    let started = Instant::now();
    let (canonical, mirrored) = canonical_state(state);
    assert!(!mirrored, "panel roots are stored in the canonical frame");
    match runner {
        Runner::Exact { depth, searcher } => full_width(searcher, &canonical, *depth, started),
        Runner::Leaf { depth, searcher } => full_width(searcher, &canonical, *depth, started),
        Runner::Pruned { depth, searcher } => {
            let d = searcher.decide(&canonical, *depth);
            Outcome {
                action: d.action,
                columns: d.columns,
                values: d.values,
                work: d.work,
                prior_work: d.prior_work,
                pruned_nodes: d.pruned_nodes,
                wall_ms: started.elapsed().as_secs_f64() * 1000.0,
            }
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut panel = String::new();
    let mut game_min = i64::MIN;
    let mut game_max = i64::MAX;
    let mut every = 1usize;
    let mut limit = usize::MAX;
    let mut threads = 8usize;
    let mut table = 65_536usize;
    let mut out = String::new();
    let mut specs: Vec<String> = Vec::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--panel" => panel = v.to_string(),
            "--game-min" => game_min = v.parse().expect("game-min"),
            "--game-max" => game_max = v.parse().expect("game-max"),
            "--every" => every = v.parse().expect("every"),
            "--limit" => limit = v.parse().expect("limit"),
            "--threads" => threads = v.parse().expect("threads"),
            "--table" => table = v.parse().expect("table"),
            "--out" => out = v.to_string(),
            "--config" => specs.push(v.to_string()),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}: every option takes a value", args[i]);
    }
    assert!(!panel.is_empty() && !out.is_empty() && !specs.is_empty(), "--panel, --out and --config required");
    let configs: Vec<Config> = specs.iter().map(|s| parse_config(s)).collect();
    let text = std::fs::read_to_string(&panel).expect("read panel");
    let roots: Vec<Root> = text
        .lines()
        .filter(|l| !l.trim().is_empty())
        .map(parse_root)
        .filter(|r| r.game >= game_min && r.game <= game_max)
        .enumerate()
        .filter(|(index, _)| index % every.max(1) == 0)
        .map(|(_, r)| r)
        .take(limit)
        .collect();
    eprintln!(
        "prune_eval: {} roots (games {}..={}, every {every}, limit {limit}), {} configs, threads {threads}",
        roots.len(),
        game_min,
        game_max,
        configs.len()
    );
    let started = Instant::now();
    let cursor = AtomicUsize::new(0);
    let done = AtomicUsize::new(0);
    let slots: Vec<Mutex<Option<String>>> = roots.iter().map(|_| Mutex::new(None)).collect();
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| {
                let mut runners: Vec<Runner> = configs.iter().map(|c| make_runner(c, table)).collect();
                loop {
                    let index = cursor.fetch_add(1, Ordering::Relaxed);
                    if index >= roots.len() {
                        break;
                    }
                    let root = &roots[index];
                    let mut results: Vec<String> = Vec::with_capacity(runners.len());
                    for (runner, spec) in runners.iter_mut().zip(specs.iter()) {
                        let o = run_one(runner, &root.state);
                        results.push(format!(
                            "{{\"config\":\"{spec}\",\"action\":{},\"columns\":[{}],\"values\":[{}],\"work\":{},\"priorWork\":{},\"prunedNodes\":{},\"wallMs\":{:.3}}}",
                            o.action,
                            o.columns.iter().map(|c| c.to_string()).collect::<Vec<_>>().join(","),
                            o.values.iter().map(|v| format!("{v:?}")).collect::<Vec<_>>().join(","),
                            o.work,
                            o.prior_work,
                            o.pruned_nodes,
                            o.wall_ms
                        ));
                    }
                    let line = format!(
                        "{{\"game\":{},\"move\":{},\"results\":[{}]}}",
                        root.game,
                        root.move_index,
                        results.join(",")
                    );
                    *slots[index].lock().unwrap() = Some(line);
                    let finished = done.fetch_add(1, Ordering::Relaxed) + 1;
                    if finished % 100 == 0 || finished == roots.len() {
                        eprintln!("prune_eval: {finished}/{} roots, {:.1}s", roots.len(), started.elapsed().as_secs_f64());
                    }
                }
            });
        }
    });
    let lines: Vec<String> = slots
        .into_iter()
        .map(|s| s.into_inner().unwrap().expect("every root decided"))
        .collect();
    std::fs::write(&out, lines.join("\n") + "\n").expect("write out");
    eprintln!("prune_eval: wrote {} lines to {out} in {:.1}s", lines.len(), started.elapsed().as_secs_f64());
}
