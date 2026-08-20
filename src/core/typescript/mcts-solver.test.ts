import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  createGame,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  MAX_MCTS_HORIZON,
  MAX_MCTS_ROLLOUT_DEPTH,
  evaluateMctsMoves,
} from "./mcts-solver.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(board: Board, overrides: Partial<GameState> = {}): GameState {
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

test("MCTS is deterministic and retains bounded search state", () => {
  const game = createGame(() => 0.5);
  const options = {
    simulations: 80,
    horizon: 8,
    rolloutDepth: 2,
    maxNodes: 25,
    seed: 0xd7072026,
  };
  const first = evaluateMctsMoves(game, options);
  const second = evaluateMctsMoves(game, options);

  assert.deepEqual(first, second);
  assert.equal(first.columns.length, BOARD_SIZE);
  assert.equal(
    first.columns.reduce((sum, column) => sum + column.visits, 0),
    options.simulations,
  );
  assert.ok(first.work.createdNodes <= options.maxNodes);
  assert.ok(first.work.simulatedMoves >= options.simulations);
});

test("mirrored positions receive exactly mirrored MCTS evaluations", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(4, E, E, E, E, E, E),
    row(3, E, 6, 7, E, E, E),
  ]);
  const options = {
    simulations: 120,
    horizon: 7,
    rolloutDepth: 1,
    seed: 9876,
  };
  const forward = evaluateMctsMoves(position(board), options);
  const mirrored = evaluateMctsMoves(position(mirrorBoard(board)), options);
  const mirroredByColumn = new Map(
    mirrored.columns.map((column) => [column.column, column]),
  );

  for (const candidate of forward.columns) {
    const opposite = mirroredByColumn.get(BOARD_SIZE - 1 - candidate.column);
    assert.ok(opposite);
    assert.equal(candidate.mean, opposite.mean);
    assert.equal(candidate.visits, opposite.visits);
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("a forced terminal move receives the terminal utility", () => {
  const board = boardFromRows([
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, E),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const terminalUtility = -123_456;
  const result = evaluateMctsMoves(position(board, { nextDisc: 6 }), {
    simulations: 10,
    horizon: 20,
    rolloutDepth: 4,
    terminalUtility,
    seed: 1,
  });

  assert.equal(result.bestColumn, 6);
  assert.deepEqual(result.columns, [
    { column: 6, mean: terminalUtility, visits: 10 },
  ]);
});

test("MCTS inputs and custom evaluator output are validated", () => {
  const game = createGame(() => 0.5);
  assert.throws(
    () => evaluateMctsMoves(game, { simulations: 0, horizon: 1, seed: 0 }),
    /simulations/,
  );
  assert.throws(
    () =>
      evaluateMctsMoves(game, {
        simulations: 1,
        horizon: MAX_MCTS_HORIZON + 1,
        seed: 0,
      }),
    /horizon/,
  );
  assert.throws(
    () =>
      evaluateMctsMoves(game, {
        simulations: 1,
        horizon: 1,
        rolloutDepth: MAX_MCTS_ROLLOUT_DEPTH + 1,
        seed: 0,
      }),
    /rolloutDepth/,
  );
  assert.throws(
    () =>
      evaluateMctsMoves(game, {
        simulations: 1,
        horizon: 1,
        exploration: -1,
        seed: 0,
      }),
    /exploration/,
  );
  assert.throws(
    () =>
      evaluateMctsMoves(game, {
        simulations: 1,
        horizon: 1,
        seed: 0,
        evaluator: () => Number.NaN,
      }),
    /finite/,
  );
});

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored.push(board[rowIndex * BOARD_SIZE + column]);
    }
  }
  return mirrored;
}
