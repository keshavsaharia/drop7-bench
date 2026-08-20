import assert from "node:assert/strict";
import test from "node:test";

import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  boardFromRows,
  createGame,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  MAX_RISK_CACHE_ENTRIES,
  MAX_RISK_CHANCE_SAMPLES,
  MAX_RISK_CONTINUATION_DEPTH,
  MAX_RISK_SCENARIOS,
  MAX_RISK_WORK,
  evaluateRiskSensitiveMoves,
  summarizeRiskDistribution,
} from "./risk-sensitive-planner.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);
const fastEvaluator = (state: GameState) =>
  -state.board.reduce<number>(
    (occupied, cell) => occupied + Number(cell !== EMPTY),
    0,
  );

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

const deterministicOptions = {
  scenarios: 7,
  continuationDepth: 1,
  chanceSamples: 3,
  tailFraction: 0.25,
  riskWeight: 0.5,
  seed: 0xdecafbad,
  maxWork: 100_000,
  maxCacheEntries: 32,
  evaluator: fastEvaluator,
};

test("risk planner is deterministic and obeys work and memory bounds", () => {
  const state = createGame(() => 0.5);
  const first = evaluateRiskSensitiveMoves(state, deterministicOptions);
  const second = evaluateRiskSensitiveMoves(state, deterministicOptions);

  assert.deepEqual(second, first);
  assert.equal(first.complete, true);
  assert.equal(first.completedScenarios, deterministicOptions.scenarios);
  assert.ok(first.work <= deterministicOptions.maxWork);
  assert.ok(first.cacheEntries <= deterministicOptions.maxCacheEntries);
  assert.ok(
    first.peakUtilityValues <= BOARD_SIZE * deterministicOptions.scenarios,
  );
  assert.ok(
    first.columns.every(
      (column) => column.scenarios === first.completedScenarios,
    ),
  );
});

test("planner chance and evaluator are blind to accumulated game score", () => {
  const state = createGame(() => 0.5);
  const observedScores: number[] = [];
  const options = {
    ...deterministicOptions,
    evaluator: (leaf: GameState) => {
      observedScores.push(leaf.score);
      return fastEvaluator(leaf);
    },
  };
  const baseline = evaluateRiskSensitiveMoves(state, options);
  const rescored = evaluateRiskSensitiveMoves(
    { ...state, score: 8_765_432 },
    options,
  );

  assert.deepEqual(rescored, baseline);
  assert.ok(observedScores.length > 0);
  assert.ok(observedScores.every((score) => score === 0));
});

test("asymmetric positions and their reflections receive mirrored decisions", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(4, E, 6, E, E, E, E),
    row(3, E, 6, 7, E, E, E),
  ]);
  const forward = evaluateRiskSensitiveMoves(
    position(board),
    deterministicOptions,
  );
  const reverse = evaluateRiskSensitiveMoves(
    position(mirrorBoard(board)),
    deterministicOptions,
  );
  const reverseByColumn = new Map(
    reverse.columns.map((column) => [column.column, column]),
  );

  for (const column of forward.columns) {
    const opposite = reverseByColumn.get(BOARD_SIZE - 1 - column.column);
    assert.ok(opposite);
    assert.equal(opposite.mean, column.mean);
    assert.equal(opposite.lowerQuantile, column.lowerQuantile);
    assert.equal(opposite.cvar, column.cvar);
    assert.equal(opposite.selectionValue, column.selectionValue);
  }
  assert.equal(
    reverse.bestColumn,
    forward.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - forward.bestColumn,
  );
});

test("CVaR is the exact fractional mean of the requested lower tail", () => {
  const summary = summarizeRiskDistribution([0, 100, 100, 100], 0.375, 0.5);
  assert.equal(summary.mean, 75);
  assert.equal(summary.lowerQuantile, 100);
  // The lower 1.5 observations contain 0 plus half of the first 100.
  assert.ok(Math.abs(summary.cvar - 100 / 3) < 1e-12);
  assert.ok(
    Math.abs(
      summary.selectionValue - (summary.mean + 0.5 * (summary.cvar - summary.mean)),
    ) < 1e-12,
  );
});

test("a partial scenario never creates an unpaired root comparison", () => {
  const state = createGame(() => 0.5);
  const result = evaluateRiskSensitiveMoves(state, {
    ...deterministicOptions,
    scenarios: 20,
    continuationDepth: 2,
    maxWork: 311,
  });

  assert.equal(result.complete, false);
  assert.equal(result.stopReason, "work");
  assert.equal(result.work, 311);
  assert.ok(result.completedScenarios < result.requestedScenarios);
  assert.ok(
    result.columns.every(
      (column) => column.scenarios === result.completedScenarios,
    ),
  );
});

test("configuration bounds and terminal states are handled explicitly", () => {
  const state = createGame(() => 0.5);
  const valid = {
    scenarios: 1,
    continuationDepth: 0,
    chanceSamples: 1,
    seed: 0,
  };
  for (const options of [
    { ...valid, scenarios: MAX_RISK_SCENARIOS + 1 },
    { ...valid, continuationDepth: MAX_RISK_CONTINUATION_DEPTH + 1 },
    { ...valid, chanceSamples: MAX_RISK_CHANCE_SAMPLES + 1 },
    { ...valid, maxWork: MAX_RISK_WORK + 1 },
    { ...valid, maxCacheEntries: MAX_RISK_CACHE_ENTRIES + 1 },
    { ...valid, tailFraction: 0 },
    { ...valid, riskWeight: 3 },
    { ...valid, seed: -1 },
  ]) {
    assert.throws(() => evaluateRiskSensitiveMoves(state, options));
  }

  const terminal = evaluateRiskSensitiveMoves(
    { ...state, gameOver: true },
    valid,
  );
  assert.equal(terminal.bestColumn, null);
  assert.deepEqual(terminal.columns, []);
  assert.equal(terminal.work, 0);
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
