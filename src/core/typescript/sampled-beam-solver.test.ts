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
  DEFAULT_SAMPLED_BEAM_POLICY_DEPTH,
  DEFAULT_SAMPLED_BEAM_POLICY_SCENARIOS,
  DEFAULT_SAMPLED_BEAM_WIDTH,
  MAX_SAMPLED_BEAM_DEPTH,
  MAX_SAMPLED_BEAM_POLICY_DEPTH,
  MAX_SAMPLED_BEAM_POLICY_SCENARIOS,
  MAX_SAMPLED_BEAM_SCENARIOS,
  MAX_SAMPLED_BEAM_WIDTH,
  MAX_SAMPLED_BEAM_WORK,
  evaluateSampledBeamMoves,
} from "./sampled-beam-solver.ts";

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

test("sampled beam evaluation is deterministic, fair, and memory-bounded", () => {
  const options = {
    scenarios: 6,
    depth: 5,
    beamWidth: 4,
    seed: 0xdecafbad,
    evaluator: fastEvaluator,
  };
  const first = evaluateSampledBeamMoves(createGame(() => 0.5), options);
  const second = evaluateSampledBeamMoves(createGame(() => 0.5), options);

  assert.deepEqual(first, second);
  assert.equal(first.complete, true);
  assert.equal(first.completedScenarios, options.scenarios);
  assert.equal(first.columns.length, BOARD_SIZE);
  assert.ok(
    first.columns.every((column) => column.scenarios === options.scenarios),
  );
  assert.ok(first.columns.every((column) => column.variance >= 0));
  assert.ok(first.work.simulatedMoves > BOARD_SIZE * options.scenarios);
  assert.equal(
    first.work.total,
    first.work.simulatedMoves + first.work.evaluatedStates,
  );
  assert.ok(first.work.policySearches > 0);
  assert.ok(first.work.deduplicatedStates > 0);
  assert.ok(first.work.prunedStates > 0);
  assert.ok(first.work.peakBeamSize <= options.beamWidth);
  assert.ok(first.work.peakCandidateStates <= options.beamWidth * BOARD_SIZE);
});

