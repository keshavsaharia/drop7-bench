// Benchmark driver for the Rust engine.
//
//   bench moves    --games N --threads T     whole games, center policy:
//                                             pure engine throughput
//   bench search   --roots F --depth D --strata S --decisions N --repeats R
//                                             fixed-work decisions from a
//                                             harvested-roots file
//   bench scaling  --games N --max-threads T  game-level worker scaling sweep
//   bench parallel --roots F --depth D --strata S --decisions N --repeats R
//                  --threads T [--split-plies auto|N]
//                                             interleaved root-column versus
//                                             central-frontier decisions
//
// Timing discipline: best of R repeats, load average printed, peak resident
// memory read from /proc/self/status.  Seeds come from the Rust benchmark
// sub-block 0xa5277000-0xa5277fff of the already-opened SEEDLEASE-A52-FAST
// development lease.

use drop7_rs::board::{Board, BOARD_SIZE, MOVES_PER_LEVEL};
use drop7_rs::engine::{center_first_move, play_headless_move, MinimalWaveSink, State};
use drop7_rs::parallel::{
    choose_action_frontier_parallel, choose_action_root_parallel, ParallelConfig, ParallelDecision,
    DEFAULT_MAX_FRONTIER_TASKS, DEFAULT_MAX_HOST_BYTES,
};
use drop7_rs::search::{
    work_bound_for, DepthTable, FairLeaf, Leaf, NoTable, SearchParams, Searcher, TranspositionTable,
};

use std::env;
use std::fs;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Instant;

const BENCH_SEEDS: u32 = 0xa527_7000;

fn peak_resident_bytes() -> u64 {
    if let Ok(status) = fs::read_to_string("/proc/self/status") {
        for line in status.lines() {
            if let Some(rest) = line.strip_prefix("VmHWM:") {
                let kb: u64 = rest
                    .trim()
                    .trim_end_matches(" kB")
                    .trim()
                    .parse()
                    .unwrap_or(0);
                return kb * 1024;
            }
        }
    }
    0
}

fn load_average() -> f64 {
    fs::read_to_string("/proc/loadavg")
        .ok()
        .and_then(|s| s.split_whitespace().next().map(str::to_owned))
        .and_then(|s| s.parse().ok())
        .unwrap_or(-1.0)
}

/// Parses one `s <board49> <next> <mr> <level> <over>` record.  Returns None
/// on a truncated record or an out-of-range field: a benchmark must never
/// time a state the emitter never produced.
fn parse_state(tokens: &[&str]) -> Option<State> {
    let [board, next, moves_remaining, level, over] = tokens else {
        return None;
    };
    let board = Board::from_serialized(board)?;
    let next_disc: u8 = next.parse().ok()?;
    let moves_remaining: i32 = moves_remaining.parse().ok()?;
    let level: i32 = level.parse().ok()?;
    if !(1..=BOARD_SIZE as u8).contains(&next_disc)
        || !(0..=MOVES_PER_LEVEL).contains(&moves_remaining)
        || level < 1
        || (*over != "0" && *over != "1")
    {
        return None;
    }
    Some(State {
        board,
        next_disc,
        score: 0,
        level,
        moves_remaining,
        moves_played: 0,
        game_over: *over == "1",
    })
}

/// Reads every `s ` record from `path`.  Exits rather than returning on a
/// missing file, a malformed record, or an empty corpus -- an empty corpus
/// would otherwise divide a timing by zero and report an infinite ns/leaf
/// while exiting successfully.
fn read_roots(path: &str) -> Vec<State> {
    let text = match fs::read_to_string(path) {
        Ok(text) => text,
        Err(error) => {
            eprintln!("cannot read roots file {path}: {error}");
            std::process::exit(2);
        }
    };
    let mut roots = Vec::new();
    for (index, line) in text.lines().enumerate() {
        let Some(rest) = line.strip_prefix("s ") else {
            continue;
        };
        match parse_state(&rest.split_whitespace().collect::<Vec<_>>()) {
            Some(state) => roots.push(state),
            None => {
                eprintln!("{path}:{}: malformed root record", index + 1);
                std::process::exit(2);
            }
        }
    }
    if roots.is_empty() {
        eprintln!("no roots in {path}");
        std::process::exit(2);
    }
    roots
}

