//! Seed-free, completed-depth measurement driver. The Python runner retains
//! every repetition and OS resource counter. Also builds against the dev
//! snapshot so both arms use exactly the same driver and constructed roots.
use drop7_rs::board::{Board, EMPTY};
use drop7_rs::engine::State;
use drop7_rs::leaf::{fair_leaf, LeafScratch};
use drop7_rs::search::{canonical_state, DepthTable, FairLeaf, NoTable, SearchParams, Searcher, TranspositionTable, COLUMN_ORDER};
use std::{env, fs, hint::black_box, time::Instant};

fn roots(path: &str) -> Vec<State> {
    let text = fs::read_to_string(path).expect("roots file");
    let states: Vec<_> = text.lines().filter_map(|line| line.strip_prefix("s ")).map(|line| {
        let f: Vec<_> = line.split_whitespace().collect();
        assert_eq!(f.len(), 5);
        let next_disc = f[1].parse().unwrap();
        let moves_remaining = f[2].parse().unwrap();
        assert!((1..=7).contains(&next_disc) && (1..=5).contains(&moves_remaining));
        assert_eq!(f[4], "0");
        State { board: Board::from_serialized(f[0]).expect("board"), next_disc,
            moves_remaining, level: 1, score: 0, moves_played: 0, game_over: false }
    }).collect();
    assert!(!states.is_empty());
    states
}

fn search<T: TranspositionTable>(states: &[State], depth: i32, gate: i32, capacity: usize, make: impl Fn() -> T) {
    let params = SearchParams { depth, chance_samples: 7, maximum_work: u64::MAX, ..SearchParams::default() };
    for (index, state) in states.iter().enumerate() {
        let init = Instant::now();
        let mut searcher = Searcher::new(params, FairLeaf::default(), make());
        let initialization_seconds = init.elapsed().as_secs_f64();
        searcher.begin_parallel_decision();
        let (canonical, mirrored) = canonical_state(state);
        let start = Instant::now();
        let mut values = Vec::new();
        let (mut work, mut nodes, mut leaves, mut moves, mut hits) = (0, 0, 0, 0, 0);
        let (mut best, mut action) = (f64::NEG_INFINITY, -1);
        for column in COLUMN_ORDER {
            if canonical.board.get(0, column) != EMPTY { continue; }
            let value = searcher.evaluate_root_column(&canonical, column, depth).expect("completed depth");
            if value > best { best = value; action = column as i32; }
            values.push(format!("[{},\"{:016x}\"]", column, value.to_bits()));
            let m = searcher.last_metrics();
            work += m.work; nodes += m.nodes; leaves += m.leaf_calls; moves += m.move_calls; hits = m.cache_hits;
        }
        if mirrored && action >= 0 { action = 6 - action; }
        let seconds = start.elapsed().as_secs_f64();
        println!("{{\"mode\":\"search\",\"root\":{index},\"depth\":{depth},\"gate\":{gate},\"capacity\":{capacity},\"strata\":7,\"action\":{action},\"values\":[{}],\"work\":{work},\"nodes\":{nodes},\"leafCalls\":{leaves},\"moveCalls\":{moves},\"cacheHits\":{hits},\"tableBytes\":{},\"initializationSeconds\":{initialization_seconds:.9},\"seconds\":{seconds:.9}}}", values.join(","), searcher.table_bytes());
    }
}

fn main() {
    let a: Vec<_> = env::args().collect();
    assert_eq!(a.len(), 7, "optimization_check MODE ROOTS DEPTH GATE CAPACITY ITERATIONS");
    let states = roots(&a[2]);
    let depth: i32 = a[3].parse().unwrap();
    let gate: i32 = a[4].parse().unwrap();
    let capacity: usize = a[5].parse().unwrap();
    let iterations: u64 = a[6].parse().unwrap();
    assert!((1..=5).contains(&depth) && (0..=5).contains(&gate));
    assert!(capacity <= 262144 && iterations > 0 && iterations <= 1_000_000);
    match a[1].as_str() {
        "search" if gate == 0 => search(&states, depth, gate, capacity, || NoTable),
        "search" => search(&states, depth, gate, capacity, || DepthTable::new(capacity, gate)),
        "micro" | "unpack" => {
            let mut scratch = LeafScratch::default();
            let mut checksum = 0u64;
            let start = Instant::now();
            for _ in 0..iterations {
                for state in &states {
                    if a[1] == "micro" {
                        checksum = checksum.wrapping_add(black_box(fair_leaf(black_box(state), &mut scratch)).to_bits());
                    } else {
                        let bytes = black_box(black_box(&state.board).to_bytes());
                        checksum = checksum.wrapping_add(bytes[48] as u64);
                    }
                }
            }
            let seconds = start.elapsed().as_secs_f64();
            println!("{{\"mode\":\"{}\",\"calls\":{},\"checksum\":\"{checksum:016x}\",\"seconds\":{seconds:.9}}}", a[1], iterations * states.len() as u64);
        }
        _ => panic!("mode must be search, micro or unpack"),
    }
}
