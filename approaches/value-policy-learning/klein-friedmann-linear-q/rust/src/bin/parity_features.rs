// CHECK gate G1: feature parity against the upstream Python extractor.
//
//   parity_features --input FILE
//
// Each input line (written by reproduction/export_parity.py --mode features):
//   <board49 engine encoding> <next> <action> <mask6> <v0> <v1> <v2> <v3> <v4> <v5>
// Exit 0 with zero mismatches, 1 otherwise.

use drop7_kf_linear_q::features::{features, FEATURE_COUNT};
use drop7_kf_linear_q::view::PublicView;
use drop7_rs::board::Board;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut input = String::new();
    let mut i = 1;
    while i + 1 < args.len() {
        if args[i] == "--input" {
            input = args[i + 1].clone();
        }
        i += 2;
    }
    if input.is_empty() {
        eprintln!("usage: parity_features --input FILE");
        std::process::exit(2);
    }
    let text = std::fs::read_to_string(&input).expect("read input");
    let mut pairs = 0u64;
    let mut states = std::collections::HashSet::new();
    let mut mismatches = 0u64;
    let mut shown = 0;
    for (line_no, line) in text.lines().enumerate() {
        let t: Vec<&str> = line.split_whitespace().collect();
        if t.len() != 4 + FEATURE_COUNT {
            eprintln!("{input}:{}: malformed line", line_no + 1);
            std::process::exit(2);
        }
        let board = Board::from_serialized(t[0]).expect("board");
        let next: u8 = t[1].parse().expect("next");
        let action: usize = t[2].parse().expect("action");
        let view = PublicView::new(board, next, 5);
        let f = features(&view, action);
        let mask = t[3];
        let mut values = [0i32; FEATURE_COUNT];
        for k in 0..FEATURE_COUNT {
            values[k] = t[4 + k].parse().expect("value");
        }
        pairs += 1;
        states.insert((t[0].to_string(), next));
        if f.mask_string() != mask || f.values != values {
            mismatches += 1;
            if shown < 8 {
                shown += 1;
                println!(
                    "MISMATCH line {} board {} next {} action {}: upstream {} {:?} rust {} {:?}",
                    line_no + 1,
                    t[0],
                    next,
                    action,
                    mask,
                    values,
                    f.mask_string(),
                    f.values
                );
            }
        }
    }
    println!(
        "parity_features: pairs {} states {} mismatches {}",
        pairs,
        states.len(),
        mismatches
    );
    if mismatches != 0 || pairs == 0 {
        std::process::exit(1);
    }
}
