// Sibling-complete root panel from complete fair-d4s7 games.
//
// Every root the reference search visits is recorded in the canonical frame
// with: the legal columns in COLUMN_ORDER, exact drop7-rs column values at
// depths 1..=D (values are table-independent), the action played (the depth-D
// argmax, which is what Searcher::choose_action plays when its budget
// completes), the 32 one-ply features and exact d1 value of every legal
// sibling under the depth-D chance scenarios, and, once the game ends, the
// remaining lifetime.  The trajectory is driven by the unchanged drop7-rs
// search, so the games themselves are ordinary fair-d4s7 games.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;
use std::time::Instant;

use drop7_rs::board::BOARD_SIZE;
use drop7_rs::engine::{play_headless_move, FullWaveSink, State};
use drop7_rs::leaf::LeafScratch;
use drop7_rs::search::{canonical_state, DepthTable, FairLeaf, SearchParams, Searcher};

use crate::oneply::{oneply, OnePly};

pub struct PanelRoot {
    pub game: u32,
    pub seed: u32,
    pub move_index: i32,
    /// Canonical board, serialized row-major with row 0 at the top.
    pub board: String,
    pub next_disc: u8,
    pub moves_remaining: i32,
    pub mirrored: bool,
    pub legal: Vec<usize>,
    /// values[d - 1][i] is the depth-d value of legal[i].
    pub values: Vec<Vec<f64>>,
    /// Canonical-frame column played.
    pub action: usize,
    pub score_before: i64,
    pub siblings: Vec<OnePly>,
    pub remaining_lifetime: i32,
    pub final_score: i64,
    pub final_moves: i32,
}

pub struct PanelGame {
    pub ordinal: u32,
    pub seed: u32,
    pub score: i64,
    pub moves: i32,
    pub censored: bool,
    pub wall_seconds: f64,
    pub trajectory_checksum: u64,
    pub roots: Vec<PanelRoot>,
}

