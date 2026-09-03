// The eighteen fair-leaf terms, the frozen leaf value and the best immediate
// six-feature drop value of every panel root, for the offline leaf refit.
//
//   leaf_terms --panel panel.ndjson --kf-weights weights-cem.txt --out root-terms.ndjson

use drop7_kf_linear_q::learn::load_weights;
use drop7_oneply_q::leaf::kf_best;
use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::leaf::{fair_leaf, fair_leaf_terms, LeafScratch};

fn json_int(line: &str, key: &str) -> i64 {
    let pattern = format!("\"{key}\":");
    let start = line.find(&pattern).unwrap_or_else(|| panic!("missing {key}")) + pattern.len();
    let rest = &line[start..];
    let end = rest.find(|c: char| !(c.is_ascii_digit() || c == '-')).unwrap_or(rest.len());
    rest[..end].parse().unwrap_or_else(|_| panic!("bad integer for {key}"))
}

fn json_str(line: &str, key: &str) -> String {
    let pattern = format!("\"{key}\":\"");
    let start = line.find(&pattern).unwrap_or_else(|| panic!("missing {key}")) + pattern.len();
    let rest = &line[start..];
    rest[..rest.find('"').expect("closing quote")].to_string()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut panel = String::new();
    let mut kf_path = String::new();
    let mut out = String::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--panel" => panel = v.to_string(),
            "--kf-weights" => kf_path = v.to_string(),
            "--out" => out = v.to_string(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}", args[i]);
    }
    assert!(!panel.is_empty() && !kf_path.is_empty() && !out.is_empty());
    let kf = load_weights(&kf_path).unwrap_or_else(|e| panic!("{e}"));
    let text = std::fs::read_to_string(&panel).expect("read panel");
    let mut scratch = LeafScratch::default();
    let mut lines: Vec<String> = Vec::new();
    for line in text.lines().filter(|l| !l.trim().is_empty()) {
        let state = State {
            board: Board::from_serialized(&json_str(line, "board")).expect("board"),
            next_disc: json_int(line, "next") as u8,
            score: 0,
            level: 1,
            moves_remaining: json_int(line, "movesRemaining") as i32,
            moves_played: 0,
            game_over: false,
        };
        let terms = fair_leaf_terms(&state, &mut scratch);
        let leaf = fair_leaf(&state, &mut scratch);
        let best = kf_best(&state, &kf);
        lines.push(format!(
            "{{\"game\":{},\"move\":{},\"terms\":[{}],\"fairLeaf\":{:?},\"kfBest\":{:?}}}",
            json_int(line, "game"),
            json_int(line, "move"),
            terms.iter().map(|t| format!("{t:?}")).collect::<Vec<_>>().join(","),
            leaf,
            best
        ));
    }
    std::fs::write(&out, lines.join("\n") + "\n").expect("write out");
    eprintln!("leaf_terms: {} roots", lines.len());
}
