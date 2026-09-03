// The public state: exactly what a legal player may read.

use drop7_rs::board::{Board, BOARD_SIZE, CELL_COUNT};
use drop7_rs::engine::State;

/// The visible board, the visible next disc and the number of drops until the
/// next rise.  No seed, score, level or move number can be reached from this
/// type, so any policy taking only a `PublicView` is inside the information
/// boundary by construction.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PublicView {
    pub board: Board,
    /// Row-major cells with row 0 at the TOP (the engine's serialization
    /// order): 0 empty, 1..=7 numbered, 8 untouched gray, 9 cracked gray.
    pub cells: [u8; CELL_COUNT],
    pub heights: [u8; BOARD_SIZE],
    pub next_disc: u8,
    pub moves_remaining: i32,
}

impl PublicView {
    pub fn new(board: Board, next_disc: u8, moves_remaining: i32) -> PublicView {
        let cells = board.to_bytes();
        let mut heights = [0u8; BOARD_SIZE];
        for col in 0..BOARD_SIZE {
            heights[col] = board.height(col) as u8;
        }
        PublicView {
            board,
            cells,
            heights,
            next_disc,
            moves_remaining,
        }
    }

    pub fn from_state(state: &State) -> PublicView {
        PublicView::new(state.board, state.next_disc, state.moves_remaining)
    }

    /// Cell in the upstream simulator's coordinates: column `x`, row `y`
    /// counted from the BOTTOM (`y == 0` is the bottom row).
    #[inline(always)]
    pub fn cell(&self, x: usize, y: usize) -> u8 {
        self.cells[(BOARD_SIZE - 1 - y) * BOARD_SIZE + x]
    }

    #[inline(always)]
    pub fn is_legal(&self, col: usize) -> bool {
        col < BOARD_SIZE && self.heights[col] < BOARD_SIZE as u8
    }

    pub fn legal_count(&self) -> usize {
        (0..BOARD_SIZE).filter(|&c| self.is_legal(c)).count()
    }

    /// A search-shaped state carrying only the public fields; score, level
    /// and move number are zeroed, exactly as the `decide` binary does.
    pub fn as_search_state(&self) -> State {
        State {
            board: self.board,
            next_disc: self.next_disc,
            score: 0,
            level: 1,
            moves_remaining: self.moves_remaining,
            moves_played: 0,
            game_over: false,
        }
    }
}