/// Play one center-policy game; returns (moves, score, waves).
fn play_center_game(seed: u32, max_moves: i32) -> (i32, i64, u64) {
    let mut state = State::initial_headless(seed);
    let mut sink = MinimalWaveSink::default();
    let mut moves = 0;
    let mut waves = 0u64;
    while !state.game_over && state.moves_played < max_moves {
        let Some(column) = center_first_move(&state.board) else {
            break;
        };
        if play_headless_move(&mut state, seed, column, &mut sink).is_none() {
            break;
        }
        moves += 1;
        waves += sink.count as u64;
        sink.count = 0;
        sink.last_depth = 0;
    }
    (moves, state.score, waves)
}

fn bench_moves(games: usize, threads: usize, max_moves: i32) {
    let start = Instant::now();
    let cursor = AtomicUsize::new(0);
    let total_moves = AtomicUsize::new(0);
    let total_waves = AtomicUsize::new(0);
    let total_score = std::sync::Mutex::new(0i64);
    std::thread::scope(|scope| {
        for _ in 0..threads {
            scope.spawn(|| loop {
                let game = cursor.fetch_add(1, Ordering::Relaxed);
                if game >= games {
                    break;
                }
                let seed = BENCH_SEEDS + game as u32;
                let (moves, score, waves) = play_center_game(seed, max_moves);
                total_moves.fetch_add(moves as usize, Ordering::Relaxed);
                total_waves.fetch_add(waves as usize, Ordering::Relaxed);
                *total_score.lock().unwrap() += score;
            });
        }
    });
    let seconds = start.elapsed().as_secs_f64();
    let moves = total_moves.load(Ordering::Relaxed);
    let waves = total_waves.load(Ordering::Relaxed);
    let score = *total_score.lock().unwrap();
    println!(
        "moves mode: {} games, {} moves, {} waves, {:.3} s, {:.0} moves/s, {:.1} ns/move, {:.2} waves/move, mean score {}",
        games,
        moves,
        waves,
        seconds,
        moves as f64 / seconds,
        seconds * 1e9 / moves.max(1) as f64,
        waves as f64 / moves.max(1) as f64,
        score / games.max(1) as i64,
    );
    println!(
        "threads {} peak-rss {} load {:.2}",
        threads,
        peak_resident_bytes(),
        load_average()
    );
}

fn bench_search<T: TranspositionTable>(
    roots: &[State],
    depth: i32,
    strata: i32,
    decisions: usize,
    repeats: usize,
    make_table: impl Fn() -> T,
    table_name: &str,
) {
    let params = SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(depth, strata) + 1,
        policy_seed: 0xd707_5eed,
    };
    let mut best_seconds = f64::INFINITY;
    let mut best_work = 0u64;
    let mut best_nodes = 0u64;
    let mut best_leaves = 0u64;
    let mut best_moves = 0u64;
    let mut best_hits = 0u64;
    let mut table_bytes = 0usize;
    for _ in 0..repeats {
        let mut searcher = Searcher::new(params, FairLeaf::default(), make_table());
        let start = Instant::now();
        let mut work = 0u64;
        let mut nodes = 0u64;
        let mut leaves = 0u64;
        let mut moves = 0u64;
        let mut hits = 0u64;
        for index in 0..decisions {
            let root = &roots[index % roots.len()];
            let (_action, metrics) = searcher.choose_action(root);
            work += metrics.work;
            nodes += metrics.nodes;
            leaves += metrics.leaf_calls;
            moves += metrics.move_calls;
            hits += metrics.cache_hits;
        }
        let seconds = start.elapsed().as_secs_f64();
        if seconds < best_seconds {
            best_seconds = seconds;
            best_work = work;
            best_nodes = nodes;
            best_leaves = leaves;
            best_moves = moves;
            best_hits = hits;
            table_bytes = searcher.table_bytes();
        }
    }
    println!(
        "search mode [{}]: depth {} strata {}, {} decisions on {} roots, best of {}",
        table_name,
        depth,
        strata,
        decisions,
        roots.len(),
        repeats
    );
    println!(
        "  {:.3} s, {:.2} ms/decision, {:.2} decisions/s, {} work/decision, {} nodes/decision, {:.2} ns/work",
        best_seconds,
        best_seconds * 1000.0 / decisions as f64,
        decisions as f64 / best_seconds,
        best_work / decisions.max(1) as u64,
        best_nodes / decisions.max(1) as u64,
        best_seconds * 1e9 / best_work.max(1) as f64,
    );
    println!(
        "  leaf calls/decision {}, move calls/decision {}, cache hits/decision {}, table {} bytes",
        best_leaves / decisions.max(1) as u64,
        best_moves / decisions.max(1) as u64,
        best_hits / decisions.max(1) as u64,
        table_bytes,
    );
    println!(
        "  peak-rss {} load {:.2}",
        peak_resident_bytes(),
        load_average()
    );
}

