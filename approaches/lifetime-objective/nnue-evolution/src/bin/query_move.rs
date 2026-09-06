// One-shot decision query for any NNUE candidate this crate produces. Given
// a single public board state on the command line, prints the internal
// column index (0-6) the deployed depth-3 seven-stratum search chooses with
// that candidate as its leaf, using the exact same configuration as
// evolve.rs/screen.rs (deployment_params(), DEPLOYMENT_TABLE).
//
// This is a synchronous bridge, not a D7P server (see docs/d7p-protocol.md):
// it answers one query and exits, which lets an external harness whose
// interface is a synchronous function of the public state (for example the
// TypeScript benchmark playground's BenchPolicy.chooseColumn) call it
// per-move via a plain subprocess, with no interactive stdin/stdout protocol
// to implement or debug. Used by src/bench/bench-external.ts to run
// a frozen Rust-only candidate through a scripted round without porting the
// search or the NNUE leaf to TypeScript. Never used for research-tier
// evidence: whatever calls this must stay inside the benchmark playground
// (see .agents/skills/drop7-benchmark-playground/SKILL.md) and never a
// research seed lease.
//
// Usage: query_move --weights FILE --board <49 chars> --next <1-7> --rise <1-5>
//
//   --weights  path to a candidate-weights.bin (or any Nnue::save output)
//   --board    49-character serialize() string, row-major from the top:
//              '0' empty, '1'-'7' numbered, '8' solid gray, '9' cracked gray
//              (identical to the reference engines' serializeBoard/board.join(""))
//   --next     the visible next disc, 1-7
//   --rise     moves remaining before the next row rise, 1-5

use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::search::{DepthTable, Searcher};

fn main() -> Result<(), String> {
    let mut weights = None;
    let mut board_str = None;
    let mut next = None;
    let mut rise = None;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--weights" => weights = Some(value.to_string()),
            "--board" => board_str = Some(value.to_string()),
            "--next" => next = Some(value.parse::<u8>().map_err(|_| "bad --next")?),
            "--rise" => rise = Some(value.parse::<i32>().map_err(|_| "bad --rise")?),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let weights = weights.ok_or("--weights required")?;
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
    let net = Nnue::load(std::path::Path::new(&weights))?;
    let params = deployment_params();
    let mut searcher = Searcher::new(params, NnueLeaf { net }, DepthTable::new(DEPLOYMENT_TABLE, 1));
    let (action, _metrics) = searcher.choose_action(&state);
    println!("{action}");
    Ok(())
}
