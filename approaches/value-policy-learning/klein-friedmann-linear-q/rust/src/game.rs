// Complete-game driver, parallel cohort evaluation and the per-game JSON row.
// Deterministic in (policy, seed): identical inputs replay to identical rows,
// and the parallel evaluator returns rows in cohort order whatever the
// completion order, so worker count cannot change the artifact.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;
use std::time::Instant;

use drop7_rs::engine::{play_headless_move, FullWaveSink, State};

use crate::policy::Policy;
use crate::view::PublicView;

pub const MOVE_CAP: i32 = 2_000;

#[derive(Clone, Debug, PartialEq)]
pub struct GameRecord {
    pub ordinal: u32,
    pub seed: u32,
    pub score: i64,
    pub moves: i32,
    pub numbered_cleared: i64,
    pub covers_revealed: i64,
    pub waves: i64,
    pub chain_depth_sum: i64,
    pub max_chain_depth: i32,
    pub censored: bool,
    pub illegal_decisions: i32,
    pub incomplete_decisions: i32,
    pub work: u64,
    pub wall_seconds: f64,
    pub trajectory_checksum: u64,
}

#[inline]
fn fnv(mut hash: u64, value: u64) -> u64 {
    for byte in value.to_le_bytes() {
        hash ^= byte as u64;
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

/// Play one complete headless game under `policy`.
pub fn play_game(policy: &mut dyn Policy, ordinal: u32, seed: u32, move_cap: i32) -> GameRecord {
    let started = Instant::now();
    policy.start_game(ordinal);
    let mut state = State::initial_headless(seed);
    let mut sink = FullWaveSink::new();
    let mut numbered_cleared = 0i64;
    let mut covers_revealed = 0i64;
    let mut waves = 0i64;
    let mut chain_depth_sum = 0i64;
    let mut max_chain_depth = 0i32;
    let mut illegal = 0;
    let mut incomplete = 0;
    let mut work = 0u64;
    let mut checksum = 0xcbf2_9ce4_8422_2325u64;
    while !state.game_over && state.moves_played < move_cap {
        let view = PublicView::from_state(&state);
        let decision = policy.choose(&view);
        work += decision.work;
        if !decision.complete {
            incomplete += 1;
        }
        if !view.is_legal(decision.column) {
            illegal += 1;
            break;
        }
        sink.clear();
        if play_headless_move(&mut state, seed, decision.column, &mut sink).is_none() {
            illegal += 1;
            break;
        }
        for wave in sink.waves.iter().take(sink.count) {
            numbered_cleared += wave.cleared as i64;
            covers_revealed += wave.revealed as i64;
            waves += 1;
            chain_depth_sum += wave.depth as i64;
            max_chain_depth = max_chain_depth.max(wave.depth);
        }
        checksum = fnv(checksum, decision.column as u64);
        checksum = fnv(checksum, state.score as u64);
        checksum = fnv(checksum, state.next_disc as u64);
    }
    GameRecord {
        ordinal,
        seed,
        score: state.score,
        moves: state.moves_played,
        numbered_cleared,
        covers_revealed,
        waves,
        chain_depth_sum,
        max_chain_depth,
        censored: !state.game_over && state.moves_played >= move_cap,
        illegal_decisions: illegal,
        incomplete_decisions: incomplete,
        work,
        wall_seconds: started.elapsed().as_secs_f64(),
        trajectory_checksum: checksum,
    }
}

/// Evaluate one arm over `seeds` with `threads` workers.  Each worker builds
/// its own policy instance; rows come back in cohort order.
pub fn evaluate_arm(
    make: &(dyn Fn() -> Box<dyn Policy> + Sync),
    seeds: &[u32],
    threads: usize,
    move_cap: i32,
) -> Vec<GameRecord> {
    let cursor = AtomicUsize::new(0);
    let slots: Vec<Mutex<Option<GameRecord>>> = seeds.iter().map(|_| Mutex::new(None)).collect();
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            scope.spawn(|| {
                let mut policy = make();
                loop {
                    let index = cursor.fetch_add(1, Ordering::Relaxed);
                    if index >= seeds.len() {
                        break;
                    }
                    let record = play_game(policy.as_mut(), index as u32, seeds[index], move_cap);
                    *slots[index].lock().unwrap() = Some(record);
                }
            });
        }
    });
    slots
        .into_iter()
        .map(|slot| slot.into_inner().unwrap().expect("every game evaluated"))
        .collect()
}

