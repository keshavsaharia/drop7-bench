// Leaf equivalence gate: reads state/value pairs emitted by the C++ fast
// leaf (cpp/leaf_trace) and compares the Rust fair leaf's uint64 bit pattern
// on every state.  Any nonzero mismatch count fails.
//
//   gate-leaf --trace <path>

use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::leaf::{fair_leaf, LeafScratch};

use std::env;
use std::fs;

fn main() {
    let mut trace_path = String::new();
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i + 1 < args.len() {
        if args[i] == "--trace" {
            trace_path = args[i + 1].clone();
        }
        i += 2;
    }
    if trace_path.is_empty() {
        eprintln!("usage: gate-leaf --trace <c++ leaf trace file>");
        std::process::exit(2);
    }
    let text = fs::read_to_string(&trace_path).expect("read trace");

    let mut compared = 0u64;
    let mut mismatches = 0u64;
    let mut first_failure = String::new();
    let mut scratch = LeafScratch::default();

    let mut lines = text.lines();
    while let Some(state_line) = lines.next() {
        let Some(rest) = state_line.strip_prefix("s ") else {
            continue;
        };
        let tokens: Vec<&str> = rest.split_whitespace().collect();
        let board = Board::from_serialized(tokens[0]).expect("board parses");
        let state = State {
            board,
            next_disc: tokens[1].parse().unwrap(),
            score: 0,
            level: tokens[3].parse().unwrap(),
            moves_remaining: tokens[2].parse().unwrap(),
            moves_played: 0,
            game_over: tokens[4] == "1",
        };
        let value_line = lines.next().expect("value line");
        let hex = value_line.strip_prefix("v ").expect("value prefix");
        let expected_bits = u64::from_str_radix(hex, 16).expect("hex bits");

        let value = fair_leaf(&state, &mut scratch);
        let bits = value.to_bits();
        compared += 1;
        if bits != expected_bits {
            mismatches += 1;
            if first_failure.is_empty() {
                first_failure = format!(
                    "state {} next {} mr {}: rust {value:e} ({bits:#x}) != c++ ({expected_bits:#x})",
                    tokens[0], tokens[1], tokens[2],
                );
            }
        }
    }

    println!("leaf values compared: {compared}");
    println!("mismatches: {mismatches}");
    if mismatches > 0 {
        println!("first failure: {first_failure}");
    }
    let ok = mismatches == 0 && compared > 0;
    println!(
        "{}",
        if ok { "LEAF GATE PASSED" } else { "LEAF GATE FAILED" }
    );
    std::process::exit(if ok { 0 } else { 1 });
}