#[derive(Clone, Copy, Default)]
struct ParallelObservation {
    wall_seconds: f64,
    execution_seconds: f64,
    busy_core_seconds: f64,
    capacity_core_seconds: f64,
    work: u64,
    nodes: u64,
    tasks: u64,
    planner_tasks: u64,
    projected_table_bytes: usize,
    projected_plan_bytes: usize,
}

impl ParallelObservation {
    fn add(&mut self, decision: &ParallelDecision) {
        let metrics = &decision.metrics;
        self.wall_seconds += metrics.wall_seconds;
        self.execution_seconds += metrics.execution_seconds;
        let capacity = metrics.execution_seconds * metrics.worker_threads as f64;
        self.capacity_core_seconds += capacity;
        self.busy_core_seconds += metrics.worker_busy_fraction * capacity;
        self.work += metrics.work;
        self.nodes += metrics.nodes;
        self.tasks += metrics.completed_tasks as u64;
        self.planner_tasks += metrics.frontier_tasks as u64;
        self.projected_table_bytes = self
            .projected_table_bytes
            .max(metrics.projected_table_bytes);
        self.projected_plan_bytes = self.projected_plan_bytes.max(metrics.projected_plan_bytes);
    }

    fn busy_fraction(&self) -> f64 {
        if self.capacity_core_seconds == 0.0 {
            0.0
        } else {
            self.busy_core_seconds / self.capacity_core_seconds
        }
    }
}

fn same_parallel_decision(left: &ParallelDecision, right: &ParallelDecision) -> bool {
    left.action == right.action
        && left.column_values.len() == right.column_values.len()
        && left
            .column_values
            .iter()
            .zip(right.column_values.iter())
            .all(|((lc, lv), (rc, rv))| lc == rc && lv.to_bits() == rv.to_bits())
}

fn median(mut values: Vec<f64>) -> f64 {
    values.sort_by(f64::total_cmp);
    let middle = values.len() / 2;
    if values.len() % 2 == 0 {
        (values[middle - 1] + values[middle]) / 2.0
    } else {
        values[middle]
    }
}

fn print_parallel_observation(label: &str, repeat: usize, observation: ParallelObservation) {
    println!(
        "parallel repeat {} {}: wall {:.6} s execution {:.6} s busy {:.4} work {} nodes {} tasks {}/{} table-bytes {} plan-bytes {}",
        repeat + 1,
        label,
        observation.wall_seconds,
        observation.execution_seconds,
        observation.busy_fraction(),
        observation.work,
        observation.nodes,
        observation.tasks,
        observation.planner_tasks,
        observation.projected_table_bytes,
        observation.projected_plan_bytes,
    );
}

