import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import { evaluateGrayRolloutMoves } from "./gray-throughput-rollout.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(board: Board, overrides: Partial<GameState> = {}): GameState {
  return {
    board,
    nextDisc: 5,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
    ...overrides,
  };
}

test("gray rollouts are deterministic, score independent, and mirror exact", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(4, E, 6, E, E, E, E),
    row(SOLID, E, 6, 7, E, E, E),
  ]);
  const options = {
    scenarios: 2,
    horizon: 3,
    guideSamples: 1,
    policySeed: 0x1234_5678,
  };
  const forward = evaluateGrayRolloutMoves(position(board), options);
  const repeated = evaluateGrayRolloutMoves(position(board), options);
  const rescored = evaluateGrayRolloutMoves(
    position(board, { score: 1_000_000 }),
    options,
  );
  const mirrored = evaluateGrayRolloutMoves(
    position(mirrorBoard(board)),
    options,
  );
  const mirroredValues = new Map(
    mirrored.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  assert.deepEqual(repeated, forward);
  assert.deepEqual(rescored, forward);
  for (const evaluation of forward.columns) {
    const opposite = mirroredValues.get(BOARD_SIZE - 1 - evaluation.column);
    assert.ok(opposite);
    assert.equal(evaluation.mean, opposite.mean);
    assert.equal(evaluation.utility, opposite.utility);
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("gray rollout options and terminal states are bounded", () => {
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
    () => evaluateGrayRolloutMoves(state, { scenarios: 0 }),
    /scenarios/,
  );
  assert.throws(
    () => evaluateGrayRolloutMoves(state, { horizon: 41 }),
    /horizon/,
  );
  assert.throws(
    () => evaluateGrayRolloutMoves(state, { riskAversion: -1 }),
    /riskAversion/,
  );
  assert.throws(
    () => evaluateGrayRolloutMoves(state, { policySeed: 0x1_0000_0000 }),
    /uint32/,
  );
  const terminal = evaluateGrayRolloutMoves({ ...state, gameOver: true });
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
