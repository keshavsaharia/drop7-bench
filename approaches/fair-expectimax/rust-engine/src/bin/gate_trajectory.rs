// Trajectory equivalence gate: replays the columns recorded by the C++
// reference trace emitter (cpp/trace.cpp) through the Rust engine and
// compares everything observable, move by move: board, next disc, score,
// score delta, the full wave list, level, moves remaining, board-clear and
// level-advance flags, and the terminal flag.
//
// The columns come from the trace, so the gate isolates engine semantics
// from policy semantics.  Any nonzero mismatch count fails.
//
//   gate-trajectory --trace <path-to-c++-trace>

use drop7_rs::board::Board;
use drop7_rs::engine::{
    initial_ts_state, play_headless_move, play_ts_move, FullWaveSink, State,
};
use drop7_rs::rng::Mulberry32;

use std::env;
use std::fs;

struct MoveRecord {
    moves_played: i32,
    column: usize,
    score_delta: i64,
    board: String,
    next_disc: u8,
    score: i64,
    level: i32,
    moves_remaining: i32,
    game_over: bool,
    cleared: bool,
    advanced: bool,
    waves: String,
}

fn parse_move(line: &str) -> MoveRecord {
    // m <played> col <c> sd <delta> b <board> next <d> score <s> level <l>
    // mr <r> over <0|1> cleared <0|1> advanced <0|1> waves d:c:r:p ...
    let tokens: Vec<&str> = line.split_whitespace().collect();
    let mut record = MoveRecord {
        moves_played: 0,
        column: 0,
        score_delta: 0,
        board: String::new(),
        next_disc: 0,
        score: 0,
        level: 0,
        moves_remaining: 0,
        game_over: false,
        cleared: false,
        advanced: false,
        waves: String::new(),
    };
    let mut i = 0;
    while i < tokens.len() {
        match tokens[i] {
            "m" => {
                record.moves_played = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "col" => {
                record.column = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "sd" => {
                record.score_delta = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "b" => {
                record.board = tokens[i + 1].to_string();
                i += 2;
            }
            "next" => {
                record.next_disc = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "score" => {
                record.score = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "level" => {
                record.level = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "mr" => {
                record.moves_remaining = tokens[i + 1].parse().unwrap();
                i += 2;
            }
            "over" => {
                record.game_over = tokens[i + 1] == "1";
                i += 2;
            }
            "cleared" => {
                record.cleared = tokens[i + 1] == "1";
                i += 2;
            }
            "advanced" => {
                record.advanced = tokens[i + 1] == "1";
                i += 2;
            }
            "waves" => {
                record.waves = tokens[i + 1..].join(" ");
                break;
            }
            _ => panic!("unknown token {}", tokens[i]),
        }
    }
    record
}

fn main() {
    let mut trace_path = String::new();
    let mut driver = String::from("headless");
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--trace" => trace_path = args[i + 1].clone(),
            "--driver" => driver = args[i + 1].clone(),
            _ => {}
        }
        i += 2;
    }
    if trace_path.is_empty() {
        eprintln!("usage: gate-trajectory --trace <trace file> [--driver headless|ts]");
        std::process::exit(2);
    }
    let text = fs::read_to_string(&trace_path).expect("read trace");

    let mut games = 0u64;
    let mut moves = 0u64;
    let mut waves_total = 0u64;
    let mut mismatches = 0u64;
    let mut first_failure = String::new();

    let mut state = State {
        board: Board::initial(),
        next_disc: 0,
        score: 0,
        level: 1,
        moves_remaining: 5,
        moves_played: 0,
        game_over: false,
    };
    let mut seed = 0u32;
    let mut sink = FullWaveSink::new();
    let mut ts_random = Mulberry32::new(0);

    for line in text.lines() {
        if let Some(rest) = line.strip_prefix("game 0x") {
            // game <seed> next <d>
            let tokens: Vec<&str> = rest.split_whitespace().collect();
            seed = u32::from_str_radix(tokens[0], 16).unwrap();
            let next: u8 = tokens[2].parse().unwrap();
            if driver == "ts" {
                ts_random = Mulberry32::new(seed);
                state = initial_ts_state(&mut ts_random);
            } else {
                state = State::initial_headless(seed);
            }
            if state.next_disc != next {
                mismatches += 1;
                if first_failure.is_empty() {
                    first_failure = format!("seed {seed:#x}: initial next disc differs");
                }
            }
            games += 1;
        } else if line.starts_with("m ") {
            let record = parse_move(line);
            sink.clear();
            let result = if driver == "ts" {
                play_ts_move(&mut state, &mut ts_random, record.column, &mut sink)
            } else {
                play_headless_move(&mut state, seed, record.column, &mut sink)
            };
            moves += 1;
            waves_total += sink.count as u64;
            let rust_waves: Vec<String> = sink.waves[..sink.count]
                .iter()
                .map(|w| format!("{}:{}:{}:{}", w.depth, w.cleared, w.revealed, w.points))
                .collect();
            let identical = result.is_some()
                && state.moves_played == record.moves_played
                && result.unwrap().score_delta == record.score_delta
                && state.board.serialize() == record.board
                && state.next_disc == record.next_disc
                && state.score == record.score
                && state.level == record.level
                && state.moves_remaining == record.moves_remaining
                && state.game_over == record.game_over
                && result.unwrap().cleared_board == record.cleared
                && result.unwrap().level_advanced == record.advanced
                && rust_waves.join(" ") == record.waves;
            if !identical {
                mismatches += 1;
                if first_failure.is_empty() {
                    first_failure = format!(
                        "seed {seed:#x} move {}\n  rust: b {} next {} score {} mr {} waves {}\n  ref:  b {} next {} score {} mr {} waves {}",
                        record.moves_played,
                        state.board.serialize(),
                        state.next_disc,
                        state.score,
                        state.moves_remaining,
                        rust_waves.join(" "),
                        record.board,
                        record.next_disc,
                        record.score,
                        record.moves_remaining,
                        record.waves,
                    );
                }
            }
        } else if line.starts_with("end ") {
            // end score <s> moves <m> over <0|1>
            let tokens: Vec<&str> = line.split_whitespace().collect();
            let end_score: i64 = tokens[2].parse().unwrap();
            let end_moves: i32 = tokens[4].parse().unwrap();
            let end_over = tokens[6] == "1";
            if state.score != end_score
                || state.moves_played != end_moves
                || state.game_over != end_over
            {
                mismatches += 1;
                if first_failure.is_empty() {
                    first_failure = format!("seed {seed:#x}: terminal state differs");
                }
            }
        }
    }

    println!("games compared: {games}");
    println!("moves compared: {moves}");
    println!("waves compared: {waves_total}");
    println!("mismatches: {mismatches}");
    if mismatches > 0 {
        println!("first failure: {first_failure}");
    }
    let ok = mismatches == 0 && games > 0;
    println!(
        "{}",
        if ok {
            "TRAJECTORY GATE PASSED"
        } else {
            "TRAJECTORY GATE FAILED"
        }
    );
    std::process::exit(if ok { 0 } else { 1 });
}
