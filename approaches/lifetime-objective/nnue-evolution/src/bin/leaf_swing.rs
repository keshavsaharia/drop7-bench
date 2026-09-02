// Diagnostic: how much of a root's per-column search value, and of the
// spread between siblings, comes from the leaf evaluator rather than from
// the score deltas the search accumulates in the tree (rise bonuses, chain
// clears, terminal utility)?  For every corpus root the fixed-depth search is
// run twice on identical chance scenarios: once with the frozen fair leaf and
// once with a leaf that returns 0.  The per-column difference is exactly the
// leaf's contribution; comparing the two argmaxes shows how often the leaf
// decides the move.  Reads already-opened training-role corpus roots only;
// opens no seed and makes no strength claim.
//
// Usage: leaf_swing --parts DIR --roots N [--depth 3] [--threads T]

use drop7_nnue_evolution::json::{parse, Json};
use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::search::{DepthTable, FairLeaf, Leaf, SearchParams, Searcher};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

struct ZeroLeaf;
impl Leaf for ZeroLeaf {
    fn value(&mut self, _state: &State) -> f64 {
        0.0
    }
}

struct Root {
    state: State,
    teacher_chosen: usize,
    teacher_spread: f64,
}

fn load_roots(parts: &str, limit: usize) -> Vec<Root> {
    let mut names: Vec<String> = std::fs::read_dir(parts)
        .expect("parts dir")
        .filter_map(|e| e.ok().map(|e| e.file_name().to_string_lossy().into_owned()))
        .filter(|n| n.ends_with(".jsonl"))
        .collect();
    names.sort();
    let mut roots = Vec::new();
    let mut all = Vec::new();
    for name in names {
        let text = std::fs::read_to_string(format!("{parts}/{name}")).unwrap();
        for line in text.lines() {
            let v = parse(line).unwrap();
            if v.get("type").and_then(Json::as_str) != Some("root") {
                continue;
            }
            let board: String = v
                .get("board")
                .and_then(Json::as_array)
                .unwrap()
                .iter()
                .map(|c| (b'0' + c.as_f64().unwrap() as u8) as char)
                .collect();
            let cols = v.get("columns").and_then(Json::as_array).unwrap();
            let values: Vec<f64> = cols.iter().map(|p| p.as_array().unwrap()[1].as_f64().unwrap()).collect();
            let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
            let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
            all.push(Root {
                state: State {
                    board: Board::from_serialized(&board).unwrap(),
                    next_disc: v.get("next").and_then(Json::as_f64).unwrap() as u8,
                    score: 0,
                    level: 1,
                    moves_remaining: v.get("movesRemaining").and_then(Json::as_f64).unwrap() as i32,
                    moves_played: 0,
                    game_over: false,
                },
                teacher_chosen: v.get("chosen").and_then(Json::as_f64).unwrap() as usize,
                teacher_spread: max - min,
            });
        }
    }
    // Evenly spaced sample across every game and phase.
    let step = (all.len() / limit.max(1)).max(1);
    for (i, r) in all.into_iter().enumerate() {
        if i % step == 0 && roots.len() < limit {
            roots.push(r);
        }
    }
    roots
}

