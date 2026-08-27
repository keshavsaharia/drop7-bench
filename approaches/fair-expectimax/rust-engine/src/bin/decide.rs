// One-shot decision binary for the benchmark playground and the competition,
// the Rust counterpart of approaches/lifetime-objective/leaf-evolution's
// decide.cpp:
//
//   decide --board <49 digits> --next <1-7> --rise <1-5>
//          [--depth 7] [--chance-samples 7]
//          [--cache TOTAL_ENTRIES] [--threads T]
//          [--scheduler frontier|root] [--split-plies auto|N]
//          [--max-frontier-tasks N] [--max-host-bytes N]
//
// prints "bestmove <column>" (0-6) or "bestmove none" on a terminal board, and
// exits 0.  The board is the engine's serializeBoard encoding, row-major from
// the top: 0 empty, 1-7 numbered, 8 solid gray, 9 cracked gray -- the same
// string the D7P protocol carries (docs/d7p-protocol.md).
//
// The policy reads exactly the public state: visible board, visible next disc,
// moves until the next rise.  There is no seed, score, level or move number on
// the command line, so there is nothing else it could read.  Decisions are
// deterministic for a given board and configuration, which the benchmark
// harness requires.
//
// CENTRAL-FRONTIER PARALLELISM IS VALUE-IDENTICAL, NOT AN APPROXIMATION.  A
// coordinator expands a deterministic prefix of the expectimax tree, workers
// claim the remaining public-state continuations from a central registry, and
// the coordinator reduces results in the original column/sample order.  This
// exposes thousands of tasks instead of at most seven root columns.  Private
// worker tables can change cache hits and logical work, but cached values are
// bit-identical to recomputation, so threads change no root value or action.
// `--scheduler root` retains the earlier coarse-grained implementation as a
// measured fallback. `--cache` remains one aggregate entry budget, partitioned
// across private worker tables so increasing the thread count cannot multiply
// the caller's memory request.

use drop7_rs::board::{Board, BOARD_SIZE};
use drop7_rs::engine::State;
use drop7_rs::parallel::{
    choose_action_frontier_parallel, choose_action_root_parallel, ParallelConfig,
    DEFAULT_MAX_FRONTIER_TASKS, DEFAULT_MAX_HOST_BYTES,
};
use drop7_rs::search::{work_bound_for, SearchParams};

use std::env;

