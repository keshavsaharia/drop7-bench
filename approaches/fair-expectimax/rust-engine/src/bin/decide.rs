// One-shot decision binary for the benchmark playground and the competition,
// the Rust counterpart of approaches/lifetime-objective/leaf-evolution's
// decide.cpp:
//
//   decide --board <49 digits> --next <1-7> --rise <1-5>
//          [--depth 7] [--chance-samples 7]
//          [--cache TOTAL_ENTRIES] [--threads T]
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
// ROOT-PARALLELISM IS VALUE-IDENTICAL, NOT AN APPROXIMATION.  The work budget
// is work_bound_for(depth, strata) + 1, the exact worst-case work of the full
// iterative-deepening run, so the sequential choose_action always completes
// the target depth and its action is exactly the argmax of the per-column
// values at that depth (ties broken by COLUMN_ORDER).  Those values are a
// deterministic function of the public state -- the transposition table is
// proven cache-independent -- so evaluating disjoint columns on separate
// threads, each with its own searcher and table, computes bit-identical
// values and the same argmax.  Threads change only wall-clock and how the
// total cache-entry budget is partitioned; they never multiply that budget.

use drop7_rs::board::{Board, BOARD_SIZE, EMPTY};
use drop7_rs::engine::State;
use drop7_rs::search::{
    canonical_state, work_bound_for, DepthTable, FairLeaf, SearchMetrics, SearchParams, Searcher,
    COLUMN_ORDER,
};

use std::env;

fn main() {
    let mut board_text = String::new();
    let mut next = 0u8;
    let mut rise = 0i32;
    let mut depth = 7i32;
    let mut strata = 7i32;
    let mut cache = 1_048_576usize;
    let mut threads = 0usize; // 0 = one per legal column, capped by cores
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--board" => board_text = args[i + 1].clone(),
            "--next" => next = args[i + 1].parse().expect("--next must be 1-7"),
            "--rise" => rise = args[i + 1].parse().expect("--rise must be 1-5"),
            "--depth" => depth = args[i + 1].parse().expect("--depth"),
            "--chance-samples" => strata = args[i + 1].parse().expect("--chance-samples"),
            "--cache" => cache = args[i + 1].parse().expect("--cache"),
            "--threads" => threads = args[i + 1].parse().expect("--threads"),
            other => {
                eprintln!("decide failed: unknown option {other}");
                std::process::exit(2);
            }
        }
        i += 2;
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

    let (canonical, mirrored) = canonical_state(&state);
    let legal: Vec<usize> = COLUMN_ORDER
        .iter()
        .copied()
        .filter(|&column| canonical.board.get(0, column) == EMPTY)
        .collect();
    if legal.is_empty() {
        println!("bestmove none");
        return;
    }

    let requested_workers = if threads == 0 {
        legal
            .len()
            .min(std::thread::available_parallelism().map_or(1, |n| n.get()))
    } else {
        threads.min(legal.len()).max(1)
    };
    // `cache` is one aggregate entry budget, not a per-worker allocation.
    // DepthTable needs a power-of-two capacity, so round each worker's equal
    // share down.  Capping workers by the entry budget keeps the invariant
    // even for deliberately tiny command-line values.
    let worker_count = requested_workers.min(cache);
    let worker_cache = cache_entries_per_worker(cache, worker_count);

    // Evaluate every legal column at the full target depth, disjoint columns
    // per worker.  Each worker owns its searcher and its bounded slice of the
    // total table budget; the values it returns are the same bits the
    // sequential search would compute.
    let mut values: Vec<(usize, f64, SearchMetrics)> = Vec::new();
    std::thread::scope(|scope| {
        let mut lanes: Vec<Vec<usize>> = (0..worker_count).map(|_| Vec::new()).collect();
        for (index, &column) in legal.iter().enumerate() {
            lanes[index % worker_count].push(column);
        }
        let handles: Vec<_> = lanes
            .into_iter()
            .map(|lane| {
                let canonical = &canonical;
                scope.spawn(move || {
                    let mut searcher = Searcher::new(
                        params,
                        FairLeaf::default(),
                        DepthTable::new(worker_cache, 1),
                    );
                    let mut out = Vec::new();
                    for column in lane {
                        // A single column's evaluation is bounded by one
                        // seventh of a root decision, far under the per-search
                        // budget, so the work limit can never fire here.
                        let value = searcher
                            .evaluate_root_column(canonical, column, depth)
                            .expect("the completion-guaranteeing budget covers one column");
                        out.push((column, value, searcher.last_metrics().clone()));
                    }
                    out
                })
            })
            .collect();
        for handle in handles {
            values.extend(handle.join().expect("worker panicked"));
        }
    });

    // Argmax in COLUMN_ORDER with strict improvement: the same tie-break the
    // sequential root_decision applies.
    let mut action = -1i32;
    let mut best_value = f64::NEG_INFINITY;
    let mut total_work = 0u64;
    let mut total_nodes = 0u64;
    for &column in COLUMN_ORDER.iter() {
        if let Some(&(_, value, ref metrics)) =
            values.iter().find(|&&(column_, _, _)| column_ == column)
        {
            total_work += metrics.work;
            total_nodes += metrics.nodes;
            if value > best_value {
                best_value = value;
                action = column as i32;
            }
        }
    }
    if mirrored && action >= 0 {
        action = BOARD_SIZE as i32 - 1 - action;
    }

    if action < 0 {
        println!("bestmove none");
    } else {
        println!("bestmove {action}");
    }
    let allocated_cache_entries = worker_cache * worker_count;
    println!(
        "info depth {depth} work {total_work} nodes {total_nodes} cache_entries {allocated_cache_entries} frozen 1"
    );
}

/// Largest power-of-two per-worker table that keeps the aggregate allocation
/// within `total_entries`.  The caller guarantees both values are positive
/// and caps `worker_count` at `total_entries`.
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
    fn cache_budget_is_shared_across_root_workers() {
        let total = 16_777_216usize;
        let workers = 7usize;
        let per_worker = cache_entries_per_worker(total, workers);

        assert_eq!(per_worker, 2_097_152);
        assert!(per_worker.is_power_of_two());
        assert!(per_worker * workers <= total);
    }

    #[test]
    fn cache_partition_never_exceeds_the_total_budget() {
        for total in 1..=257usize {
            for workers in 1..=total.min(7) {
                let per_worker = cache_entries_per_worker(total, workers);
                assert!(per_worker.is_power_of_two());
                assert!(per_worker * workers <= total);
            }
        }
    }
}
