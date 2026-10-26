// One-shot decision query for the n-tuple tables this crate produces.  Given
// a single public board state on the command line, prints the internal
// column index (0-6) the chosen arm plays: by default the frozen tables as
// the leaf of the deployed depth-3 seven-stratum fair search, exactly the
// `Arm::NTupleD3` configuration the screen binary evaluates.
//
// This is a synchronous bridge, not a D7P server (see docs/d7p-protocol.md):
// it answers one query and exits, which lets a harness whose interface is a
// synchronous function of the public state (the TypeScript benchmark
// playground's BenchPolicy.chooseColumn, through src/bench/bench-external.ts)
// call it per move as a plain subprocess.  The convention is the one
// approaches/lifetime-objective/nnue-evolution/src/bin/query_move.rs set.
//
// Never used for research-tier evidence: whatever calls this must stay inside
// the benchmark playground (.agents/skills/drop7-benchmark-playground/SKILL.md)
// and never a research seed lease.  The tables are loaded from disk on every
// call (about four gigabytes for the first experiment's layout), so a
// scripted-round game costs a few seconds per move.
//
// Usage: query_move [--arm ARM] [--weights FILE] --board <49 chars> --next <1-7> --rise <1-5>
//
//   --arm      ntuple-d3 (default): the tables as the d3s7 leaf, needs --weights
//              ntuple-d4:           the tables as the d4s7 leaf, needs --weights
//              direct:              the tables played one ply, needs --weights
//              fair-d3:             the frozen fair leaf inside d3s7
//              fair-d4:             the frozen fair leaf inside d4s7
//   --weights  a Model::save output (best-weights.bin / candidate-weights.bin)
//   --board    49-character serialize() string, row-major from the top:
//              '0' empty, '1'-'7' numbered, '8' solid gray, '9' cracked gray
//              (identical to the reference engines' serializeBoard/board.join(""))
//   --next     the visible next disc, 1-7
//   --rise     moves remaining before the next row rise, 1-5
//
// INFORMATION BOUNDARY.  Only the board, the visible next disc and the rise
// clock cross the process boundary; score, level and move number are fixed
// constants below, as in every arm of the screen.

use std::io::{BufRead, Write};
use std::path::Path;
use std::sync::Arc;

use drop7_ntuple_scale::game::Arm;
use drop7_ntuple_scale::model::{CalibrationConfig, Model};
use drop7_ntuple_scale::policy::{HybridLeaf, NTupleLeaf};
use drop7_ntuple_scale::{deployment_params, reference_d4_params, DEPLOYMENT_TABLE, REFERENCE_TABLE};
use drop7_rs::board::{Board, BOARD_SIZE, EMPTY};
use drop7_rs::engine::{center_first_move, State};
use drop7_rs::search::{canonical_state, work_bound_for, DepthTable, SearchParams, Searcher, COLUMN_ORDER};

fn d5_params() -> SearchParams {
    SearchParams {
        depth: 5,
        chance_samples: 7,
        terminal_utility: -1_000_000.0,
        maximum_work: work_bound_for(5, 7) + 1,
        policy_seed: 0xd707_5eed,
    }
}

fn decide_parallel<L: drop7_rs::search::Leaf + Send>(
    searchers: &mut [Searcher<L, DepthTable>],
    state: &State,
    depth: i32,
) -> i32 {
    if state.game_over {
        return -1;
    }
    let (canonical, mirrored) = canonical_state(state);
    let mut results = [f64::NEG_INFINITY; BOARD_SIZE];
    std::thread::scope(|s| {
        let canonical_ref = &canonical;
        for ((col, searcher), res) in searchers
            .iter_mut()
            .enumerate()
            .zip(results.iter_mut())
        {
            if canonical_ref.board.get(0, col) == EMPTY {
                s.spawn(move || {
                    searcher.begin_parallel_decision();
                    *res = searcher
                        .evaluate_root_column(canonical_ref, col, depth)
                        .unwrap_or(f64::NEG_INFINITY);
                });
            }
        }
    });
    let mut action = -1i32;
    let mut best_value = f64::NEG_INFINITY;
    for &col in COLUMN_ORDER.iter() {
        if canonical.board.get(0, col) != EMPTY {
            continue;
        }
        let val = results[col];
        if val > best_value {
            best_value = val;
            action = col as i32;
        }
    }
    if action < 0 {
        action = center_first_move(&canonical.board)
            .map(|c| c as i32)
            .unwrap_or(-1);
    }
    if mirrored && action >= 0 {
        BOARD_SIZE as i32 - 1 - action
    } else {
        action
    }
}