#[inline]
fn fnv(mut hash: u64, value: u64) -> u64 {
    for byte in value.to_le_bytes() {
        hash ^= byte as u64;
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

pub fn deployment_params(depth: i32, strata: i32) -> SearchParams {
    SearchParams {
        depth,
        chance_samples: strata,
        terminal_utility: -1_000_000.0,
        maximum_work: u64::MAX >> 1,
        policy_seed: 0xd707_5eed,
    }
}

pub fn play_panel_game(
    ordinal: u32,
    seed: u32,
    depth: i32,
    strata: i32,
    table_entries: usize,
    move_cap: i32,
) -> PanelGame {
    let started = Instant::now();
    let params = deployment_params(depth, strata);
    let mut searcher = Searcher::new(params, FairLeaf::default(), DepthTable::new(table_entries, 1));
    let mut scratch = LeafScratch::default();
    let mut state = State::initial_headless(seed);
    let mut sink = FullWaveSink::new();
    let mut roots: Vec<PanelRoot> = Vec::new();
    let mut checksum = 0xcbf2_9ce4_8422_2325u64;
    while !state.game_over && state.moves_played < move_cap {
        let (canonical, mirrored) = canonical_state(&state);
        searcher.begin_parallel_decision();
        let mut legal: Vec<usize> = Vec::new();
        let mut values: Vec<Vec<f64>> = Vec::with_capacity(depth as usize);
        let mut action = -1i32;
        for d in 1..=depth {
            let (column_values, chosen) = searcher.column_values(&state, d);
            if d == 1 {
                legal = column_values.iter().map(|(c, _)| *c).collect();
            } else {
                debug_assert!(legal.iter().eq(column_values.iter().map(|(c, _)| c)));
            }
            values.push(column_values.iter().map(|(_, v)| *v).collect());
            if d == depth {
                action = chosen;
            }
        }
        assert!(action >= 0, "the reference search found no legal column");
        let canonical_action = if mirrored {
            BOARD_SIZE - 1 - action as usize
        } else {
            action as usize
        };
        let siblings: Vec<OnePly> = legal
            .iter()
            .map(|&column| oneply(&canonical, column, &params, depth, &mut scratch))
            .collect();
        roots.push(PanelRoot {
            game: ordinal,
            seed,
            move_index: state.moves_played,
            board: canonical.board.serialize(),
            next_disc: canonical.next_disc,
            moves_remaining: canonical.moves_remaining,
            mirrored,
            legal,
            values,
            action: canonical_action,
            score_before: state.score,
            siblings,
            remaining_lifetime: 0,
            final_score: 0,
            final_moves: 0,
        });
        sink.clear();
        play_headless_move(&mut state, seed, action as usize, &mut sink)
            .expect("the reference search plays a legal column");
        checksum = fnv(checksum, action as u64);
        checksum = fnv(checksum, state.score as u64);
        checksum = fnv(checksum, state.next_disc as u64);
    }
    let total = state.moves_played;
    for root in roots.iter_mut() {
        root.remaining_lifetime = total - root.move_index;
        root.final_score = state.score;
        root.final_moves = total;
    }
    PanelGame {
        ordinal,
        seed,
        score: state.score,
        moves: total,
        censored: !state.game_over && total >= move_cap,
        wall_seconds: started.elapsed().as_secs_f64(),
        trajectory_checksum: checksum,
        roots,
    }
}

/// One finished game as a self-contained part file: the game row first,
/// then one root line each, written atomically (temporary file + rename) so
/// a part is either complete or absent.
pub fn write_part(dir: &str, game: &PanelGame) -> std::io::Result<()> {
    let path = format!("{dir}/game-{:04}.ndjson", game.ordinal);
    let tmp = format!("{path}.tmp");
    let mut text = game_json(game);
    text.push('\n');
    for root in &game.roots {
        text.push_str(&root_json(root));
        text.push('\n');
    }
    std::fs::write(&tmp, text)?;
    std::fs::rename(&tmp, &path)
}

/// Read a part file back into the game row and root lines (raw JSON lines);
/// None when the part does not exist.
pub fn read_part(dir: &str, ordinal: u32) -> Option<(String, Vec<String>)> {
    let path = format!("{dir}/game-{ordinal:04}.ndjson");
    let text = std::fs::read_to_string(&path).ok()?;
    let mut lines = text.lines().map(|l| l.to_string());
    let game_row = lines.next()?;
    Some((game_row, lines.filter(|l| !l.trim().is_empty()).collect()))
}

/// Build the panel for `seeds` with `threads` workers.  Each finished game
/// is written to `parts_dir` at once; games whose part already exists are
/// not replayed, so an interrupted build resumes where it stopped.  Returns
/// (game row, root lines) per seed in cohort order.
pub fn build_panel_resumable(
    seeds: &[u32],
    depth: i32,
    strata: i32,
    table_entries: usize,
    move_cap: i32,
    threads: usize,
    parts_dir: &str,
    progress: bool,
) -> Vec<(String, Vec<String>)> {
    std::fs::create_dir_all(parts_dir).expect("create parts dir");
    let pending: Vec<usize> = (0..seeds.len())
        .filter(|&i| read_part(parts_dir, i as u32).is_none())
        .collect();
    if progress {
        eprintln!("panel: {} of {} games already on disk", seeds.len() - pending.len(), seeds.len());
    }
    let cursor = AtomicUsize::new(0);
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| loop {
                let slot = cursor.fetch_add(1, Ordering::Relaxed);
                if slot >= pending.len() {
                    break;
                }
                let index = pending[slot];
                let game = play_panel_game(index as u32, seeds[index], depth, strata, table_entries, move_cap);
                write_part(parts_dir, &game).expect("write part");
                if progress {
                    eprintln!(
                        "panel game {index} seed 0x{:08x}: {} moves, score {}, {} roots, {:.1}s",
                        seeds[index],
                        game.moves,
                        game.score,
                        game.roots.len(),
                        game.wall_seconds
                    );
                }
            });
        }
    });
    (0..seeds.len())
        .map(|i| read_part(parts_dir, i as u32).expect("every part written"))
        .collect()
}

