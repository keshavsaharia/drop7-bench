import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  createInitialBoard,
  createInitialLatentValues,
  isNumbered,
  legalColumns,
  playMove,
  seededRandom,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "./engine.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function stateWith(
  board: readonly Cell[],
  overrides: Partial<GameState> = {},
): GameState {
  return {
    board,
    nextDisc: 4,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
    ...overrides,
  };
}

function indexOf(rowIndex: number, column: number) {
  return rowIndex * BOARD_SIZE + column;
}

function emptyLatent(): LatentValues {
  return Array<DiscValue | null>(BOARD_SIZE * BOARD_SIZE).fill(null);
}

function assertLatentInvariant(board: Board, latent: readonly (DiscValue | null)[]) {
  for (let index = 0; index < board.length; index += 1) {
    const cell = board[index];
    if (cell === SOLID || cell === CRACKED) {
      assert.notEqual(
        latent[index],
        null,
        `covered cell ${index} must carry a latent value`,
      );
    } else {
      assert.equal(
        latent[index] ?? null,
        null,
        `uncovered cell ${index} must not carry a latent value`,
      );
    }
  }
}

test("a reveal uses the covered cell's predetermined latent value", () => {
  // (5,2) = 2 pops through its column; (6,3) = 2 pops through its row.
  // Both pops hit the covered cell at (6,2) in the same wave, revealing it.
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(E, E, SOLID, 2, E, E, E),
  ]);
  const latent = emptyLatent();
  latent[indexOf(6, 2)] = 5;

  const result = playMove(stateWith(board, { nextDisc: 7 }), 0, () => 0.5, {
    captureAnimation: false,
    latent: { values: latent, nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7] },
  });

  assert.ok(result);
  assert.equal(result.state.board[indexOf(6, 2)], 5);
  assert.equal(result.latentValues?.[indexOf(6, 2)] ?? null, null);
  assertLatentInvariant(result.state.board, result.latentValues ?? []);
});

test("a reveal without a latent value is an error, not a draw", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(E, E, SOLID, 2, E, E, E),
  ]);
  assert.throws(
    () =>
      playMove(stateWith(board, { nextDisc: 7 }), 0, () => 0.5, {
        captureAnimation: false,
        latent: { values: emptyLatent(), nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7] },
      }),
    /no valid latent value/,
  );
});

test("latent values follow their covered cell through gravity", () => {
  // The 1 pops, the covered cell above it cracks and falls one row.
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, SOLID, E, E, E, E),
    row(E, E, 1, E, E, E, E),
  ]);
  const latent = emptyLatent();
  latent[indexOf(5, 2)] = 6;

  const first = playMove(stateWith(board, { nextDisc: 7 }), 0, () => 0.5, {
    captureAnimation: false,
    latent: { values: latent, nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7] },
  });
  assert.ok(first);
  assert.equal(first.state.board[indexOf(6, 2)], CRACKED);
  assert.equal(first.latentValues?.[indexOf(6, 2)], 6);

  // A second hit on the cracked cell reveals the value that traveled with it.
  const second = playMove(
    stateWith(first.state.board, { nextDisc: 1 }),
    1,
    () => 0.5,
    {
      captureAnimation: false,
      latent: {
        values: first.latentValues ?? [],
        nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7],
      },
    },
  );
  assert.ok(second);
  assert.equal(second.state.board[indexOf(6, 2)], 6);
  assertLatentInvariant(second.state.board, second.latentValues ?? []);
});

test("a row rise shifts latent values up and draws the new row from the source", () => {
  const board = createInitialBoard();
  const latent = createInitialLatentValues([1, 2, 3, 4, 5, 6, 7]);
  let draws = 0;

  const result = playMove(
    stateWith(board, { nextDisc: 7, movesRemaining: 1 }),
    0,
    () => 0.5,
    {
      captureAnimation: false,
      latent: {
        values: latent,
        nextCoveredRow: () => {
          draws += 1;
          return [7, 6, 5, 4, 3, 2, 1];
        },
      },
    },
  );

  assert.ok(result);
  assert.equal(result.levelAdvanced, true);
  assert.equal(draws, 1);
  const updated = result.latentValues ?? [];
  // The dropped 7 moved from row 5 to row 4 and carries no latent value.
  assert.equal(result.state.board[indexOf(4, 0)], 7);
  assert.equal(updated[indexOf(4, 0)] ?? null, null);
  // The original covered row moved from row 6 to row 5 with its values.
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    assert.equal(result.state.board[indexOf(5, column)], SOLID);
    assert.equal(updated[indexOf(5, column)], (column + 1) as DiscValue);
    assert.equal(result.state.board[indexOf(6, column)], SOLID);
    assert.equal(updated[indexOf(6, column)], (7 - column) as DiscValue);
  }
  assertLatentInvariant(result.state.board, updated);
});

test("scripted latent games are exactly reproducible", () => {
  const playScriptedGame = () => {
    const latentRows: DiscValue[][] = [[3, 1, 4, 1, 5, 2, 6]];
    const random = seededRandom(0x5eed);
    for (let rowIndex = 1; rowIndex < 40; rowIndex += 1) {
      latentRows.push(
        Array.from(
          { length: BOARD_SIZE },
          () => (Math.floor(random() * BOARD_SIZE) + 1) as DiscValue,
        ),
      );
    }
    let rowCursor = 1;
    let state = stateWith(createInitialBoard(), { nextDisc: 3 });
    let latent: readonly (DiscValue | null)[] = createInitialLatentValues(
      latentRows[0],
    );
    const scores: number[] = [];
    while (!state.gameOver && state.movesPlayed < 120) {
      const legal = legalColumns(state.board);
      const column = legal[(state.movesPlayed * 3 + 1) % legal.length];
      const move = playMove(state, column, () => 0.5, {
        captureAnimation: false,
        latent: {
          values: latent,
          nextCoveredRow: () => latentRows[rowCursor++],
        },
      });
      assert.ok(move);
      state = { ...move.state, nextDisc: ((state.movesPlayed * 5) % 7 + 1) as DiscValue };
      latent = move.latentValues ?? [];
      assertLatentInvariant(state.board, latent);
      scores.push(state.score);
    }
    return { score: state.score, moves: state.movesPlayed, board: state.board.join("") };
  };

  const first = playScriptedGame();
  const second = playScriptedGame();
  assert.deepEqual(first, second);
  assert.ok(first.moves > 20, "the scripted game should survive a while");
});

test("moves without a latent board keep random reveals and report no latent state", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(E, E, SOLID, 2, E, E, E),
  ]);
  const result = playMove(
    stateWith(board, { nextDisc: 7 }),
    0,
    seededRandom(0x5eed),
    { captureAnimation: false },
  );
  assert.ok(result);
  assert.equal(result.latentValues, undefined);
  const revealed = result.state.board[indexOf(6, 2)];
  assert.ok(isNumbered(revealed), "the covered cell is still revealed");
});
