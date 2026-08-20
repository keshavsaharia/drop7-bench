import assert from "node:assert/strict";
import test from "node:test";
import {
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  createGame,
  type Cell,
  type GameState,
} from "./engine.ts";
import { evaluateMoves } from "./solver.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(board: readonly Cell[], nextDisc: GameState["nextDisc"]) {
  return {
    board,
    nextDisc,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  } satisfies GameState;
}

test("depth-one evaluation finds an immediate three-disc clear", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(3, 3, E, E, E, E, E),
  ]);
  const result = evaluateMoves(position(board, 3), {
    maxDepth: 1,
    timeLimitMs: 2_000,
  });

  assert.equal(result.depth, 1);
  assert.equal(result.complete, true);
  assert.equal(result.bestColumn, 2);
  assert.equal(
    result.columns.find((evaluation) => evaluation.column === 2)
      ?.expectedScore,
    70_021,
  );
});

test("the potential profile prefers a loaded chain over a seven-point pop", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(5, E, E, E, E, E, E),
    row(5, E, E, E, E, E, E),
    row(2, E, 6, 6, 6, 6, E),
  ]);
  const state = position(board, 5);

  const legacy = evaluateMoves(state, {
    maxDepth: 1,
    timeLimitMs: 2_000,
    heuristicProfile: "legacy",
  });
  const combined = evaluateMoves(state, {
    maxDepth: 1,
    timeLimitMs: 2_000,
    heuristicProfile: "combined",
  });

  assert.notEqual(legacy.bestColumn, 0);
  assert.equal(combined.bestColumn, 0);
});

test("symmetric starting positions receive symmetric evaluations", () => {
  const game = createGame(() => 0.5);
  const result = evaluateMoves(game, {
    maxDepth: 1,
    timeLimitMs: 2_000,
  });
  const values = new Map(
    result.columns.map((evaluation) => [evaluation.column, evaluation.value]),
  );

  assert.equal(values.get(0), values.get(6));
  assert.equal(values.get(1), values.get(5));
  assert.equal(values.get(2), values.get(4));
  assert.ok(result.cacheEntries > 0);
  assert.ok(result.cacheEntries <= 40_000);
  assert.ok(result.cacheHits > 0);
});

test("a full column is never returned as the best move", () => {
  const board = boardFromRows([
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
  ]);
  const result = evaluateMoves(position(board, 4), {
    maxDepth: 1,
    timeLimitMs: 2_000,
  });

  assert.notEqual(result.bestColumn, 0);
  assert.ok(result.columns.every((evaluation) => evaluation.column !== 0));
});

test("the iterative search accepts eight ply and clamps deeper requests", () => {
  const game = createGame(() => 0.5);
  let clock = 0;
  const result = evaluateMoves(game, {
    maxDepth: 99,
    timeLimitMs: 25,
    now: () => {
      const current = clock;
      clock += 30;
      return current;
    },
  });

  assert.equal(result.requestedDepth, 8);
  assert.equal(result.complete, false);
});

test("iterative deepening reports every completed depth", () => {
  const completedDepths: number[] = [];
  const result = evaluateMoves(createGame(() => 0.5), {
    maxDepth: 2,
    timeLimitMs: 2_000,
    onDepthComplete: (progress) => completedDepths.push(progress.depth),
  });

  assert.equal(result.complete, true);
  assert.deepEqual(completedDepths, [1, 2]);
});

test("a fixed work budget makes truncated searches reproducible", () => {
  const game = createGame(() => 0.5);
  const options = {
    maxDepth: 4,
    timeLimitMs: Number.POSITIVE_INFINITY,
    maxWork: 2_000,
  } as const;

  const first = evaluateMoves(game, options);
  const second = evaluateMoves(game, options);

  assert.equal(first.complete, false);
  assert.equal(first.work, second.work);
  assert.equal(first.nodes, second.nodes);
  assert.equal(first.depth, second.depth);
  assert.equal(first.bestColumn, second.bestColumn);
  assert.deepEqual(first.columns, second.columns);
});
