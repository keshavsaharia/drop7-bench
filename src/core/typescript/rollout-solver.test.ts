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
  HEURISTIC_GAME_OVER_UTILITY,
} from "./heuristic.ts";
import {
  MAX_CONTINUATION_SAMPLES,
  MAX_ROLLOUT_HORIZON,
  evaluateRolloutMoves,
  type RolloutContinuationPolicy,
} from "./rollout-solver.ts";

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

test("rollout evaluation is deterministic and keeps bounded statistics", () => {
  const game = createGame(() => 0.5);
  const options = {
    rollouts: 12,
    horizon: 6,
    continuationSamples: 3,
    seed: 0xdecafbad,
    heuristicProfile: "combined" as const,
  };
  const first = evaluateRolloutMoves(game, options);
  const second = evaluateRolloutMoves(game, options);

  assert.deepEqual(first, second);
  assert.equal(first.columns.length, BOARD_SIZE);
  assert.ok(first.columns.every((column) => column.rollouts === 12));
  assert.ok(first.columns.every((column) => column.variance >= 0));
  assert.ok(first.work > first.columns.length * options.rollouts);
});

test("root columns use the same samples on a symmetric board", () => {
  const game = createGame(() => 0.5);
  const result = evaluateRolloutMoves(game, {
    rollouts: 8,
    horizon: 1,
    seed: 123,
    heuristicProfile: "combined",
  });
  const values = new Map(
    result.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  assert.deepEqual(values.get(0), {
    ...values.get(6),
    column: 0,
  });
  assert.deepEqual(values.get(1), {
    ...values.get(5),
    column: 1,
  });
  assert.deepEqual(values.get(2), {
    ...values.get(4),
    column: 2,
  });
});

test("future discs are stratified across complete groups of seven", () => {
  const seen = new Set<number>();
  const result = evaluateRolloutMoves(createGame(() => 0.5), {
    rollouts: BOARD_SIZE,
    horizon: 2,
    stratifiedSamples: true,
    seed: 0x5157,
    evaluator: (state) => {
      seen.add(state.nextDisc);
      return 0;
    },
  });

  assert.equal(result.columns.length, BOARD_SIZE);
  assert.equal(result.stratifiedSamples, true);
  assert.deepEqual([...seen].sort((a, b) => a - b), [1, 2, 3, 4, 5, 6, 7]);
});

test("mirrored positions receive mirrored rollout evaluations", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(4, E, E, E, E, E, E),
    row(3, E, 6, 7, E, E, E),
  ]);
  const mirror = mirrorBoard(board);
  const options = {
    rollouts: 10,
    horizon: 4,
    continuationSamples: 4,
    seed: 9876,
    heuristicProfile: "combined" as const,
  };
  const forward = evaluateRolloutMoves(position(board), options);
  const mirrored = evaluateRolloutMoves(position(mirror), options);
  const mirroredValues = new Map(
    mirrored.columns.map((evaluation) => [evaluation.column, evaluation]),
  );

  for (const evaluation of forward.columns) {
    const opposite = mirroredValues.get(BOARD_SIZE - 1 - evaluation.column);
    assert.ok(opposite);
    assert.ok(Math.abs(evaluation.mean - opposite.mean) < 1e-9);
    assert.ok(Math.abs(evaluation.variance - opposite.variance) < 1e-9);
  }
  assert.equal(
    forward.bestColumn,
    mirrored.bestColumn === null
      ? null
      : BOARD_SIZE - 1 - mirrored.bestColumn,
  );
});

test("horizon-one work and a custom leaf evaluator are predictable", () => {
  const result = evaluateRolloutMoves(createGame(() => 0.5), {
    rollouts: 3,
    horizon: 1,
    seed: 7,
    evaluator: () => 123,
  });

  assert.equal(result.work, BOARD_SIZE * 3 * 2);
  assert.equal(result.continuationSamples, 1);
  assert.ok(result.columns.every((column) => column.mean === 123));
  assert.ok(result.columns.every((column) => column.variance === 0));
});

