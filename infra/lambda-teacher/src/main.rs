// AWS Lambda pilot handler for one search decision at a configurable depth.
//
// This is the Rust counterpart of approaches/fair-expectimax/rust-engine's
// decide.rs, repackaged as a Lambda function instead of a CLI binary, to
// test whether horizontally scaling teacher-quality search decisions across
// many short-lived Lambda invocations is viable. It is a VIABILITY PILOT,
// not a production data-generation pipeline: no retry/backoff, no batching,
// no S3 output, one decision per invocation. See infra/lambda-teacher/README.md
// for the deployment story and AGENTS.md's public-information rules, which
// this function also follows: the event carries only the public state
// (board, next disc, moves until rise, depth, chance strata), never a seed,
// score, or hidden value.
//
// Input event (JSON):
//   {"board": "<49 digits>", "next": 1-7, "rise": 1-5, "depth": N,
//    "chance_samples": 7 (optional), "threads": 1 (optional)}
// Output (JSON):
//   {"bestmove": 0-6 or -1, "work": u64, "nodes": u64,
//    "search_wall_seconds": f64, "handler_wall_seconds": f64,
//    "depth": N, "chance_samples": N, "worker_threads": usize}
//
// The board is the engine's serializeBoard encoding used throughout this
// repository and by the D7P protocol: row-major from the top, one digit per
// cell (0 empty, 1-7 numbered, 8 solid gray, 9 cracked gray).

use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::parallel::{
    choose_action_root_parallel, ParallelConfig, TableScope, DEFAULT_MAX_FRONTIER_TASKS,
    DEFAULT_MAX_HOST_BYTES,
};
use drop7_rs::search::{work_bound_for, SearchParams};
use lambda_runtime::{run, service_fn, Error, LambdaEvent};
use serde::{Deserialize, Serialize};
use std::time::Instant;

#[derive(Deserialize)]
struct DecisionRequest {
    board: String,
    next: u8,
    rise: i32,
    depth: i32,
    #[serde(default = "default_strata")]
    chance_samples: i32,
    /// Single-threaded by default: this is what one Lambda invocation's
    /// allocated vCPU share realistically is, and what the workstation
    /// timing measurements this pilot is checked against used.
    #[serde(default = "default_threads")]
    threads: usize,
    #[serde(default = "default_cache")]
    cache: usize,
}

fn default_strata() -> i32 {
    7
}
fn default_threads() -> usize {
    1
}
fn default_cache() -> usize {
    1_048_576
}

#[derive(Serialize)]
struct DecisionResponse {
    bestmove: i32,
    work: u64,
    nodes: u64,
    search_wall_seconds: f64,
    handler_wall_seconds: f64,
    depth: i32,
    chance_samples: i32,
    worker_threads: usize,
}

/// Largest power-of-two per-worker table that keeps the aggregate allocation
/// within `total_entries`. Copied verbatim from decide.rs so a Lambda
/// invocation partitions its cache budget the same way the reference CLI
/// does; a divergent implementation here would be a second, unverified
/// cache-sizing rule for the same aggregate-entries contract.
fn cache_entries_per_worker(total_entries: usize, worker_count: usize) -> usize {
    let share = total_entries / worker_count.max(1);
    1usize << (usize::BITS - 1 - share.max(1).leading_zeros())
}

fn decide(req: DecisionRequest) -> Result<DecisionResponse, String> {
    let board = Board::from_serialized(&req.board)
        .ok_or_else(|| format!("board must be 49 characters of digits 0-9, got {:?}", req.board))?;
    if !(1..=7).contains(&req.next) {
        return Err("next must be 1-7".into());
    }
    if !(1..=5).contains(&req.rise) {
        return Err("rise must be 1-5".into());
    }
    if req.depth < 1 || req.chance_samples < 1 {
        return Err("depth and chance_samples must be >= 1".into());
    }
    if req.cache == 0 {
        return Err("cache must be >= 1".into());
    }
    let state = State {
        board,
        next_disc: req.next,
        score: 0,
        level: 1,
        moves_remaining: req.rise,
        moves_played: 0,
        game_over: false,
    };
    let params = SearchParams {
        depth: req.depth,
        chance_samples: req.chance_samples,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(req.depth, req.chance_samples) + 1,
        policy_seed: 0xd707_5eed,
    };
    let workers = req.threads.max(1).min(req.cache);
    let config = ParallelConfig {
        threads: workers,
        table_capacity_per_worker: cache_entries_per_worker(req.cache, workers),
        table_scope: TableScope::Private,
        table_from_depth: 1,
        split_plies: None,
        max_frontier_tasks: DEFAULT_MAX_FRONTIER_TASKS,
        max_host_bytes: DEFAULT_MAX_HOST_BYTES,
    };
    let handler_started = Instant::now();
    let decision = match choose_action_root_parallel(&state, params, config) {
        Ok(decision) => decision,
        Err(error) if error.contains("no legal columns") => {
            return Ok(DecisionResponse {
                bestmove: -1,
                work: 0,
                nodes: 0,
                search_wall_seconds: 0.0,
                handler_wall_seconds: handler_started.elapsed().as_secs_f64(),
                depth: req.depth,
                chance_samples: req.chance_samples,
                worker_threads: 0,
            });
        }
        Err(error) => return Err(error),
    };
    let metrics = decision.metrics;
    Ok(DecisionResponse {
        bestmove: decision.action,
        work: metrics.work,
        nodes: metrics.nodes,
        search_wall_seconds: metrics.wall_seconds,
        handler_wall_seconds: handler_started.elapsed().as_secs_f64(),
        depth: req.depth,
        chance_samples: req.chance_samples,
        worker_threads: metrics.worker_threads,
    })
}

async fn handler(event: LambdaEvent<DecisionRequest>) -> Result<DecisionResponse, Error> {
    decide(event.payload).map_err(Error::from)
}

#[tokio::main]
async fn main() -> Result<(), Error> {
    run(service_fn(handler)).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decides_a_legal_column_on_the_empty_board_at_depth_4() {
        let board49 = "0".repeat(49);
        let req = DecisionRequest {
            board: board49,
            next: 4,
            rise: 5,
            depth: 4,
            chance_samples: 7,
            threads: 1,
            cache: 1_048_576,
        };
        let resp = decide(req).expect("legal decision");
        assert!((0..7).contains(&resp.bestmove));
        assert!(resp.work > 0);
        assert!(resp.search_wall_seconds >= 0.0);
    }

    #[test]
    fn rejects_a_malformed_board() {
        let req = DecisionRequest {
            board: "not a board".into(),
            next: 4,
            rise: 5,
            depth: 3,
            chance_samples: 7,
            threads: 1,
            cache: 1_048_576,
        };
        assert!(decide(req).is_err());
    }
}