fn bench_parallel(
    roots: &[State],
    depth: i32,
    strata: i32,
    decisions: usize,
    repeats: usize,
    threads: usize,
    split_plies: Option<usize>,
    table_capacity: usize,
    max_frontier_tasks: usize,
    max_host_bytes: usize,
) {
    let params = SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(depth, strata) + 1,
        policy_seed: 0xd707_5eed,
    };
    let config = ParallelConfig {
        threads,
        table_capacity_per_worker: table_capacity,
        split_plies,
        max_frontier_tasks,
        max_host_bytes,
        ..ParallelConfig::default()
    };
    let mut root_observations = Vec::with_capacity(repeats);
    let mut frontier_observations = Vec::with_capacity(repeats);
    let mut compared = 0usize;
    for repeat in 0..repeats {
        let mut root_observation = ParallelObservation::default();
        let mut frontier_observation = ParallelObservation::default();
        for index in 0..decisions {
            let state = &roots[index % roots.len()];
            // Alternate arm order to reduce drift from heating or unrelated
            // load while keeping each root comparison adjacent.
            let (root, frontier) = if (repeat + index) % 2 == 0 {
                let root = choose_action_root_parallel(state, params, config)
                    .unwrap_or_else(|error| panic!("root scheduler failed: {error}"));
                let frontier = choose_action_frontier_parallel(state, params, config)
                    .unwrap_or_else(|error| panic!("frontier scheduler failed: {error}"));
                (root, frontier)
            } else {
                let frontier = choose_action_frontier_parallel(state, params, config)
                    .unwrap_or_else(|error| panic!("frontier scheduler failed: {error}"));
                let root = choose_action_root_parallel(state, params, config)
                    .unwrap_or_else(|error| panic!("root scheduler failed: {error}"));
                (root, frontier)
            };
            if !same_parallel_decision(&root, &frontier) {
                panic!(
                    "parallel parity mismatch at repeat {} root {}: root action {} frontier action {}",
                    repeat + 1,
                    index,
                    root.action,
                    frontier.action,
                );
            }
            if frontier.metrics.completed_tasks != frontier.metrics.frontier_tasks {
                panic!(
                    "frontier task mismatch at repeat {} root {}: completed {} registered {}",
                    repeat + 1,
                    index,
                    frontier.metrics.completed_tasks,
                    frontier.metrics.frontier_tasks,
                );
            }
            compared += 1;
            root_observation.add(&root);
            frontier_observation.add(&frontier);
        }
        print_parallel_observation("root", repeat, root_observation);
        print_parallel_observation("frontier", repeat, frontier_observation);
        root_observations.push(root_observation);
        frontier_observations.push(frontier_observation);
    }
    let root_median = median(
        root_observations
            .iter()
            .map(|row| row.wall_seconds)
            .collect(),
    );
    let frontier_median = median(
        frontier_observations
            .iter()
            .map(|row| row.wall_seconds)
            .collect(),
    );
    let frontier_busy = median(
        frontier_observations
            .iter()
            .map(ParallelObservation::busy_fraction)
            .collect(),
    );
    println!("parallel parity: {compared} paired decisions, 0 value/action/task mismatches");
    println!(
        "parallel summary: depth {} strata {} threads {} decisions/repeat {} repeats {} root-median {:.6} s frontier-median {:.6} s speedup {:.4} frontier-busy-median {:.4}",
        depth,
        strata,
        threads,
        decisions,
        repeats,
        root_median,
        frontier_median,
        root_median / frontier_median,
        frontier_busy,
    );
}

fn bench_scaling(games: usize, max_threads: usize, max_moves: i32) {
    println!(
        "scaling mode: {} games per worker count, center policy",
        games
    );
    let mut threads = 1usize;
    while threads <= max_threads {
        let start = Instant::now();
        let cursor = AtomicUsize::new(0);
        let total_moves = AtomicUsize::new(0);
        std::thread::scope(|scope| {
            for _ in 0..threads {
                scope.spawn(|| loop {
                    let game = cursor.fetch_add(1, Ordering::Relaxed);
                    if game >= games {
                        break;
                    }
                    let seed = BENCH_SEEDS + game as u32;
                    let (moves, _score, _waves) = play_center_game(seed, max_moves);
                    total_moves.fetch_add(moves as usize, Ordering::Relaxed);
                });
            }
        });
        let seconds = start.elapsed().as_secs_f64();
        let moves = total_moves.load(Ordering::Relaxed);
        println!(
            "  threads {:>3}: {:.3} s, {:.0} moves/s, {:.0} games/s",
            threads,
            seconds,
            moves as f64 / seconds,
            games as f64 / seconds,
        );
        threads *= 2;
    }
}

