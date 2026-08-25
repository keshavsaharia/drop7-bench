// Game state and move resolution, statement-for-statement from
// src/core/native/engine.hpp (and the sampled variant in
// public-behavior.hpp).  Generic over the random source and the wave sink so
// the search pays for no wave storage it does not read (the C++ fast
// engine's MinimalWaveSink/FullWaveSink split).

use crate::board::{Board, Scan, CLEAR_BONUS, LEVEL_BONUS, MOVES_PER_LEVEL};
use crate::tables::wave_score;
use crate::rng::{headless_disc, mix32, Mulberry32, Random, REVEAL_DOMAIN};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct State {
    pub board: Board,
    pub next_disc: u8,
    pub score: i64,
    pub level: i32,
    pub moves_remaining: i32,
    pub moves_played: i32,
    pub game_over: bool,
}

impl State {
    pub fn initial_headless(seed: u32) -> State {
        State {
            board: Board::initial(),
            next_disc: headless_disc(seed, 0),
            score: 0,
            level: 1,
            moves_remaining: MOVES_PER_LEVEL,
            moves_played: 0,
            game_over: false,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Wave {
    pub depth: i32,
    pub cleared: i32,
    pub revealed: i32,
    pub points: i64,
}

/// The search reads only `count` and `last_depth` from a move's wave list;
/// the trajectory gate wants the full list.  Same split as the C++ sinks.
pub trait WaveSink {
    fn push(&mut self, wave: Wave);
    fn is_empty(&self) -> bool;
    fn back_depth(&self) -> i32;
}

#[derive(Default, Clone, Copy)]
pub struct MinimalWaveSink {
    pub count: i32,
    pub last_depth: i32,
}

impl WaveSink for MinimalWaveSink {
    #[inline(always)]
    fn push(&mut self, wave: Wave) {
        self.count += 1;
        self.last_depth = wave.depth;
    }
    #[inline(always)]
    fn is_empty(&self) -> bool {
        self.count == 0
    }
    #[inline(always)]
    fn back_depth(&self) -> i32 {
        self.last_depth
    }
}

/// A move can pop at most 49 discs and every wave clears at least one, so a
/// single move produces at most 49 waves before the rise and 49 after it;
/// 128 is a hard upper bound with margin, as in the C++ fast engine.
pub const MAX_WAVES_PER_MOVE: usize = 128;

pub struct FullWaveSink {
    pub waves: [Wave; MAX_WAVES_PER_MOVE],
    pub count: usize,
}

impl FullWaveSink {
    pub fn new() -> FullWaveSink {
        FullWaveSink {
            waves: [Wave { depth: 0, cleared: 0, revealed: 0, points: 0 };
                MAX_WAVES_PER_MOVE],
            count: 0,
        }
    }
    pub fn clear(&mut self) {
        self.count = 0;
    }
}

impl Default for FullWaveSink {
    fn default() -> Self {
        Self::new()
    }
}

impl WaveSink for FullWaveSink {
    #[inline]
    fn push(&mut self, wave: Wave) {
        assert!(self.count < MAX_WAVES_PER_MOVE, "wave capacity exceeded");
        self.waves[self.count] = wave;
        self.count += 1;
    }
    #[inline(always)]
    fn is_empty(&self) -> bool {
        self.count == 0
    }
    #[inline(always)]
    fn back_depth(&self) -> i32 {
        self.waves[self.count - 1].depth
    }
}

#[derive(Clone, Copy, Debug)]
pub struct MoveResult {
    pub state: State,
    pub score_delta: i64,
    pub cleared_board: bool,
    pub level_advanced: bool,
}

/// Resolve the cascade on `board`, drawing reveals from `random`, starting
/// at `starting_depth`.  Mirrors resolveCascadeSampled / resolveCascadeFast.
#[inline]
pub fn resolve_cascade<R: Random, S: WaveSink>(
    board: &mut Board,
    random: &mut R,
    starting_depth: i32,
    score: &mut i64,
    sink: &mut S,
) {
    let mut depth = starting_depth;
    loop {
        let scan: Scan = board.scan();
        let (popping, popper_count, popped_columns) = board.find_poppers(&scan);
        if popper_count == 0 {
            return;
        }
        let reveal_count = board.clear_wave(&scan, popping, popped_columns, random);
        let points = popper_count as i64 * wave_score(depth);
        *score += points;
        sink.push(Wave {
            depth,
            cleared: popper_count as i32,
            revealed: reveal_count as i32,
            points,
        });
        depth += 1;
    }
}

/// Play one move.  Mirrors playMoveSampled / playMoveFast: cascade, clear
/// bonus, rise on the five-move boundary with its continuation cascade,
/// terminal checks, next-disc draw.  Returns None for an illegal column or a
/// finished game.
pub fn play_move_sampled<R: Random, S: WaveSink>(
    state: &State,
    column: usize,
    random: &mut R,
    sink: &mut S,
) -> Option<MoveResult> {
    if state.game_over {
        return None;
    }
    let mut board = state.board;
    if !board.place_disc(column, state.next_disc) {
        return None;
    }

    let mut score_delta: i64 = 0;
    resolve_cascade(&mut board, random, 1, &mut score_delta, sink);
    let mut cleared_board = board.is_empty();
    if cleared_board {
        score_delta += CLEAR_BONUS;
    }

    let mut level = state.level;
    let mut moves_remaining = state.moves_remaining - 1;
    let mut game_over = false;
    let mut level_advanced = false;
    if moves_remaining == 0 {
        if !board.raise_covered_row() {
            game_over = true;
        } else {
            level_advanced = true;
            level += 1;
            moves_remaining = MOVES_PER_LEVEL;
            score_delta += LEVEL_BONUS;
            let next_depth = if sink.is_empty() { 1 } else { sink.back_depth() + 1 };
            let mut level_score: i64 = 0;
            resolve_cascade(&mut board, random, next_depth, &mut level_score, sink);
            score_delta += level_score;
            if board.is_empty() {
                score_delta += CLEAR_BONUS;
                cleared_board = true;
            }
        }
    }

    if !game_over && !board.cols.iter().any(|&c| c & 0x0F00_0000 == 0) {
        // No legal column remains (every column's top row is occupied).
        game_over = true;
    }

    Some(MoveResult {
        state: State {
            board,
            next_disc: if game_over {
                state.next_disc
            } else {
                random.next_disc()
            },
            score: state.score + score_delta,
            level,
            moves_remaining,
            moves_played: state.moves_played + 1,
            game_over,
        },
        score_delta,
        cleared_board,
        level_advanced,
    })
}

/// The real-game driver, bit-identical to drop7::playHeadlessMove: reveals
/// come from a per-move Mulberry32 seeded from the game seed and move number,
/// and the next disc comes from the separate headless disc stream.
pub fn play_headless_move<S: WaveSink>(
    state: &mut State,
    game_seed: u32,
    column: usize,
    sink: &mut S,
) -> Option<MoveResult> {
    let reveal_seed = mix32(
        game_seed
            ^ (state.moves_played as u32).wrapping_add(1).wrapping_mul(0x85eb_ca6b)
            ^ REVEAL_DOMAIN,
    );
    let mut random = Mulberry32::new(reveal_seed);
    let result = play_move_sampled(state, column, &mut random, sink)?;
    *state = result.state;
    if !state.game_over {
        state.next_disc = headless_disc(game_seed, state.moves_played);
    }
    Some(result)
}

/// The center-first fallback policy, same column order as the references.
pub fn center_first_move(board: &Board) -> Option<usize> {
    const ORDER: [usize; 7] = [3, 2, 4, 1, 5, 0, 6];
    ORDER.into_iter().find(|&c| board.is_legal(c))
}

/// The TypeScript driver's initial state: createGame draws the first next
/// disc from the game's single long-lived stream.
pub fn initial_ts_state(random: &mut Mulberry32) -> State {
    State {
        board: Board::initial(),
        next_disc: random.next_disc(),
        score: 0,
        level: 1,
        moves_remaining: MOVES_PER_LEVEL,
        moves_played: 0,
        game_over: false,
    }
}

/// The TypeScript driver: playMove draws reveals and the next disc from the
/// game's single long-lived Mulberry32 stream (engine.ts's seededRandom).
/// play_move_sampled already matches that draw pattern exactly, so this is
/// just the stream plumbing for the three-way trajectory cross-check.
pub fn play_ts_move<S: WaveSink>(
    state: &mut State,
    random: &mut Mulberry32,
    column: usize,
    sink: &mut S,
) -> Option<MoveResult> {
    let result = play_move_sampled(state, column, random, sink)?;
    *state = result.state;
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn first_move_of_seed_one_is_deterministic() {
        let mut state = State::initial_headless(1);
        let mut sink = FullWaveSink::new();
        let column = center_first_move(&state.board).unwrap();
        let result = play_headless_move(&mut state, 1, column, &mut sink).unwrap();
        assert_eq!(state.moves_played, 1);
        assert!(!state.game_over);
        assert_eq!(result.state.score, state.score);
        // Replaying must reproduce the identical state.
        let mut again = State::initial_headless(1);
        let mut sink2 = FullWaveSink::new();
        play_headless_move(&mut again, 1, column, &mut sink2).unwrap();
        assert_eq!(again, state);
        assert_eq!(sink.count, sink2.count);
    }

    #[test]
    fn five_moves_advance_level() {
        let mut state = State::initial_headless(7);
        let mut sink = FullWaveSink::new();
        for _ in 0..5 {
            let column = center_first_move(&state.board).unwrap();
            play_headless_move(&mut state, 7, column, &mut sink).unwrap();
        }
        assert_eq!(state.level, 2);
        assert_eq!(state.moves_remaining, MOVES_PER_LEVEL);
        // Level bonus was awarded at the rise.
        assert!(state.score >= LEVEL_BONUS);
    }
}
