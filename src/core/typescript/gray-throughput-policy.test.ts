import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  playMove,
  seededRandom,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS,
  REQUIRED_CLEAR_THROUGHPUT,
  REQUIRED_REVEAL_THROUGHPUT,
  evaluateGrayThroughputMoves,
  extractGrayStateFeatures,
  extractGrayTransitionFeatures,
  scoreGrayState,
} from "./gray-throughput-policy.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(
  board: Board,
  nextDisc: DiscValue = 4,
  overrides: Partial<GameState> = {},
): GameState {
  return {
    board,
    nextDisc,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
    ...overrides,
  };
}

test("the queueing targets match Hardcore's long-run arrivals", () => {
  assert.equal(REQUIRED_CLEAR_THROUGHPUT, 2.4);
  assert.equal(REQUIRED_REVEAL_THROUGHPUT, 1.4);
});

test("a two-row operating board outranks a four-row board", () => {
  const shallow = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(7, 6, 5, 4, 5, 6, 7),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const tall = boardFromRows([
    blank(),
    blank(),
    blank(),
    row(7, 6, 5, 4, 5, 6, 7),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(7, 6, 5, 4, 5, 6, 7),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);

  assert.ok(scoreGrayState(position(shallow)) > scoreGrayState(position(tall)));
  assert.equal(extractGrayStateFeatures(position(shallow)).peakExcess, 0);
  assert.ok(extractGrayStateFeatures(position(tall)).peakExcess > 0);
});

test("transition features count cover throughput before gravity", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, E, E, E, E, E, E),
  ]);
  const state = position(board, 1);
  const move = playMove(state, 0, seededRandom(1), {
    captureAnimation: true,
  });
  assert.ok(move);
  const features = extractGrayTransitionFeatures(state, move);

  assert.equal(features.crackedCovers, 1);
  assert.equal(features.clearedDiscs, 1);
  assert.equal(features.revealedCovers, 0);
  assert.equal(features.clearSurplus, 1 - REQUIRED_CLEAR_THROUGHPUT);
  assert.equal(features.revealSurplus, -REQUIRED_REVEAL_THROUGHPUT);
});

test("high adjacent low caps are recognized as clog debt", () => {
  const clear = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 6, 6, E, E, E),
    row(E, E, SOLID, SOLID, E, E, E),
  ]);
  const clogged = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 1, 2, E, E, E),
    row(E, E, SOLID, SOLID, E, E, E),
  ]);
  const clearFeatures = extractGrayStateFeatures(position(clear));
  const clogFeatures = extractGrayStateFeatures(position(clogged));

  assert.equal(clearFeatures.lowCaps, 0);
  assert.ok(clogFeatures.lowCaps > 0);
  assert.ok(clogFeatures.adjacentLowCaps > 0);
  assert.ok(scoreGrayState(position(clear)) > scoreGrayState(position(clogged)));
});

test("policy evaluations reflect exactly and ignore accumulated score", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(4, E, 6, E, E, E, E),
    row(SOLID, E, 6, 7, E, E, E),
  ]);
  const state = position(board, 5);
  const options = {
    samples: 4,
    continuationSamples: 2,
    depth: 2 as const,
    policySeed: 0x1234_5678,
  };
  const forward = evaluateGrayThroughputMoves(state, options);
  const mirrored = evaluateGrayThroughputMoves(
    position(mirrorBoard(board), 5),
    options,
  );
  const rescored = evaluateGrayThroughputMoves(
    { ...state, score: 1_000_000 },
    options,
  );
  const mirroredValues = new Map(
    mirrored.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  assert.deepEqual(rescored, forward);
  for (const evaluation of forward.columns) {
    const opposite = mirroredValues.get(BOARD_SIZE - 1 - evaluation.column);
    assert.ok(opposite);
    assert.equal(evaluation.mean, opposite.mean);
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("policy inputs and terminal states are bounded", () => {
  const state = position(boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]));
  assert.throws(
    () => evaluateGrayThroughputMoves(state, { samples: 0 }),
    /samples/,
  );
  assert.throws(
    () => evaluateGrayThroughputMoves(state, { depth: 3 as never }),
    /depth/,
  );
  assert.throws(
    () =>
      evaluateGrayThroughputMoves(state, {
        policySeed: 0x1_0000_0000,
      }),
    /uint32/,
  );
  assert.throws(
    () =>
      evaluateGrayThroughputMoves(state, {
        weights: {
          ...DEFAULT_GRAY_THROUGHPUT_WEIGHTS,
          continuationWeight: Number.NaN,
        },
      }),
    /finite/,
  );

  const terminal = evaluateGrayThroughputMoves(
    { ...state, gameOver: true },
  );
  assert.equal(terminal.bestColumn, null);
  assert.deepEqual(terminal.columns, []);
});

function mirrorBoard(board: Board): Board {
  const result: Cell[] = [];
  for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      result.push(board[rowIndex * BOARD_SIZE + column]);
    }
  }
  return result;
}