test("common scenarios give mirrored root moves equal statistics", () => {
  const result = evaluateSampledBeamMoves(createGame(() => 0.5), {
    scenarios: 5,
    depth: 4,
    beamWidth: 6,
    seed: 1234,
    evaluator: fastEvaluator,
  });
  const values = new Map(
    result.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  assert.deepEqual(values.get(0), { ...values.get(6), column: 0 });
  assert.deepEqual(values.get(1), { ...values.get(5), column: 1 });
  assert.deepEqual(values.get(2), { ...values.get(4), column: 2 });
});

test("mirrored positions receive mirrored sampled-beam evaluations", () => {
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
    scenarios: 6,
    depth: 5,
    beamWidth: 8,
    seed: 9876,
    evaluator: fastEvaluator,
  };
  const forward = evaluateSampledBeamMoves(position(board), options);
  const mirrored = evaluateSampledBeamMoves(
    position(mirrorBoard(board)),
    options,
  );
  const mirroredValues = new Map(
    mirrored.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  for (const evaluation of forward.columns) {
    const opposite = mirroredValues.get(BOARD_SIZE - 1 - evaluation.column);
    assert.ok(opposite);
    assert.equal(evaluation.mean, opposite.mean);
    assert.equal(evaluation.variance, opposite.variance);
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("the evaluator and scenario seed are independent of state.score", () => {
  const game = createGame(() => 0.5);
  const observedScores: number[] = [];
  const options = {
    scenarios: 4,
    depth: 4,
    beamWidth: 5,
    seed: 42,
    evaluator: (state: GameState) => {
      observedScores.push(state.score);
      return fastEvaluator(state);
    },
  };
  const baseline = evaluateSampledBeamMoves(game, options);
  const rescored = evaluateSampledBeamMoves(
    { ...game, score: 9_999_999 },
    options,
  );

  assert.deepEqual(rescored, baseline);
  assert.ok(observedScores.length > 0);
  assert.ok(observedScores.every((score) => score === 0));
});

test("depth one has predictable work and the default beam width", () => {
  const scenarios = 3;
  const result = evaluateSampledBeamMoves(createGame(() => 0.5), {
    scenarios,
    depth: 1,
    seed: 7,
    evaluator: () => 123,
  });

  assert.equal(result.beamWidth, DEFAULT_SAMPLED_BEAM_WIDTH);
  assert.equal(result.policyDepth, DEFAULT_SAMPLED_BEAM_POLICY_DEPTH);
  assert.equal(
    result.policyScenarios,
    DEFAULT_SAMPLED_BEAM_POLICY_SCENARIOS,
  );
  assert.equal(result.work.simulatedMoves, BOARD_SIZE * scenarios);
  assert.equal(result.work.evaluatedStates, BOARD_SIZE * scenarios);
  assert.ok(result.columns.every((column) => column.mean === 123));
  assert.ok(result.columns.every((column) => column.variance === 0));
});

test("seven scenarios stratify the next disc across all seven values", () => {
  const result = evaluateSampledBeamMoves(createGame(() => 0.5), {
    scenarios: BOARD_SIZE,
    depth: 1,
    beamWidth: 1,
    seed: 2026,
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

test("continuation decisions average every configured policy scenario", () => {
  const shared = {
    scenarios: 1,
    depth: 2,
    policyDepth: 1,
    beamWidth: 1,
    seed: 17,
    evaluator: () => 0,
  };
  const one = evaluateSampledBeamMoves(createGame(() => 0.5), {
    ...shared,
    policyScenarios: 1,
  });
  const four = evaluateSampledBeamMoves(createGame(() => 0.5), {
    ...shared,
    policyScenarios: 4,
  });

  // Per outer root: root/continuation moves plus final evaluation are three
  // units. Each policy scenario evaluates seven moves at two units apiece.
  assert.equal(one.work.total, BOARD_SIZE * (3 + BOARD_SIZE * 2));
  assert.equal(four.work.total, BOARD_SIZE * (3 + BOARD_SIZE * 2 * 4));
  assert.equal(one.work.policySearches, BOARD_SIZE);
  assert.equal(four.work.policySearches, BOARD_SIZE);
});

test("a deterministic work limit stops only between fair scenario batches", () => {
  const maxWork = 2_000;
  const result = evaluateSampledBeamMoves(createGame(() => 0.5), {
    scenarios: 5,
    depth: 4,
    policyDepth: 2,
    policyScenarios: 1,
    beamWidth: 2,
    maxWork,
    seed: 99,
    evaluator: fastEvaluator,
  });

  assert.equal(result.complete, false);
  assert.ok(result.completedScenarios > 0);
  assert.ok(result.completedScenarios < result.scenarios);
  assert.equal(result.work.total, maxWork);
  assert.ok(
    result.columns.every(
      (column) => column.scenarios === result.completedScenarios,
    ),
  );
});

test("sampled beam inputs are bounded and validated", () => {
  const game = createGame(() => 0.5);
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 0,
        depth: 1,
        seed: 0,
      }),
    /scenarios/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: MAX_SAMPLED_BEAM_SCENARIOS + 1,
        depth: 1,
        seed: 0,
      }),
    /scenarios/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: MAX_SAMPLED_BEAM_DEPTH + 1,
        seed: 0,
      }),
    /depth/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
        policyDepth: MAX_SAMPLED_BEAM_POLICY_DEPTH + 1,
        seed: 0,
      }),
    /policyDepth/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
        policyScenarios: MAX_SAMPLED_BEAM_POLICY_SCENARIOS + 1,
        seed: 0,
      }),
    /policyScenarios/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
        beamWidth: MAX_SAMPLED_BEAM_WIDTH + 1,
        seed: 0,
      }),
    /beamWidth/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
        maxWork: MAX_SAMPLED_BEAM_WORK + 1,
        seed: 0,
      }),
    /maxWork/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
        seed: 0x1_0000_0000,
      }),
    /uint32/,
  );
  assert.throws(
    () =>
      evaluateSampledBeamMoves(game, {
        scenarios: 1,
        depth: 1,
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
