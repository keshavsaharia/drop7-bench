// Complete-game driver and the population-artifact emission shared by the
// evolution and screen binaries.  The artifact shape is byte-compatible with
// approaches/lifetime-objective/leaf-evolution/evaluate.cpp so the proven
// compare.py statistics (paired bootstrap, Student-t, detection floor) apply
// unchanged.

use drop7_rs::board::BOARD_SIZE;
use drop7_rs::engine::{play_headless_move, FullWaveSink, State};
use drop7_rs::search::{DepthTable, FairLeaf, Leaf, Searcher, SearchParams, TranspositionTable};

use crate::nnue::NnueLeaf;

pub const MOVE_CAP: i32 = 2_000;

/// A leaf that is either the frozen fair evaluator (the control arm) or an
/// NNUE candidate.  Evaluation binaries are monomorphic over this enum; the
/// per-call branch is predictable and irrelevant next to NNUE inference.
pub enum EvalLeaf {
    Fair(FairLeaf),
    Nnue(NnueLeaf),
}

impl Leaf for EvalLeaf {
    #[inline]
    fn value(&mut self, state: &State) -> f64 {
        match self {
            EvalLeaf::Fair(leaf) => leaf.value(state),
            EvalLeaf::Nnue(leaf) => leaf.value(state),
        }
    }
}

/// One (individual, game) unit of work for the parallel evaluator.
#[derive(Clone, Copy)]
pub struct EvalTask {
    pub individual: usize,
    pub seed: u32,
}

/// Evaluate (individual, seed) tasks over `threads` workers with an atomic
/// cursor.  Each game is computed by exactly one worker with a fresh searcher
/// and table (choose_action clears the table per decision, so a fresh table
/// per game loses nothing and keeps peak memory at one searcher per worker).
/// Results are independent of the worker count and the returned vector is in
/// task order regardless of completion order — the worker-count-independence
/// gate relies on exactly this.
pub fn evaluate_tasks(
    leaf_for: &(dyn Fn(usize) -> EvalLeaf + Sync),
    params: &SearchParams,
    table_entries: usize,
    tasks: &[EvalTask],
    threads: usize,
    move_cap: i32,
) -> Vec<GameRecord> {
    let cursor = std::sync::atomic::AtomicUsize::new(0);
    let results: Vec<std::sync::Mutex<Option<GameRecord>>> =
        tasks.iter().map(|_| std::sync::Mutex::new(None)).collect();
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| {
                loop {
                    let task_index =
                        cursor.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    if task_index >= tasks.len() {
                        break;
                    }
                    let task = tasks[task_index];
                    let mut searcher = Searcher::new(
                        *params,
                        leaf_for(task.individual),
                        DepthTable::new(table_entries, 1),
                    );
                    let record = play_game(&mut searcher, task.seed, move_cap, params.depth);
                    *results[task_index].lock().unwrap() = Some(record);
                }
            });
        }
    });
    results
        .into_iter()
        .map(|slot| slot.into_inner().unwrap().expect("every task evaluated"))
        .collect()
}

#[derive(Clone)]
pub struct GameRecord {
    pub seed: u32,
    pub score: i64,
    pub moves: i32,
    pub numbered_cleared: i64,
    pub covers_revealed: i64,
    pub max_chain_depth: i32,
    pub mean_occupied_cells: f64,
    pub censored: bool,
    pub illegal_decisions: i32,
    pub incomplete_decisions: i32,
    pub work: u64,
    pub wall_seconds: f64,
}