/// One per-game JSON row in the shape of research/schemas/game-result-v1
/// (the run-level identifiers are added by the summariser).
pub fn game_json(g: &GameRecord) -> String {
    let mean_chain_depth = if g.waves > 0 {
        g.chain_depth_sum as f64 / g.waves as f64
    } else {
        0.0
    };
    format!(
        "{{\"cohortOrdinal\":{},\"seedHex\":\"0x{:08x}\",\"score\":{},\"moves\":{},\"censored\":{},\"numberedClears\":{},\"coveredReveals\":{},\"meanChainDepth\":{},\"maximumChainDepth\":{},\"illegalDecisions\":{},\"incompleteDecisions\":{},\"runnerFailures\":0,\"logicalWork\":{},\"wallSeconds\":{},\"trajectoryChecksum\":\"{:016x}\"}}",
        g.ordinal,
        g.seed,
        g.score,
        g.moves,
        g.censored,
        g.numbered_cleared,
        g.covers_revealed,
        mean_chain_depth,
        g.max_chain_depth,
        g.illegal_decisions,
        g.incomplete_decisions,
        g.work,
        g.wall_seconds,
        g.trajectory_checksum,
    )
}

pub struct ArmSummary {
    pub games: usize,
    pub mean_score: f64,
    pub mean_moves: f64,
    pub censored: usize,
    pub illegal: i32,
    pub incomplete: i32,
    pub wall_seconds: f64,
}

pub fn summarize(rows: &[GameRecord]) -> ArmSummary {
    let n = rows.len().max(1) as f64;
    ArmSummary {
        games: rows.len(),
        mean_score: rows.iter().map(|g| g.score as f64).sum::<f64>() / n,
        mean_moves: rows.iter().map(|g| g.moves as f64).sum::<f64>() / n,
        censored: rows.iter().filter(|g| g.censored).count(),
        illegal: rows.iter().map(|g| g.illegal_decisions).sum(),
        incomplete: rows.iter().map(|g| g.incomplete_decisions).sum(),
        wall_seconds: rows.iter().map(|g| g.wall_seconds).sum(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::policy::{CenterFirst, RandomLegal};

    #[test]
    fn replay_is_deterministic_and_legal() {
        let mut a = CenterFirst;
        let mut b = CenterFirst;
        let mut ga = play_game(&mut a, 0, 0xa527_8000, 300);
        let mut gb = play_game(&mut b, 0, 0xa527_8000, 300);
        // Wall time is the one field allowed to differ between replays.
        ga.wall_seconds = 0.0;
        gb.wall_seconds = 0.0;
        assert_eq!(ga, gb);
        assert_eq!(ga.illegal_decisions, 0);
        assert!(ga.moves > 0);
    }

    #[test]
    fn worker_count_does_not_change_rows() {
        let seeds: Vec<u32> = (0..12).map(|i| 0xa527_8000 + i).collect();
        let make = || -> Box<dyn Policy> { Box::new(RandomLegal::new(0x1234_5678)) };
        let one = evaluate_arm(&make, &seeds, 1, 300);
        let many = evaluate_arm(&make, &seeds, 4, 300);
        for (x, y) in one.iter().zip(many.iter()) {
            assert_eq!(x.trajectory_checksum, y.trajectory_checksum);
            assert_eq!(x.score, y.score);
            assert_eq!(x.moves, y.moves);
        }
    }
}
