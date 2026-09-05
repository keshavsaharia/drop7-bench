//! Allocation-free resource plan for one central-frontier search decision.

use drop7_rs::parallel::{
    plan_parallel_resources, ParallelConfig, TableScope, DEFAULT_MAX_FRONTIER_TASKS,
    DEFAULT_MAX_HOST_BYTES,
};
use drop7_rs::search::{work_bound_for, SearchParams};
use std::env;

fn main() {
    let mut depth = 7i32;
    let mut strata = 7i32;
    let mut threads = 192usize;
    let mut cache = 262_144usize;
    let mut table_scope = TableScope::Private;
    let mut table_from_depth = 1i32;
    let mut split_plies = None;
    let mut max_frontier_tasks = DEFAULT_MAX_FRONTIER_TASKS;
    let mut max_host_bytes = DEFAULT_MAX_HOST_BYTES;
    let args: Vec<String> = env::args().collect();
    let mut index = 1usize;
    while index < args.len() {
        if args[index] == "--help" || args[index] == "-h" {
            eprintln!("usage: plan [--depth N] [--strata N] [--threads N] [--cache N] [--tt-scope private|shared] [--tt-from-depth N] [--split-plies auto|N] [--max-frontier-tasks N] [--max-host-bytes N]");
            eprintln!("--cache is entries per worker in private scope, total entries in shared scope (rounded up to a power of two).");
            return;
        }
        if index + 1 >= args.len() {
            eprintln!("plan failed: {} needs a value", args[index]);
            std::process::exit(2);
        }
        match args[index].as_str() {
            "--depth" => depth = args[index + 1].parse().expect("--depth"),
            "--strata" => strata = args[index + 1].parse().expect("--strata"),
            "--threads" => threads = args[index + 1].parse().expect("--threads"),
            "--cache" => cache = args[index + 1].parse().expect("--cache"),
            "--tt-scope" => {
                table_scope = args[index + 1]
                    .parse()
                    .expect("--tt-scope must be private or shared")
            }
            "--tt-from-depth" => {
                table_from_depth = args[index + 1].parse().expect("--tt-from-depth")
            }
            "--split-plies" => {
                split_plies = if args[index + 1] == "auto" {
                    None
                } else {
                    Some(args[index + 1].parse().expect("--split-plies"))
                }
            }
            "--max-frontier-tasks" => {
                max_frontier_tasks = args[index + 1].parse().expect("--max-frontier-tasks")
            }
            "--max-host-bytes" => {
                max_host_bytes = args[index + 1].parse().expect("--max-host-bytes")
            }
            option => {
                eprintln!("plan failed: unknown option {option}");
                std::process::exit(2);
            }
        }
        index += 2;
    }
    if depth < 1 || strata < 1 {
        eprintln!("plan failed: depth and strata must be at least 1");
        std::process::exit(2);
    }
    let params = SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(depth, strata) + 1,
        policy_seed: 0xd707_5eed,
    };
    let config = ParallelConfig {
        threads,
        table_capacity_per_worker: cache,
        table_scope,
        table_from_depth,
        split_plies,
        max_frontier_tasks,
        max_host_bytes,
        ..ParallelConfig::default()
    };
    match plan_parallel_resources(params, config) {
        Ok(plan) => println!(
            "{{\"format\":\"drop7-parallel-resource-plan-v1\",\"depth\":{},\"strata\":{},\"threads\":{},\"splitPlies\":{},\"worstCaseFrontierTasks\":{},\"ttScope\":\"{}\",\"ttFromDepth\":{},\"tableBytesPerWorker\":{},\"tableEntryBytes\":{},\"projectedTableBytes\":{},\"projectedPlanBytes\":{},\"projectedTotalBytes\":{},\"maxHostBytes\":{}}}",
            depth,
            strata,
            plan.requested_threads,
            plan.split_plies,
            plan.worst_case_frontier_tasks,
            table_scope.as_str(),
            table_from_depth,
            plan.table_bytes_per_worker,
            plan.table_entry_bytes,
            plan.projected_table_bytes,
            plan.projected_plan_bytes,
            plan.projected_total_bytes,
            max_host_bytes,
        ),
        Err(error) => {
            eprintln!("plan failed: {error}");
            std::process::exit(2);
        }
    }
}
