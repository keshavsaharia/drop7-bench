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
  MAX_SPARSE_EXPECTIMAX_DEPTH,
  MAX_SPARSE_EXPECTIMAX_SAMPLES,
  evaluateSparseExpectimaxMoves,
} from "./sparse-expectimax.ts";

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

test("sparse expectimax is deterministic, iterative, and bounded", () => {
  const game = createGame(() => 0.5);
  const options = {
    maxDepth: 3,
    chanceSamples: 2,
    maxWork: 100_000,
    maxCacheEntries: 100,
    seed: 0xdecafbad,
    now: () => 0,
  };
  const first = evaluateSparseExpectimaxMoves(game, options);
  const second = evaluateSparseExpectimaxMoves(game, options);

  assert.deepEqual(first, second);
  assert.equal(first.depth, 3);
  assert.equal(first.complete, true);
  assert.equal(first.stratifiedSamples, true);
  assert.ok(first.work <= options.maxWork);
  assert.ok(first.cacheEntries <= options.maxCacheEntries);
  assert.equal(first.columns.length, BOARD_SIZE);
});

test("a work cutoff returns the last fully completed depth", () => {
  const result = evaluateSparseExpectimaxMoves(createGame(() => 0.5), {
    maxDepth: 5,
    chanceSamples: 2,
    maxWork: 250,
    seed: 1,
  });

  assert.ok(result.depth >= 1);
  assert.ok(result.depth < result.requestedDepth);
  assert.equal(result.complete, false);
  assert.ok(result.work <= 250);
});

test("mirrored states receive exactly mirrored evaluations", () => {
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
    maxDepth: 3,
    chanceSamples: 2,
    maxWork: 100_000,
    seed: 9876,
  };
  const forward = evaluateSparseExpectimaxMoves(position(board), options);
  const mirrored = evaluateSparseExpectimaxMoves(
    position(mirrorBoard(board)),
    options,
  );
  const mirroredByColumn = new Map(
    mirrored.columns.map((column) => [column.column, column]),
  );
  for (const candidate of forward.columns) {
    assert.deepEqual(
      candidate,
      {
        ...mirroredByColumn.get(BOARD_SIZE - 1 - candidate.column),
        column: candidate.column,
      },
    );
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("terminal moves and invalid inputs are handled", () => {
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
  const result = evaluateSparseExpectimaxMoves(
    position(board, { nextDisc: 6 }),
    {
      maxDepth: 4,
      chanceSamples: 3,
      maxWork: 10_000,
      terminalUtility,
      seed: 1,
    },
  );
  assert.equal(result.bestColumn, 6);
  assert.equal(result.columns[0].value, terminalUtility);

  const game = createGame(() => 0.5);
  assert.throws(
    () =>
      evaluateSparseExpectimaxMoves(game, {
        maxDepth: MAX_SPARSE_EXPECTIMAX_DEPTH + 1,
        chanceSamples: 1,
        seed: 0,
      }),
    /maxDepth/,
  );
  assert.throws(
    () =>
      evaluateSparseExpectimaxMoves(game, {
        maxDepth: 1,
        chanceSamples: MAX_SPARSE_EXPECTIMAX_SAMPLES + 1,
        seed: 0,
      }),
    /chanceSamples/,
  );
  assert.throws(
    () =>
      evaluateSparseExpectimaxMoves(game, {
        maxDepth: 1,
        chanceSamples: 1,
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