/// Build the panel for `seeds` with `threads` workers; games come back in
/// cohort order whatever the completion order.  (In-memory variant used by
/// tests; the binary uses `build_panel_resumable`.)
pub fn build_panel(
    seeds: &[u32],
    depth: i32,
    strata: i32,
    table_entries: usize,
    move_cap: i32,
    threads: usize,
    progress: bool,
) -> Vec<PanelGame> {
    let cursor = AtomicUsize::new(0);
    let slots: Vec<Mutex<Option<PanelGame>>> = seeds.iter().map(|_| Mutex::new(None)).collect();
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| loop {
                let index = cursor.fetch_add(1, Ordering::Relaxed);
                if index >= seeds.len() {
                    break;
                }
                let game = play_panel_game(index as u32, seeds[index], depth, strata, table_entries, move_cap);
                if progress {
                    eprintln!(
                        "panel game {index} seed 0x{:08x}: {} moves, score {}, {} roots, {:.1}s",
                        seeds[index],
                        game.moves,
                        game.score,
                        game.roots.len(),
                        game.wall_seconds
                    );
                }
                *slots[index].lock().unwrap() = Some(game);
            });
        }
    });
    slots
        .into_iter()
        .map(|slot| slot.into_inner().unwrap().expect("every game played"))
        .collect()
}

fn floats(values: &[f64]) -> String {
    values.iter().map(|v| format!("{v:?}")).collect::<Vec<_>>().join(",")
}

/// One NDJSON line per root.
pub fn root_json(root: &PanelRoot) -> String {
    let mut siblings: Vec<String> = Vec::with_capacity(root.siblings.len());
    for (column, one) in root.legal.iter().zip(root.siblings.iter()) {
        siblings.push(format!(
            "{{\"col\":{column},\"d1\":{:?},\"f\":[{}]}}",
            one.d1_value,
            floats(&one.values)
        ));
    }
    let mut values: Vec<String> = Vec::with_capacity(root.values.len());
    for (d, v) in root.values.iter().enumerate() {
        values.push(format!("\"v{}\":[{}]", d + 1, floats(v)));
    }
    format!(
        "{{\"game\":{},\"seed\":\"0x{:08x}\",\"move\":{},\"board\":\"{}\",\"next\":{},\"movesRemaining\":{},\"mirrored\":{},\"legal\":[{}],{},\"action\":{},\"scoreBefore\":{},\"remainingLifetime\":{},\"finalScore\":{},\"finalMoves\":{},\"siblings\":[{}]}}",
        root.game,
        root.seed,
        root.move_index,
        root.board,
        root.next_disc,
        root.moves_remaining,
        root.mirrored,
        root.legal.iter().map(|c| c.to_string()).collect::<Vec<_>>().join(","),
        values.join(","),
        root.action,
        root.score_before,
        root.remaining_lifetime,
        root.final_score,
        root.final_moves,
        siblings.join(",")
    )
}

pub fn game_json(game: &PanelGame) -> String {
    format!(
        "{{\"cohortOrdinal\":{},\"seedHex\":\"0x{:08x}\",\"score\":{},\"moves\":{},\"censored\":{},\"roots\":{},\"wallSeconds\":{},\"trajectoryChecksum\":\"{:016x}\"}}",
        game.ordinal,
        game.seed,
        game.score,
        game.moves,
        game.censored,
        game.roots.len(),
        game.wall_seconds,
        game.trajectory_checksum
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn panel_game_is_deterministic_and_sibling_complete() {
        let a = play_panel_game(0, 0xa527_8005, 2, 5, 1024, 12);
        let b = play_panel_game(0, 0xa527_8005, 2, 5, 1024, 12);
        assert_eq!(a.trajectory_checksum, b.trajectory_checksum);
        assert_eq!(a.roots.len(), b.roots.len());
        for (x, y) in a.roots.iter().zip(b.roots.iter()) {
            assert_eq!(x.board, y.board);
            assert_eq!(x.legal, y.legal);
            assert_eq!(x.siblings.len(), x.legal.len());
            assert_eq!(x.values.len(), 2);
            assert_eq!(x.values[1].len(), x.legal.len());
            for (u, v) in x.values[1].iter().zip(y.values[1].iter()) {
                assert_eq!(u.to_bits(), v.to_bits());
            }
            assert!(x.legal.contains(&x.action));
            assert_eq!(x.remaining_lifetime, a.moves - x.move_index);
        }
        assert!(root_json(&a.roots[0]).starts_with("{\"game\":0,"));
    }
}