test("one continuation sample preserves the default midpoint policy", () => {
  const game = createGame(() => 0.5);
  const shared = {
    rollouts: 4,
    horizon: 4,
    seed: 29,
    heuristicProfile: "combined" as const,
  };
  const implicit = evaluateRolloutMoves(game, shared);
  const explicit = evaluateRolloutMoves(game, {
    ...shared,
    continuationSamples: 1,
  });

  assert.deepEqual(implicit, explicit);
});

test("an observable continuation policy controls moves after the root", () => {
  const calls: Readonly<GameState>[] = [];
  const policy: RolloutContinuationPolicy = (...arguments_) => {
    assert.equal(arguments_.length, 1);
    const [state] = arguments_;
    calls.push(state);
    return [3, 2, 4, 1, 5, 0, 6].find(
      (column) => state.board[column] === EMPTY,
    ) ?? null;
  };
  const result = evaluateRolloutMoves(createGame(() => 0.5), {
    rollouts: 3,
    horizon: 2,
    continuationPolicy: policy,
    seed: 0x5151,
    evaluator: () => 0,
  });

  assert.equal(calls.length, BOARD_SIZE * 3);
  assert.ok(calls.every((state) => state.nextDisc >= 1 && state.nextDisc <= 7));
  assert.equal(result.bestColumn, 3);
  // Per root rollout: two moves and one final evaluator call. Policy work is
  // intentionally opaque and is not misreported as simulated engine work.
  assert.equal(result.work, BOARD_SIZE * 3 * 3);
});

test("an invalid continuation-policy move fails at the policy boundary", () => {
  assert.throws(
    () =>
      evaluateRolloutMoves(createGame(() => 0.5), {
        rollouts: 1,
        horizon: 2,
        continuationPolicy: () => BOARD_SIZE,
        seed: 1,
      }),
    /continuation policy returned illegal column 7/,
  );
});

test("risk aversion is deterministic, reported, and validated", () => {
  const game = createGame(() => 0.5);
  const result = evaluateRolloutMoves(game, {
    rollouts: 8,
    horizon: 4,
    riskAversion: 0.5,
    seed: 29,
  });

  assert.equal(result.riskAversion, 0.5);
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 1,
        horizon: 1,
        riskAversion: -1,
        seed: 0,
      }),
    /riskAversion/,
  );
});

test("continuation sample work scales without retaining sample outcomes", () => {
  const continuationSamples = 3;
  const result = evaluateRolloutMoves(createGame(() => 0.5), {
    rollouts: 1,
    horizon: 2,
    continuationSamples,
    seed: 77,
    evaluator: () => 123,
  });

  // Per root: two actual moves and one final evaluation, plus each of seven
  // greedy candidates receiving one move and one evaluation per probe.
  assert.equal(
    result.work,
    BOARD_SIZE * (3 + BOARD_SIZE * continuationSamples * 2),
  );
  assert.equal(result.continuationSamples, continuationSamples);
});

test("terminal rollouts receive the terminal penalty", () => {
  const board = boardFromRows([
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, E),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const result = evaluateRolloutMoves(position(board, { nextDisc: 6 }), {
    rollouts: 4,
    horizon: 10,
    seed: 1,
  });

  assert.equal(result.bestColumn, 6);
  assert.equal(result.columns.length, 1);
  assert.equal(result.columns[0].mean, HEURISTIC_GAME_OVER_UTILITY);
  assert.equal(result.columns[0].variance, 0);
  assert.equal(result.work, 4);
});

test("rollout inputs are bounded and validated", () => {
  const game = createGame(() => 0.5);
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 0,
        horizon: 1,
        seed: 0,
      }),
    /rollouts/,
  );
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 1,
        horizon: MAX_ROLLOUT_HORIZON + 1,
        seed: 0,
      }),
    /horizon/,
  );
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 1,
        horizon: 1,
        continuationSamples: MAX_CONTINUATION_SAMPLES + 1,
        seed: 0,
      }),
    /continuationSamples/,
  );
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 1,
        horizon: 1,
        seed: -1,
      }),
    /uint32/,
  );
  assert.throws(
    () =>
      evaluateRolloutMoves(game, {
        rollouts: 1,
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