/// Play one complete game under the searcher's policy.  `depth` is the
/// configured search depth, used only to count incomplete decisions.
/// Deterministic in the seed and the leaf: identical inputs replay to
/// identical records.
pub fn play_game<L: Leaf, T: TranspositionTable>(
    searcher: &mut Searcher<L, T>,
    seed: u32,
    move_cap: i32,
    depth: i32,
) -> GameRecord {
    let started = std::time::Instant::now();
    let mut state = State::initial_headless(seed);
    let mut sink = FullWaveSink::new();
    let mut numbered_cleared = 0i64;
    let mut covers_revealed = 0i64;
    let mut max_chain_depth = 0i32;
    let mut occupied_sum = 0u64;
    let mut illegal = 0;
    let mut incomplete = 0;
    let mut work = 0u64;
    while !state.game_over && state.moves_played < move_cap {
        let (action, metrics) = searcher.choose_action(&state);
        work += metrics.work;
        if metrics.completed_depth < depth {
            incomplete += 1;
        }
        if action < 0 || !state.board.is_legal(action as usize) {
            illegal += 1;
            break;
        }
        sink.clear();
        if play_headless_move(&mut state, seed, action as usize, &mut sink).is_none() {
            illegal += 1;
            break;
        }
        for wave in sink.waves.iter().take(sink.count) {
            numbered_cleared += wave.cleared as i64;
            covers_revealed += wave.revealed as i64;
            max_chain_depth = max_chain_depth.max(wave.depth);
        }
        let occupied: usize = (0..BOARD_SIZE).map(|c| state.board.height(c)).sum();
        occupied_sum += occupied as u64;
    }
    let moves = state.moves_played;
    GameRecord {
        seed,
        score: state.score,
        moves,
        numbered_cleared,
        covers_revealed,
        max_chain_depth,
        mean_occupied_cells: if moves > 0 {
            occupied_sum as f64 / moves as f64
        } else {
            0.0
        },
        censored: !state.game_over && moves >= move_cap,
        illegal_decisions: illegal,
        incomplete_decisions: incomplete,
        work,
        wall_seconds: started.elapsed().as_secs_f64(),
    }
}

pub struct Individual {
    pub name: String,
    pub games: Vec<GameRecord>,
}

fn json_escape(text: &str) -> String {
    text.replace('\\', "\\\\").replace('"', "\\\"")
}

/// The per-game row shape compare.py reads.
fn game_json(game: &GameRecord) -> String {
    format!(
        "{{\"seedHex\":\"0x{:08x}\",\"score\":{},\"moves\":{},\"numberedCleared\":{},\"coversRevealed\":{},\"maxChainDepth\":{},\"meanOccupiedCells\":{},\"censored\":{},\"illegalDecisions\":{},\"incompleteDecisions\":{},\"work\":{},\"wallSeconds\":{}}}",
        game.seed,
        game.score,
        game.moves,
        game.numbered_cleared,
        game.covers_revealed,
        game.max_chain_depth,
        game.mean_occupied_cells,
        game.censored,
        game.illegal_decisions,
        game.incomplete_decisions,
        game.work,
        game.wall_seconds,
    )
}

/// Serialise one evaluated population (or screen line-up) as a
/// drop7-leaf-evolution population artifact.
pub fn population_artifact_json(
    config_json: &str,
    seed_start: u32,
    individuals: &[Individual],
) -> String {
    let mut out = String::new();
    out.push_str("{\n  \"config\": ");
    out.push_str(config_json);
    out.push_str(",\n  \"seedStartHex\": ");
    out.push_str(&format!("\"0x{seed_start:08x}\",\n"));
    out.push_str("  \"individuals\": [\n");
    for (index, individual) in individuals.iter().enumerate() {
        let total_moves: i64 = individual.games.iter().map(|g| g.moves as i64).sum();
        let clears: i64 = individual.games.iter().map(|g| g.numbered_cleared).sum();
        let reveals: i64 = individual.games.iter().map(|g| g.covers_revealed).sum();
        let censored = individual.games.iter().filter(|g| g.censored).count();
        let illegal: i32 = individual.games.iter().map(|g| g.illegal_decisions).sum();
        let incomplete: i32 = individual.games.iter().map(|g| g.incomplete_decisions).sum();
        out.push_str("    {\"name\": \"");
        out.push_str(&json_escape(&individual.name));
        out.push_str("\", \"games\": [");
        let rows: Vec<String> = individual.games.iter().map(game_json).collect();
        out.push_str(&rows.join(", "));
        out.push_str(&format!(
            "], \"numberedClearsPerMove\": {}, \"coverRevealsPerMove\": {}, \"censoredGames\": {}, \"incompleteDecisions\": {}, \"illegalDecisions\": {}}}",
            clears as f64 / total_moves.max(1) as f64,
            reveals as f64 / total_moves.max(1) as f64,
            censored,
            incomplete,
            illegal,
        ));
        if index + 1 < individuals.len() {
            out.push(',');
        }
        out.push('\n');
    }
    out.push_str("  ]\n}\n");
    out
}