fn parse_state(board_str: &str, next: u8, rise: i32) -> Result<State, String> {
    if !(1..=7).contains(&next) {
        return Err("--next must be 1-7".into());
    }
    if !(1..=5).contains(&rise) {
        return Err("--rise must be 1-5".into());
    }
    let board = Board::from_serialized(board_str).ok_or("bad --board: not 49 digit characters")?;
    Ok(State {
        board,
        next_disc: next,
        score: 0,
        level: 1,
        moves_remaining: rise,
        moves_played: 0,
        game_over: false,
    })
}

fn main() -> Result<(), String> {
    let mut arm_name = String::from("ntuple-d3");
    let mut weights = None;
    let mut cal_spec = None;
    let mut board_str = None;
    let mut next = None;
    let mut rise = None;
    let mut serve = false;
    let mut parallel_root = false;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--serve" => {
                serve = true;
                i += 1;
                continue;
            }
            "--parallel-root" => {
                parallel_root = true;
                i += 1;
                continue;
            }
            _ => {}
        }
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--arm" => arm_name = value.to_string(),
            "--weights" => weights = Some(value.to_string()),
            "--cal" => cal_spec = Some(value.to_string()),
            "--board" => board_str = Some(value.to_string()),
            "--next" => next = Some(value.parse::<u8>().map_err(|_| "bad --next")?),
            "--rise" => rise = Some(value.parse::<i32>().map_err(|_| "bad --rise")?),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }

    let load_tables = || -> Result<Arc<Model>, String> {
        let path = weights.as_deref().ok_or("--weights required for this arm")?;
        Ok(Arc::new(Model::load(Path::new(path), false)?))
    };
    let cfg = match cal_spec.as_deref() {
        Some(spec) => Some(CalibrationConfig::parse(spec)?),
        None => None,
    };

    if parallel_root && (arm_name.ends_with("-d3") || arm_name.ends_with("-d4") || arm_name.ends_with("-d5")) {
        let model = load_tables()?;
        let (params, table_cap, depth) = if arm_name.ends_with("-d3") {
            (deployment_params(), DEPLOYMENT_TABLE, 3)
        } else if arm_name.ends_with("-d4") {
            (reference_d4_params(), REFERENCE_TABLE, 4)
        } else {
            (d5_params(), REFERENCE_TABLE, 5)
        };
        if let Some(cal) = cfg {
            let mut searchers: Vec<Searcher<HybridLeaf, DepthTable>> = (0..BOARD_SIZE)
                .map(|_| Searcher::new(params, HybridLeaf::new(model.clone(), cal), DepthTable::new(table_cap, 1)))
                .collect();
            if serve {
                let stdin = std::io::stdin();
                let mut stdout = std::io::stdout();
                for line in stdin.lock().lines() {
                    let line = line.map_err(|e| e.to_string())?;
                    let line = line.trim();
                    if line.is_empty() {
                        continue;
                    }
                    let mut parts = line.split_whitespace();
                    let b = parts.next().ok_or("missing board")?;
                    let n: u8 = parts.next().ok_or("missing next")?.parse().map_err(|_| "bad next")?;
                    let r: i32 = parts.next().ok_or("missing rise")?.parse().map_err(|_| "bad rise")?;
                    let state = parse_state(b, n, r)?;
                    let action = decide_parallel(&mut searchers, &state, depth);
                    writeln!(stdout, "{action}").map_err(|e| e.to_string())?;
                    stdout.flush().map_err(|e| e.to_string())?;
                }
                return Ok(());
            } else {
                let state = parse_state(
                    &board_str.ok_or("--board required")?,
                    next.ok_or("--next required")?,
                    rise.ok_or("--rise required")?,
                )?;
                let action = decide_parallel(&mut searchers, &state, depth);
                if action < 0 {
                    return Err("no legal column".into());
                }
                println!("{action}");
                return Ok(());
            }
        } else {
            let mut searchers: Vec<Searcher<NTupleLeaf, DepthTable>> = (0..BOARD_SIZE)
                .map(|_| Searcher::new(params, NTupleLeaf::new(model.clone()), DepthTable::new(table_cap, 1)))
                .collect();
            if serve {
                let stdin = std::io::stdin();
                let mut stdout = std::io::stdout();
                for line in stdin.lock().lines() {
                    let line = line.map_err(|e| e.to_string())?;
                    let line = line.trim();
                    if line.is_empty() {
                        continue;
                    }
                    let mut parts = line.split_whitespace();
                    let b = parts.next().ok_or("missing board")?;
                    let n: u8 = parts.next().ok_or("missing next")?.parse().map_err(|_| "bad next")?;
                    let r: i32 = parts.next().ok_or("missing rise")?.parse().map_err(|_| "bad rise")?;
                    let state = parse_state(b, n, r)?;
                    let action = decide_parallel(&mut searchers, &state, depth);
                    writeln!(stdout, "{action}").map_err(|e| e.to_string())?;
                    stdout.flush().map_err(|e| e.to_string())?;
                }
                return Ok(());
            } else {
                let state = parse_state(
                    &board_str.ok_or("--board required")?,
                    next.ok_or("--next required")?,
                    rise.ok_or("--rise required")?,
                )?;
                let action = decide_parallel(&mut searchers, &state, depth);
                if action < 0 {
                    return Err("no legal column".into());
                }
                println!("{action}");
                return Ok(());
            }
        }
    }

    let arm = match (arm_name.as_str(), cfg) {
        ("ntuple-d3" | "hybrid-d3", Some(cal)) => Arm::HybridD3(load_tables()?, cal),
        ("ntuple-d4" | "hybrid-d4", Some(cal)) => Arm::HybridD4(load_tables()?, cal),
        ("ntuple-d3", None) => Arm::NTupleD3(load_tables()?),
        ("ntuple-d4", None) => Arm::NTupleD4(load_tables()?),
        ("direct", _) => Arm::Direct(load_tables()?),
        ("fair-d3", _) => Arm::FairD3,
        ("fair-d4", _) => Arm::FairD4,
        (other, _) => return Err(format!("unknown --arm {other}")),
    };
    let mut player = arm.player();
    if serve {
        let stdin = std::io::stdin();
        let mut stdout = std::io::stdout();
        for line in stdin.lock().lines() {
            let line = line.map_err(|e| e.to_string())?;
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let mut parts = line.split_whitespace();
            let b = parts.next().ok_or("missing board")?;
            let n: u8 = parts.next().ok_or("missing next")?.parse().map_err(|_| "bad next")?;
            let r: i32 = parts.next().ok_or("missing rise")?.parse().map_err(|_| "bad rise")?;
            let state = parse_state(b, n, r)?;
            let (action, _work, _depth) = player.decide(&state);
            writeln!(stdout, "{action}").map_err(|e| e.to_string())?;
            stdout.flush().map_err(|e| e.to_string())?;
        }
        return Ok(());
    }
    let state = parse_state(
        &board_str.ok_or("--board required")?,
        next.ok_or("--next required")?,
        rise.ok_or("--rise required")?,
    )?;
    let (action, _work, _depth) = player.decide(&state);
    if action < 0 {
        return Err("no legal column".into());
    }
    println!("{action}");
    Ok(())
}