fn main() {
    let mut board_text = String::new();
    let mut next = 0u8;
    let mut rise = 0i32;
    let mut depth = 7i32;
    let mut strata = 7i32;
    let mut cache = 1_048_576usize;
    let mut threads = 0usize; // 0 = all CPUs visible to this process
    let mut scheduler = String::from("frontier");
    let mut split_plies = None;
    let mut max_frontier_tasks = DEFAULT_MAX_FRONTIER_TASKS;
    let mut max_host_bytes = DEFAULT_MAX_HOST_BYTES;
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i < args.len() {
        if args[i] == "--help" || args[i] == "-h" {
            eprintln!("usage: decide --board BOARD --next 1-7 --rise 1-5 [--depth N] [--chance-samples N] [--cache N] [--threads N] [--scheduler frontier|root] [--split-plies auto|N] [--max-frontier-tasks N] [--max-host-bytes N]");
            return;
        }
        if i + 1 >= args.len() {
            eprintln!("decide failed: {} needs a value", args[i]);
            std::process::exit(2);
        }
        match args[i].as_str() {
            "--board" => board_text = args[i + 1].clone(),
            "--next" => next = args[i + 1].parse().expect("--next must be 1-7"),
            "--rise" => rise = args[i + 1].parse().expect("--rise must be 1-5"),
            "--depth" => depth = args[i + 1].parse().expect("--depth"),
            "--chance-samples" => strata = args[i + 1].parse().expect("--chance-samples"),
            "--cache" => cache = args[i + 1].parse().expect("--cache"),
            "--threads" => threads = args[i + 1].parse().expect("--threads"),
            "--scheduler" => scheduler = args[i + 1].clone(),
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
            "--max-frontier-tasks" => {
                max_frontier_tasks = args[i + 1].parse().expect("--max-frontier-tasks")
            }
            "--max-host-bytes" => max_host_bytes = args[i + 1].parse().expect("--max-host-bytes"),
            other => {
                eprintln!("decide failed: unknown option {other}");
                std::process::exit(2);
            }
        }
        i += 2;
    }
    if depth < 1 || strata < 1 {
        eprintln!("decide failed: depth and chance samples must be at least 1");
        std::process::exit(2);
    }
    let Some(board) = Board::from_serialized(&board_text) else {
        eprintln!("decide failed: --board must be 49 characters of digits 0-9");
        std::process::exit(2);
    };
    if !(1..=BOARD_SIZE as u8).contains(&next) {
        eprintln!("decide failed: --next must be 1-7");
        std::process::exit(2);
    }
    if cache == 0 {
        eprintln!("decide failed: --cache must be at least 1");
        std::process::exit(2);
    }

    // The public state only: no score, level or move number exists here, and
    // the search and leaf provably read none of them.
    let state = State {
        board,
        next_disc: next,
        score: 0,
        level: 1,
        moves_remaining: rise,
        moves_played: 0,
        game_over: false,
    };

    let params = SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(depth, strata) + 1,
        policy_seed: 0xd707_5eed,
    };

    let requested_threads = if threads == 0 {
        std::thread::available_parallelism().map_or(1, |n| n.get())
    } else {
        threads
    };
    // DepthTable rounds to powers of two. Partition the aggregate cache budget
    // before constructing ParallelConfig so all private worker tables together
    // remain at or below the caller's requested entry count.
    let worker_count = requested_threads.min(cache);
    let worker_cache = cache_entries_per_worker(cache, worker_count);
    let config = ParallelConfig {
        threads: worker_count,
        table_capacity_per_worker: worker_cache,
        split_plies,
        max_frontier_tasks,
        max_host_bytes,
        ..ParallelConfig::default()
    };
    let decision = match scheduler.as_str() {
        "frontier" => choose_action_frontier_parallel(&state, params, config),
        "root" => choose_action_root_parallel(&state, params, config),
        _ => {
            eprintln!("decide failed: --scheduler must be frontier or root");
            std::process::exit(2);
        }
    };
    let decision = match decision {
        Ok(decision) => decision,
        Err(error) if error.contains("no legal columns") => {
            println!("bestmove none");
            return;
        }
        Err(error) => {
            eprintln!("decide failed: {error}");
            std::process::exit(2);
        }
    };
    println!("bestmove {}", decision.action);
    let metrics = decision.metrics;
    let allocated_cache_entries = worker_cache * metrics.worker_threads;
    println!(
        "info depth {} scheduler {} threads {} tasks {} split {} work {} nodes {} busy {:.4} wall {:.6} cache-entries {} table-bytes {} frozen 1",
        depth,
        scheduler,
        metrics.worker_threads,
        metrics.frontier_tasks,
        metrics.split_plies,
        metrics.work,
        metrics.nodes,
        metrics.worker_busy_fraction,
        metrics.wall_seconds,
        allocated_cache_entries,
        metrics.projected_table_bytes,
    );
}

/// Largest power-of-two per-worker table that keeps the aggregate allocation
/// within `total_entries`. The caller guarantees both values are positive and
/// caps `worker_count` at `total_entries`.
fn cache_entries_per_worker(total_entries: usize, worker_count: usize) -> usize {
    debug_assert!(total_entries > 0);
    debug_assert!(worker_count > 0);
    debug_assert!(worker_count <= total_entries);
    let share = total_entries / worker_count;
    1usize << (usize::BITS - 1 - share.leading_zeros())
}

#[cfg(test)]
mod tests {
    use super::cache_entries_per_worker;

    #[test]
    fn cache_budget_is_shared_across_frontier_workers() {
        let total = 16_777_216usize;
        let workers = 192usize;
        let per_worker = cache_entries_per_worker(total, workers);

        assert_eq!(per_worker, 65_536);
        assert!(per_worker.is_power_of_two());
        assert!(per_worker * workers <= total);
    }

    #[test]
    fn cache_partition_never_exceeds_the_total_budget() {
        for total in 1..=257usize {
            for workers in 1..=total.min(257) {
                let per_worker = cache_entries_per_worker(total, workers);
                assert!(per_worker.is_power_of_two());
                assert!(per_worker * workers <= total);
            }
        }
    }
}
