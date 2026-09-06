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

use std::path::Path;
use std::sync::Arc;

use drop7_ntuple_scale::game::Arm;
use drop7_ntuple_scale::model::Model;
use drop7_rs::board::Board;
use drop7_rs::engine::State;

fn main() -> Result<(), String> {
    let mut arm_name = String::from("ntuple-d3");
    let mut weights = None;
    let mut board_str = None;
    let mut next = None;
    let mut rise = None;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--arm" => arm_name = value.to_string(),
            "--weights" => weights = Some(value.to_string()),
            "--board" => board_str = Some(value.to_string()),
            "--next" => next = Some(value.parse::<u8>().map_err(|_| "bad --next")?),
            "--rise" => rise = Some(value.parse::<i32>().map_err(|_| "bad --rise")?),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let board_str = board_str.ok_or("--board required")?;
    let next = next.ok_or("--next required")?;
    let rise = rise.ok_or("--rise required")?;
    if !(1..=7).contains(&next) {
        return Err("--next must be 1-7".into());
    }
    if !(1..=5).contains(&rise) {
        return Err("--rise must be 1-5".into());
    }
    let board = Board::from_serialized(&board_str).ok_or("bad --board: not 49 digit characters")?;
    let state = State {
        board,
        next_disc: next,
        score: 0,
        level: 1,
        moves_remaining: rise,
        moves_played: 0,
        game_over: false,
    };

    let load_tables = || -> Result<Arc<Model>, String> {
        let path = weights.as_deref().ok_or("--weights required for this arm")?;
        Ok(Arc::new(Model::load(Path::new(path), false)?))
    };
    let arm = match arm_name.as_str() {
        "ntuple-d3" => Arm::NTupleD3(load_tables()?),
        "direct" => Arm::Direct(load_tables()?),
        "fair-d3" => Arm::FairD3,
        "fair-d4" => Arm::FairD4,
        other => return Err(format!("unknown --arm {other} (ntuple-d3, direct, fair-d3, fair-d4)")),
    };
    let mut player = arm.player();
    let (action, _work, _depth) = player.decide(&state);
    if action < 0 {
        return Err("no legal column".into());
    }
    println!("{action}");
    Ok(())
}