/// Microbenchmark: tight-loop leaf evaluation and search-context move
/// application, to price the two hot primitives without search overhead.
fn bench_micro(roots: &[State], repeats: usize) {
    // Leaf throughput over the real root states.
    let mut best = f64::INFINITY;
    for _ in 0..repeats {
        let mut leaf = FairLeaf::default();
        let start = Instant::now();
        let mut acc = 0.0f64;
        for state in roots {
            acc += leaf.value(state);
        }
        let seconds = start.elapsed().as_secs_f64();
        std::hint::black_box(acc);
        if seconds < best {
            best = seconds;
        }
    }
    println!(
        "micro leaf: {} evals, {:.1} ns/leaf",
        roots.len(),
        best * 1e9 / roots.len() as f64
    );

    // Move throughput on search-like states: apply every legal column of each
    // root with a deterministic draw stream, no recursion.
    let mut best = f64::INFINITY;
    let mut total_moves = 0u64;
    for _ in 0..repeats {
        let mut count = 0u64;
        let start = Instant::now();
        for state in roots {
            for &column in drop7_rs::search::COLUMN_ORDER.iter() {
                let mut random = drop7_rs::rng::Mulberry32::new(0x1234_5678);
                let mut sink = MinimalWaveSink::default();
                if let Some(result) =
                    drop7_rs::engine::play_move_sampled(state, column, &mut random, &mut sink)
                {
                    std::hint::black_box(result.score_delta);
                    count += 1;
                }
            }
        }
        let seconds = start.elapsed().as_secs_f64();
        if seconds < best {
            best = seconds;
            total_moves = count;
        }
    }
    println!(
        "micro move: {} moves, {:.1} ns/move",
        total_moves,
        best * 1e9 / total_moves.max(1) as f64
    );
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut mode = String::from("moves");
    let mut games = 512usize;
    let mut threads = 1usize;
    let mut max_threads = 32usize;
    let mut max_moves = 2000i32;
    let mut roots_path = String::new();
    let mut depth = 4i32;
    let mut strata = 7i32;
    let mut decisions = 60usize;
    let mut repeats = 3usize;
    let mut tt = String::from("none");
    let mut tt_capacity = 65_536usize;
    let mut split_plies = None;
    let mut max_frontier_tasks = DEFAULT_MAX_FRONTIER_TASKS;
    let mut max_host_bytes = DEFAULT_MAX_HOST_BYTES;
    let mut i = 1;
    if args.len() > 1 && !args[1].starts_with("--") {
        mode = args[1].clone();
        i = 2;
    }
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--games" => games = args[i + 1].parse().unwrap(),
            "--threads" => threads = args[i + 1].parse().unwrap(),
            "--max-threads" => max_threads = args[i + 1].parse().unwrap(),
            "--max-moves" => max_moves = args[i + 1].parse().unwrap(),
            "--roots" => roots_path = args[i + 1].clone(),
            "--depth" => depth = args[i + 1].parse().unwrap(),
            "--strata" => strata = args[i + 1].parse().unwrap(),
            "--decisions" => decisions = args[i + 1].parse().unwrap(),
            "--repeats" => repeats = args[i + 1].parse().unwrap(),
            "--tt" => tt = args[i + 1].clone(),
            "--tt-capacity" => tt_capacity = args[i + 1].parse().unwrap(),
            "--split-plies" => {
                split_plies = if args[i + 1] == "auto" {
                    None
                } else {
                    Some(
                        args[i + 1]
                            .parse()
                            .expect("--split-plies must be auto or an integer"),
                    )
                }
            }
            "--max-frontier-tasks" => max_frontier_tasks = args[i + 1].parse().unwrap(),
            "--max-host-bytes" => max_host_bytes = args[i + 1].parse().unwrap(),
            _ => {}
        }
        i += 2;
    }

    match mode.as_str() {
        "moves" => bench_moves(games, threads, max_moves),
        "scaling" => bench_scaling(games, max_threads, max_moves),
        "micro" => {
            let roots = read_roots(&roots_path);
            bench_micro(&roots, repeats);
        }
        "search" => {
            let roots = read_roots(&roots_path);
            // Memoization arms: "none" (NoTable) or "gate<K>" (DepthTable
            // caching nodes at depth >= K).  Both compute identical values;
            // only node counts and per-node cost differ.
            if tt == "none" {
                bench_search(
                    &roots,
                    depth,
                    strata,
                    decisions,
                    repeats,
                    || NoTable,
                    "no-table",
                );
            } else if let Some(gate) = tt.strip_prefix("gate") {
                let from_depth: i32 = gate.parse().unwrap();
                let name = format!("depth-gate>={from_depth} cap={tt_capacity}");
                bench_search(
                    &roots,
                    depth,
                    strata,
                    decisions,
                    repeats,
                    || DepthTable::new(tt_capacity, from_depth),
                    &name,
                );
            } else {
                eprintln!("unknown --tt {tt} (want none or gate<K>)");
                std::process::exit(2);
            }
        }
        "parallel" => {
            let roots = read_roots(&roots_path);
            bench_parallel(
                &roots,
                depth,
                strata,
                decisions,
                repeats,
                threads,
                split_plies,
                tt_capacity,
                max_frontier_tasks,
                max_host_bytes,
            );
        }
        _ => {
            eprintln!("unknown mode {mode}");
            std::process::exit(2);
        }
    }
}
