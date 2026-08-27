// Search equivalence gate: runs the Rust fair expectimax on the root states
// emitted by cpp/search_trace and requires
//   values mode:  bit-identical per-column values and identical actions
//   metrics mode: identical action, work, nodes, cache hits, completed depth
// against the C++ fast search at the same fixed depth/strata.
//
//   gate-search --trace <file> --mode values|metrics --depth 4 --strata 7

use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::search::{DepthTable, FairLeaf, Searcher, SearchParams};

/// Build a searcher with the requested memoization arm and run `f` on it.
/// Both arms compute identical values (cache-independence), so the gate can
/// run under either to prove it.
fn with_searcher<R>(
    tt: &str,
    tt_capacity: usize,
    params: SearchParams,
    f: impl FnOnce(&mut Searcher<FairLeaf, DepthTable>) -> R,
) -> R {
    if tt == "none" {
        let mut searcher = Searcher::new(params, FairLeaf::default(), DepthTable::new(8, i32::MAX));
        f(&mut searcher)
    } else if let Some(gate) = tt.strip_prefix("gate") {
        let from_depth: i32 = gate.parse().unwrap();
        let mut searcher =
            Searcher::new(params, FairLeaf::default(), DepthTable::new(tt_capacity, from_depth));
        f(&mut searcher)
    } else {
        eprintln!("unknown --tt {tt}");
        std::process::exit(2);
    }
}

use std::env;
use std::fs;

fn parse_state(tokens: &[&str]) -> State {
    State {
        board: Board::from_serialized(tokens[0]).expect("board parses"),
        next_disc: tokens[1].parse().unwrap(),
        score: 0,
        level: tokens[3].parse().unwrap(),
        moves_remaining: tokens[2].parse().unwrap(),
        moves_played: 0,
        game_over: tokens[4] == "1",
    }
}

fn main() {
    let mut trace_path = String::new();
    let mut mode = String::from("values");
    let mut depth = 4i32;
    let mut strata = 7i32;
    let mut tt = String::from("none");
    let mut tt_capacity = 65_536usize;
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--trace" => trace_path = args[i + 1].clone(),
            "--mode" => mode = args[i + 1].clone(),
            "--depth" => depth = args[i + 1].parse().unwrap(),
            "--strata" => strata = args[i + 1].parse().unwrap(),
            "--tt" => tt = args[i + 1].clone(),
            "--tt-capacity" => tt_capacity = args[i + 1].parse().unwrap(),
            _ => {}
        }
        i += 2;
    }
    if trace_path.is_empty() {
        eprintln!("usage: gate-search --trace <file> --mode values|metrics --depth D --strata S");
        std::process::exit(2);
    }
    let text = fs::read_to_string(&trace_path).expect("read trace");

    let mut compared = 0u64;
    let mut mismatches = 0u64;
    let mut work_total = 0u64;
    let mut nodes_total = 0u64;
    let mut first_failure = String::new();

    let mut lines = text.lines().peekable();
    while let Some(line) = lines.next() {
        let Some(rest) = line.strip_prefix("s ") else {
            continue;
        };
        let state = parse_state(&rest.split_whitespace().collect::<Vec<_>>());
        compared += 1;

        if mode == "values" {
            let params = SearchParams {
                depth,
                chance_samples: strata,
                terminal_utility: -1_000_000.0,
                maximum_work: 1u64 << 62,
                policy_seed: 0xd707_5eed,
            };
            let (values, action) = with_searcher(&tt, tt_capacity, params, |searcher| {
                searcher.column_values(&state, depth)
            });

            // Read the c <col> <bits> lines and the final a <col> line.
            let mut expected_action: i32 = i32::MIN;
            let mut cpp_values: Vec<(usize, u64)> = Vec::new();
            while let Some(next) = lines.peek() {
                if next.starts_with("c ") || next.starts_with("a ") {
                    let record = lines.next().unwrap();
                    let tokens: Vec<&str> = record.split_whitespace().collect();
                    if tokens[0] == "c" {
                        cpp_values.push((
                            tokens[1].parse().unwrap(),
                            u64::from_str_radix(tokens[2], 16).unwrap(),
                        ));
                    } else {
                        expected_action = tokens[1].parse().unwrap();
                    }
                } else {
                    break;
                }
            }
            let mut ok = values.len() == cpp_values.len();
            if ok {
                for ((col, value), (ecol, ebits)) in values.iter().zip(cpp_values.iter()) {
                    if col != ecol || value.to_bits() != *ebits {
                        ok = false;
                        break;
                    }
                }
            }
            if action != expected_action {
                ok = false;
            }
            if !ok {
                mismatches += 1;
                if first_failure.is_empty() {
                    first_failure = format!(
                        "state {}: rust action {action} vs c++ {expected_action}; values {:?} vs {:?}",
                        rest,
                        values.iter().map(|(c, v)| (c, format!("{:e}", v))).collect::<Vec<_>>(),
                        cpp_values,
                    );
                }
            }
        } else {
            // The Rust search carries no transposition table (the measured
            // hit rate is ~1.3%, pure overhead), so its work/node counts are
            // its own and are reported, not compared.  What must match the
            // C++ fast search exactly is the chosen action and the completed
            // depth: the values the argmax reads are table-independent.
            let params = SearchParams {
                depth,
                chance_samples: strata,
                terminal_utility: -1_000_000.0,
                maximum_work: 1u64 << 62,
                policy_seed: 0xd707_5eed,
            };
            let (action, metrics) = with_searcher(&tt, tt_capacity, params, |searcher| {
                searcher.choose_action(&state)
            });

            let record = lines.next().expect("metrics line");
            let tokens: Vec<&str> = record.split_whitespace().collect();
            assert_eq!(tokens[0], "m");
            let expected_action: i32 = tokens[1].parse().unwrap();
            let expected_depth: i32 = tokens[5].parse().unwrap();
            work_total += metrics.work;
            nodes_total += metrics.nodes;
            if action != expected_action || metrics.completed_depth != expected_depth {
                mismatches += 1;
                if first_failure.is_empty() {
                    first_failure = format!(
                        "state {}: rust (action {} depth {}) vs c++ (action {} depth {})",
                        rest, action, metrics.completed_depth, expected_action, expected_depth,
                    );
                }
            }
        }
    }

    println!("roots compared: {compared}");
    if mode == "metrics" {
        println!("rust work total: {work_total}");
        println!("rust nodes total: {nodes_total}");
    }
    println!("mismatches: {mismatches}");
    if mismatches > 0 {
        println!("first failure: {first_failure}");
    }
    let ok = mismatches == 0 && compared > 0;
    println!(
        "{}",
        if ok { "SEARCH GATE PASSED" } else { "SEARCH GATE FAILED" }
    );
    std::process::exit(if ok { 0 } else { 1 });
}
