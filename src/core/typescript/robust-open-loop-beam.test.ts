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
  MAX_OPEN_LOOP_BEAM_WIDTH,
  MAX_OPEN_LOOP_DEPTH,
  MAX_OPEN_LOOP_SCENARIOS,
  MAX_OPEN_LOOP_TIME_MS,
  MAX_OPEN_LOOP_WORK,
  evaluateRobustOpenLoopBeam,
} from "./robust-open-loop-beam.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(
  board: Board,
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

const fastEvaluator = (state: GameState) =>
  -state.board.reduce<number>(
    (occupied, cell) => occupied + Number(cell !== E),
    0,
  );

test("open-loop search is deterministic and retains only bounded beams", () => {
  const options = {
    scenarios: 8,
    depth: 4,
    beamWidth: 9,
    seed: 0xdecafbad,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: fastEvaluator,
  };
  const first = evaluateRobustOpenLoopBeam(createGame(() => 0.5), options);
  const second = evaluateRobustOpenLoopBeam(createGame(() => 0.5), options);
  const { elapsedMs: firstElapsed, ...firstDeterministic } = first;
  const { elapsedMs: secondElapsed, ...secondDeterministic } = second;

  assert.ok(firstElapsed >= 0 && secondElapsed >= 0);
  assert.deepEqual(firstDeterministic, secondDeterministic);
  assert.equal(first.complete, true);
  assert.equal(first.completedDepth, options.depth);
  assert.equal(first.bestPrefix.length, options.depth);
  assert.ok(first.columns.every((column) => column.scenarios === 8));
  assert.ok(
    first.columns.every((column) => column.prefix[0] === column.column),
  );
  assert.ok(first.work.peakBeamNodes <= options.beamWidth);
  assert.ok(
    first.work.peakRetainedScenarioStates <=
      (options.beamWidth * 2 + 1) * options.scenarios,
  );
  assert.ok(first.work.generatedNodes > options.beamWidth);
  assert.ok(first.work.prunedNodes > 0);
});

test("one prefix is shared across scenarios instead of fusing strategies", () => {
  const result = evaluateRobustOpenLoopBeam(createGame(() => 0.999), {
    scenarios: BOARD_SIZE,
    depth: 2,
    beamWidth: BOARD_SIZE * BOARD_SIZE,
    seed: 0x0f3a_1000,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: (state) => {
      // The second disc is stratified from 1 through 7. A chance-aware policy
      // could route 3/4/5/6 to four different columns and collect this bonus
      // four times as often. One fixed second column can match at most one.
      for (let column = 0; column < 4; column += 1) {
        if (state.board[5 * BOARD_SIZE + column] === column + 3) return 1_000;
      }
      return 0;
    },
  });

  assert.equal(result.complete, true);
  assert.equal(result.bestPrefix.length, 2);
  assert.ok(Math.max(...result.columns.map((column) => column.mean)) < 300);
  assert.ok(
    result.columns.every(
      (column) => column.prefix.length === 2 && column.scenarios === 7,
    ),
  );
});

test("seven common scenarios stratify the next disc exactly", () => {
  const result = evaluateRobustOpenLoopBeam(createGame(() => 0.5), {
    scenarios: BOARD_SIZE,
    depth: 1,
    beamWidth: BOARD_SIZE,
    seed: 2026,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: (state) => state.nextDisc,
  });

  assert.ok(
    result.columns.every((column) => Math.abs(column.mean - 4) < 1e-12),
  );
  assert.ok(
    result.columns.every(
      (column) => Math.abs(column.variance - 4) < 1e-12,
    ),
  );
});

test("risk aversion ranks mean minus population standard deviation", () => {
  const riskAversion = 0.75;
  const result = evaluateRobustOpenLoopBeam(createGame(() => 0.5), {
    scenarios: 7,
    depth: 2,
    beamWidth: 12,
    riskAversion,
    seed: 99,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: (state) => state.nextDisc * 100,
  });

  for (const column of result.columns) {
    assert.ok(
      Math.abs(
        column.robustValue -
          (column.mean - riskAversion * Math.sqrt(column.variance)),
      ) < 1e-10,
    );
  }
});

