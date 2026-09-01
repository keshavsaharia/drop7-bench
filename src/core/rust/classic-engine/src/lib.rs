//! Deterministic Classic/Normal Drop7 mechanics.
//!
//! Random futures are inputs: callers supply the visible next disc, hidden
//! value for a dropped gray disc, and latent values for a risen covered row.
//! This keeps mobile-submission validation exact and lets later research own
//! its sampling policy without coupling it to game transitions.

pub const BOARD_SIZE: usize = 7;
pub const CELL_COUNT: usize = BOARD_SIZE * BOARD_SIZE;
pub const EMPTY: u8 = 0;
pub const SOLID: u8 = 8;
pub const CRACKED: u8 = 9;
pub const FIRST_LEVEL_DROPS: i32 = 30;
pub const LEVEL_BONUS: i64 = 7_000;
pub const CLEAR_BONUS: i64 = 70_000;

pub type Board = [u8; CELL_COUNT];
pub type LatentBoard = [u8; CELL_COUNT];
pub type CoveredRow = [u8; BOARD_SIZE];

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct State {
    pub board: Board,
    pub next_disc: u8,
    pub score: i64,
    pub level: i32,
    pub moves_remaining: i32,
    pub moves_played: i32,
    pub game_over: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Wave {
    pub depth: i32,
    pub cleared: i32,
    pub revealed: i32,
    pub points: i64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MoveResult {
    pub state: State,
    pub latent: LatentBoard,
    pub score_delta: i64,
    pub waves: Vec<Wave>,
    pub cleared_board: bool,
    pub level_advanced: bool,
    pub covered_row_consumed: bool,
}

pub fn drops_for_level(level: i32) -> i32 {
    assert!(level >= 1, "Classic level must be positive");
    (FIRST_LEVEL_DROPS - (level - 1)).max(1)
}

pub fn initial_board() -> Board {
    let mut board = [EMPTY; CELL_COUNT];
    for column in 0..BOARD_SIZE {
        board[index(BOARD_SIZE - 1, column)] = SOLID;
    }
    board
}

pub fn initial_latent(row: CoveredRow) -> LatentBoard {
    assert_covered_row(&row);
    let mut latent = [EMPTY; CELL_COUNT];
    for column in 0..BOARD_SIZE {
        latent[index(BOARD_SIZE - 1, column)] = row[column];
    }
    latent
}

pub fn initial_state(next_disc: u8) -> State {
    assert_droppable(next_disc);
    State {
        board: initial_board(),
        next_disc,
        score: 0,
        level: 1,
        moves_remaining: drops_for_level(1),
        moves_played: 0,
        game_over: false,
    }
}

pub fn legal_columns(board: &Board) -> Vec<usize> {
    (0..BOARD_SIZE)
        .filter(|column| board[index(0, *column)] == EMPTY)
        .collect()
}

/// Play one Classic move against an explicit future.
///
/// `dropped_hidden` must be `Some(1..=7)` exactly when the visible drop is
/// gray. `covered_row` is required only when this move successfully raises a
/// row. `next_disc` is ignored on terminal moves.
pub fn play_move(
    state: &State,
    latent: &LatentBoard,
    column: usize,
    dropped_hidden: Option<u8>,
    covered_row: Option<CoveredRow>,
    next_disc: u8,
) -> Option<MoveResult> {
    if state.game_over || column >= BOARD_SIZE || state.board[index(0, column)] != EMPTY {
        return None;
    }
    assert_droppable(state.next_disc);
    match (state.next_disc, dropped_hidden) {
        (SOLID, Some(value)) => assert_numbered(value),
        (SOLID, None) => panic!("a dropped gray disc needs a hidden value"),
        (_, Some(_)) => panic!("a numbered drop cannot carry a hidden value"),
        (_, None) => {}
    }

    let mut board = state.board;
    let mut hidden = *latent;
    let dropped_index = place_disc(&mut board, column, state.next_disc)?;
    hidden[dropped_index] = dropped_hidden.unwrap_or(EMPTY);

    let mut score_delta = 0;
    let mut waves = Vec::new();
    resolve_cascade(&mut board, &mut hidden, 1, &mut score_delta, &mut waves);
    let mut cleared_board = board.iter().all(|cell| *cell == EMPTY);
    if cleared_board {
        score_delta += CLEAR_BONUS;
    }

    let mut level = state.level;
    let mut moves_remaining = state.moves_remaining - 1;
    let mut game_over = false;
    let mut level_advanced = false;
    let mut covered_row_consumed = false;

    if moves_remaining == 0 {
        if board.iter().take(BOARD_SIZE).any(|cell| *cell != EMPTY) {
            game_over = true;
        } else {
            let row = covered_row.expect("a successful rise needs a covered row");
            assert_covered_row(&row);
            raise_row(&mut board, &mut hidden, row);
            covered_row_consumed = true;
            level_advanced = true;
            level += 1;
            moves_remaining = drops_for_level(level);
            score_delta += LEVEL_BONUS;
            let next_depth = waves.len() as i32 + 1;
            resolve_cascade(
                &mut board,
                &mut hidden,
                next_depth,
                &mut score_delta,
                &mut waves,
            );
            if board.iter().all(|cell| *cell == EMPTY) {
                score_delta += CLEAR_BONUS;
                cleared_board = true;
            }
        }
    }

    if !game_over && legal_columns(&board).is_empty() {
        game_over = true;
    }
    if !game_over {
        assert_droppable(next_disc);
    }

    Some(MoveResult {
        state: State {
            board,
            next_disc: if game_over {
                state.next_disc
            } else {
                next_disc
            },
            score: state.score + score_delta,
            level,
            moves_remaining,
            moves_played: state.moves_played + 1,
            game_over,
        },
        latent: hidden,
        score_delta,
        waves,
        cleared_board,
        level_advanced,
        covered_row_consumed,
    })
}

fn resolve_cascade(
    board: &mut Board,
    latent: &mut LatentBoard,
    starting_depth: i32,
    score: &mut i64,
    waves: &mut Vec<Wave>,
) {
    let mut depth = starting_depth;
    loop {
        let poppers = find_poppers(board);
        if poppers.is_empty() {
            return;
        }
        let mut popping = [false; CELL_COUNT];
        let before = *board;
        for offset in &poppers {
            popping[*offset] = true;
            board[*offset] = EMPTY;
            latent[*offset] = EMPTY;
        }

        let mut reveal_indexes = Vec::new();
        for row in 0..BOARD_SIZE {
            for column in 0..BOARD_SIZE {
                let offset = index(row, column);
                let cell = before[offset];
                if cell != SOLID && cell != CRACKED {
                    continue;
                }
                let mut hits = 0;
                for (row_delta, column_delta) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
                    let neighbor_row = row as isize + row_delta;
                    let neighbor_column = column as isize + column_delta;
                    if inside(neighbor_row, neighbor_column)
                        && popping[index(neighbor_row as usize, neighbor_column as usize)]
                    {
                        hits += 1;
                    }
                }
                if hits == 0 {
                    continue;
                }
                let needed = if cell == SOLID { 2 } else { 1 };
                if hits >= needed {
                    reveal_indexes.push(offset);
                } else {
                    board[offset] = CRACKED;
                }
            }
        }
        for offset in &reveal_indexes {
            let value = latent[*offset];
            assert_numbered(value);
            board[*offset] = value;
            latent[*offset] = EMPTY;
        }
        let points = poppers.len() as i64 * score_for_wave(depth);
        *score += points;
        waves.push(Wave {
            depth,
            cleared: poppers.len() as i32,
            revealed: reveal_indexes.len() as i32,
            points,
        });
        apply_gravity(board, latent);
        depth += 1;
    }
}

fn find_poppers(board: &Board) -> Vec<usize> {
    let mut result = Vec::new();
    for row in 0..BOARD_SIZE {
        for column in 0..BOARD_SIZE {
            let offset = index(row, column);
            let cell = board[offset];
            if !(1..=7).contains(&cell) {
                continue;
            }
            if line_length(board, row, column, false) == cell as usize
                || line_length(board, row, column, true) == cell as usize
            {
                result.push(offset);
            }
        }
    }
    result
}

fn line_length(board: &Board, row: usize, column: usize, vertical: bool) -> usize {
    if board[index(row, column)] == EMPTY {
        return 0;
    }
    let (row_step, column_step) = if vertical { (1, 0) } else { (0, 1) };
    let mut count = 1;
    for direction in [-1, 1] {
        let mut next_row = row as isize + row_step * direction;
        let mut next_column = column as isize + column_step * direction;
        while inside(next_row, next_column)
            && board[index(next_row as usize, next_column as usize)] != EMPTY
        {
            count += 1;
            next_row += row_step * direction;
            next_column += column_step * direction;
        }
    }
    count
}

fn apply_gravity(board: &mut Board, latent: &mut LatentBoard) {
    let before = *board;
    let hidden_before = *latent;
    *board = [EMPTY; CELL_COUNT];
    *latent = [EMPTY; CELL_COUNT];
    for column in 0..BOARD_SIZE {
        let mut destination = BOARD_SIZE - 1;
        for row in (0..BOARD_SIZE).rev() {
            let offset = index(row, column);
            if before[offset] == EMPTY {
                continue;
            }
            let target = index(destination, column);
            board[target] = before[offset];
            latent[target] = hidden_before[offset];
            destination = destination.saturating_sub(1);
        }
    }
}

fn raise_row(board: &mut Board, latent: &mut LatentBoard, row: CoveredRow) {
    let before = *board;
    let hidden_before = *latent;
    *board = [EMPTY; CELL_COUNT];
    *latent = [EMPTY; CELL_COUNT];
    for source_row in 1..BOARD_SIZE {
        for column in 0..BOARD_SIZE {
            board[index(source_row - 1, column)] = before[index(source_row, column)];
            latent[index(source_row - 1, column)] = hidden_before[index(source_row, column)];
        }
    }
    for column in 0..BOARD_SIZE {
        board[index(BOARD_SIZE - 1, column)] = SOLID;
        latent[index(BOARD_SIZE - 1, column)] = row[column];
    }
}

fn place_disc(board: &mut Board, column: usize, disc: u8) -> Option<usize> {
    for row in (0..BOARD_SIZE).rev() {
        let offset = index(row, column);
        if board[offset] == EMPTY {
            board[offset] = disc;
            return Some(offset);
        }
    }
    None
}

fn score_for_wave(depth: i32) -> i64 {
    (7.0 * (depth as f64).powf(2.5)).floor() as i64
}

fn index(row: usize, column: usize) -> usize {
    row * BOARD_SIZE + column
}

fn inside(row: isize, column: isize) -> bool {
    row >= 0 && row < BOARD_SIZE as isize && column >= 0 && column < BOARD_SIZE as isize
}

fn assert_numbered(value: u8) {
    assert!(
        (1..=7).contains(&value),
        "hidden values must be numbered 1..=7"
    );
}

fn assert_droppable(value: u8) {
    assert!((1..=8).contains(&value), "Classic drops must be 1..=8");
}

fn assert_covered_row(row: &CoveredRow) {
    for value in row {
        assert_numbered(*value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clock_and_bonus_match_classic() {
        assert_eq!(drops_for_level(1), 30);
        assert_eq!(drops_for_level(2), 29);
        assert_eq!(drops_for_level(30), 1);
        assert_eq!(drops_for_level(80), 1);
        assert_eq!(LEVEL_BONUS, 7_000);
    }

    #[test]
    fn gray_drop_keeps_its_hidden_value_through_gravity() {
        let mut state = initial_state(SOLID);
        state.board = [EMPTY; CELL_COUNT];
        let latent = [EMPTY; CELL_COUNT];
        let result = play_move(&state, &latent, 3, Some(6), None, 1).unwrap();
        assert_eq!(result.state.board[index(6, 3)], SOLID);
        assert_eq!(result.latent[index(6, 3)], 6);
        assert_eq!(result.state.moves_remaining, 29);
    }

    #[test]
    fn level_boundary_uses_seven_thousand_and_a_decreasing_clock() {
        let mut state = initial_state(7);
        state.board = [EMPTY; CELL_COUNT];
        state.moves_remaining = 1;
        let result = play_move(
            &state,
            &[EMPTY; CELL_COUNT],
            0,
            None,
            Some([1, 2, 3, 4, 5, 6, 7]),
            4,
        )
        .unwrap();
        assert_eq!(result.score_delta, LEVEL_BONUS);
        assert!(result.level_advanced);
        assert_eq!(result.state.level, 2);
        assert_eq!(result.state.moves_remaining, 29);
        assert!(result.covered_row_consumed);
    }

    #[test]
    fn shared_typescript_conformance_transition() {
        let mut state = initial_state(1);
        state.board = [EMPTY; CELL_COUNT];
        state.board[index(6, 2)] = 3;
        state.board[index(6, 3)] = SOLID;
        state.board[index(6, 4)] = 3;
        let mut latent = [EMPTY; CELL_COUNT];
        latent[index(6, 3)] = 6;
        let result = play_move(&state, &latent, 0, None, None, 2).unwrap();
        assert_eq!(
            result.waves,
            vec![Wave {
                depth: 1,
                cleared: 3,
                revealed: 1,
                points: 21,
            }]
        );
        assert_eq!(result.score_delta, 21);
        assert_eq!(result.state.score, 21);
        assert_eq!(result.state.moves_remaining, 29);
        assert_eq!(result.state.board[index(6, 3)], 6);
        assert_eq!(
            result
                .state
                .board
                .iter()
                .filter(|cell| **cell != EMPTY)
                .count(),
            1
        );
        assert!(result.latent.iter().all(|value| *value == EMPTY));
    }
}