struct Row {
    fair_spread: f64,
    zero_spread: f64,
    leaf_spread: f64,
    leaf_abs_mean: f64,
    argmax_changes: bool,
    fair_matches_teacher: bool,
    zero_matches_teacher: bool,
    has_terminal_column: bool,
    teacher_spread: f64,
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut parts = String::new();
    let mut limit = 200usize;
    let mut depth = 3i32;
    let mut threads = 4usize;
    let mut i = 1;
    while i < args.len() {
        let v = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--parts" => parts = v.to_string(),
            "--roots" => limit = v.parse().unwrap(),
            "--depth" => depth = v.parse().unwrap(),
            "--threads" => threads = v.parse().unwrap(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    let roots = load_roots(&parts, limit);
    let params = SearchParams {
        depth,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: u64::MAX,
        policy_seed: 0xd707_5eed,
    };
    let cursor = AtomicUsize::new(0);
    let rows: Mutex<Vec<Row>> = Mutex::new(Vec::new());
    std::thread::scope(|scope| {
        for _ in 0..threads {
            scope.spawn(|| loop {
                let index = cursor.fetch_add(1, Ordering::Relaxed);
                if index >= roots.len() {
                    break;
                }
                let root = &roots[index];
                let mut fair = Searcher::new(params, FairLeaf::default(), DepthTable::new(65_536, 1));
                let mut zero = Searcher::new(params, ZeroLeaf, DepthTable::new(65_536, 1));
                let (vf, af) = fair.column_values(&root.state, depth);
                let (vz, az) = zero.column_values(&root.state, depth);
                assert_eq!(vf.len(), vz.len());
                let spread = |v: &Vec<(usize, f64)>| {
                    let max = v.iter().map(|p| p.1).fold(f64::NEG_INFINITY, f64::max);
                    let min = v.iter().map(|p| p.1).fold(f64::INFINITY, f64::min);
                    max - min
                };
                let contrib: Vec<f64> = vf.iter().zip(vz.iter()).map(|(a, b)| a.1 - b.1).collect();
                let cmax = contrib.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                let cmin = contrib.iter().cloned().fold(f64::INFINITY, f64::min);
                rows.lock().unwrap().push(Row {
                    fair_spread: spread(&vf),
                    zero_spread: spread(&vz),
                    leaf_spread: cmax - cmin,
                    leaf_abs_mean: contrib.iter().map(|c| c.abs()).sum::<f64>() / contrib.len() as f64,
                    argmax_changes: af != az,
                    fair_matches_teacher: af as usize == root.teacher_chosen,
                    zero_matches_teacher: az as usize == root.teacher_chosen,
                    has_terminal_column: vf.iter().any(|p| p.1 < -500_000.0),
                    teacher_spread: root.teacher_spread,
                });
            });
        }
    });
    let rows = rows.into_inner().unwrap();
    let n = rows.len() as f64;
    let q = |f: &dyn Fn(&Row) -> f64| -> (f64, f64, f64) {
        let mut v: Vec<f64> = rows.iter().map(f).collect();
        v.sort_by(|a, b| a.partial_cmp(b).unwrap());
        (v[v.len() / 4], v[v.len() / 2], v[3 * v.len() / 4])
    };
    println!("roots {} depth {} strata 7", rows.len(), depth);
    let (a, b, c) = q(&|r| r.fair_spread);
    println!("sibling spread, fair leaf      q25 {a:9.0} median {b:9.0} q75 {c:9.0}");
    let (a, b, c) = q(&|r| r.zero_spread);
    println!("sibling spread, zero leaf      q25 {a:9.0} median {b:9.0} q75 {c:9.0}   (in-tree score deltas only)");
    let (a, b, c) = q(&|r| r.leaf_spread);
    println!("leaf contribution spread       q25 {a:9.0} median {b:9.0} q75 {c:9.0}   (max minus min leaf term across columns)");
    let (a, b, c) = q(&|r| r.leaf_abs_mean);
    println!("mean |leaf term| per column    q25 {a:9.0} median {b:9.0} q75 {c:9.0}");
    let (a, b, c) = q(&|r| r.teacher_spread);
    println!("teacher (d5) sibling spread    q25 {a:9.0} median {b:9.0} q75 {c:9.0}");
    let non_terminal: Vec<&Row> = rows.iter().filter(|r| !r.has_terminal_column).collect();
    let share = |rows: &[&Row]| {
        let mut v: Vec<f64> = rows.iter().map(|r| r.leaf_spread / r.fair_spread.max(1e-9)).collect();
        v.sort_by(|a, b| a.partial_cmp(b).unwrap());
        v[v.len() / 2]
    };
    println!(
        "roots with a terminal (lost) column: {} of {}",
        rows.len() - non_terminal.len(),
        rows.len()
    );
    println!(
        "median leaf-spread / value-spread on non-terminal roots: {:.2}",
        share(&non_terminal)
    );
    println!(
        "argmax changes when the leaf is removed: {:.1}% of roots ({:.1}% of non-terminal roots)",
        100.0 * rows.iter().filter(|r| r.argmax_changes).count() as f64 / n,
        100.0 * non_terminal.iter().filter(|r| r.argmax_changes).count() as f64 / non_terminal.len().max(1) as f64
    );
    println!(
        "agreement with the depth-5 teacher's column: fair leaf {:.1}%, zero leaf {:.1}%",
        100.0 * rows.iter().filter(|r| r.fair_matches_teacher).count() as f64 / n,
        100.0 * rows.iter().filter(|r| r.zero_matches_teacher).count() as f64 / n
    );
}