test("symmetric roots and mirrored positions receive mirrored values", () => {
  const symmetric = evaluateRobustOpenLoopBeam(createGame(() => 0.5), {
    scenarios: 7,
    depth: 3,
    beamWidth: 28,
    seed: 71,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: fastEvaluator,
  });
  const symmetricValues = new Map(
    symmetric.columns.map((column) => [column.column, column]),
  );
  for (const [first, second] of [
    [0, 6],
    [1, 5],
    [2, 4],
  ] as const) {
    const left = symmetricValues.get(first);
    const right = symmetricValues.get(second);
    assert.ok(left && right);
    assert.equal(left.mean, right.mean);
    assert.equal(left.variance, right.variance);
    assert.equal(left.robustValue, right.robustValue);
  }

  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(4, E, 6, E, E, E, E),
    row(3, E, 6, 7, E, E, E),
  ]);
  const options = {
    scenarios: 8,
    depth: 4,
    beamWidth: 32,
    seed: 9876,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: fastEvaluator,
  };
  const forward = evaluateRobustOpenLoopBeam(position(board), options);
  const reverse = evaluateRobustOpenLoopBeam(
    position(mirrorBoard(board)),
    options,
  );
  const reverseValues = new Map(
    reverse.columns.map((column) => [column.column, column]),
  );
  for (const column of forward.columns) {
    const opposite = reverseValues.get(BOARD_SIZE - 1 - column.column);
    assert.ok(opposite);
    assert.equal(column.mean, opposite.mean);
    assert.equal(column.variance, opposite.variance);
    assert.equal(column.robustValue, opposite.robustValue);
  }
  assert.equal(
    forward.bestColumn,
    reverse.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - reverse.bestColumn,
  );
  assert.deepEqual(
    forward.bestPrefix,
    reverse.bestPrefix.map((column) => BOARD_SIZE - 1 - column),
  );
});

test("work exhaustion returns the last fully completed beam layer", () => {
  const maxWork = 35;
  const result = evaluateRobustOpenLoopBeam(createGame(() => 0.5), {
    scenarios: 3,
    depth: 4,
    beamWidth: 7,
    maxWork,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    seed: 5,
    evaluator: fastEvaluator,
  });

  assert.equal(result.complete, false);
  assert.equal(result.stopReason, "work");
  assert.equal(result.completedDepth, 1);
  assert.equal(result.bestPrefix.length, 1);
  assert.equal(result.work.total, maxWork);

  const fallback = evaluateRobustOpenLoopBeam(createGame(() => 0.5), {
    scenarios: 8,
    depth: 4,
    beamWidth: 7,
    maxWork: 1,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    seed: 5,
    evaluator: fastEvaluator,
  });
  assert.equal(fallback.completedDepth, 0);
  assert.equal(fallback.stopReason, "work");
  assert.equal(fallback.bestColumn, 3);
  assert.deepEqual(fallback.bestPrefix, [3]);
});

test("the evaluator and chance seed do not depend on accumulated score", () => {
  const game = createGame(() => 0.5);
  const observedScores: number[] = [];
  const options = {
    scenarios: 5,
    depth: 3,
    beamWidth: 14,
    seed: 42,
    timeLimitMs: MAX_OPEN_LOOP_TIME_MS,
    evaluator: (state: GameState) => {
      observedScores.push(state.score);
      return fastEvaluator(state);
    },
  };
  const baseline = evaluateRobustOpenLoopBeam(game, options);
  const rescored = evaluateRobustOpenLoopBeam(
    { ...game, score: 9_999_999 },
    options,
  );
  const { elapsedMs: baselineElapsed, ...baselineStable } = baseline;
  const { elapsedMs: rescoredElapsed, ...rescoredStable } = rescored;

  assert.ok(baselineElapsed >= 0 && rescoredElapsed >= 0);
  assert.deepEqual(rescoredStable, baselineStable);
  assert.ok(observedScores.length > 0);
  assert.ok(observedScores.every((score) => score === 0));
});

test("limits, evaluator output, and terminal positions are validated", () => {
  const game = createGame(() => 0.5);
  const valid = {
    scenarios: 1,
    depth: 1,
    beamWidth: 1,
    seed: 0,
  };
  for (const [field, value] of [
    ["scenarios", MAX_OPEN_LOOP_SCENARIOS + 1],
    ["depth", MAX_OPEN_LOOP_DEPTH + 1],
    ["beamWidth", MAX_OPEN_LOOP_BEAM_WIDTH + 1],
    ["maxWork", MAX_OPEN_LOOP_WORK + 1],
    ["timeLimitMs", MAX_OPEN_LOOP_TIME_MS + 1],
  ] as const) {
    assert.throws(
      () => evaluateRobustOpenLoopBeam(game, { ...valid, [field]: value }),
      new RegExp(field),
    );
  }
  assert.throws(
    () =>
      evaluateRobustOpenLoopBeam(game, {
        ...valid,
        riskAversion: -1,
      }),
    /riskAversion/,
  );
  assert.throws(
    () =>
      evaluateRobustOpenLoopBeam(game, {
        ...valid,
        seed: 0x1_0000_0000,
      }),
    /uint32/,
  );
  assert.throws(
    () =>
      evaluateRobustOpenLoopBeam(game, {
        ...valid,
        evaluator: () => Number.NaN,
      }),
    /finite/,
  );

  const terminal = evaluateRobustOpenLoopBeam(
    { ...game, gameOver: true },
    valid,
  );
  assert.equal(terminal.complete, true);
  assert.equal(terminal.bestColumn, null);
  assert.equal(terminal.completedDepth, 0);
  assert.deepEqual(terminal.columns, []);
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
