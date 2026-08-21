/**
 * Parity gates for the browser solver against the reference TypeScript
 * engine and solver in src/core/typescript. Every comparison is exact: the
 * fast path must produce the same outcomes in the same order with the same
 * probabilities, bit-identical leaf utilities and column values, and the same
 * work, node and cache counts, on real positions reached through play.
 *
 *   cd web && npm test
 */
import assert from "node:assert/strict";
import test from "node:test";
import {
  SearchAbortedError,
  createGame,
  forEachMoveOutcome,
  legalColumns,
  playMove,
  seededRandom,
  serializeBoard,
  type Board,
  type DiscValue,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  HEURISTIC_PROFILES,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "../../../src/core/typescript/heuristic.ts";
import { evaluateMoves } from "../../../src/core/typescript/solver.ts";
import { FastLeaf } from "./fast-leaf.ts";
import {
  fastEvaluateMoves,
  fastForEachMoveOutcome,
  type SearchState,
} from "./fast-search.ts";

const CORPUS_SEED = 0x5eed_f457;
const PROFILES = Object.keys(HEURISTIC_PROFILES) as HeuristicProfileName[];

interface Outcome {
  board: string;
  nextDisc: number;
  level: number;
  movesRemaining: number;
  gameOver: boolean;
  scoreDelta: number;
  probability: number;
}

/** Positions reached by random play and by shallow-search play, for variety of shape. */
function collectStates(): GameState[] {
  const states: GameState[] = [];
  const chooser = seededRandom(CORPUS_SEED);
  const pushGame = (seed: number, pickColumn: (state: GameState) => number, maxMoves: number) => {
    const random = seededRandom(seed);
    let state = createGame(random);
    for (let move = 0; move < maxMoves && !state.gameOver; move += 1) {
      states.push(state);
      const result = playMove(state, pickColumn(state), random, { captureAnimation: false });
      if (!result) break;
      state = result.state;
    }
    states.push(state);
  };
  const randomColumn = (state: GameState) => {
    const legal = legalColumns(state.board);
    return legal[Math.floor(chooser() * legal.length)];
  };
  const searchColumn = (state: GameState) =>
    evaluateMoves(state, { maxDepth: 1, maxWork: 50_000, now: () => 0 }).bestColumn ?? randomColumn(state);

  for (let game = 0; game < 24; game += 1) {
    pushGame((CORPUS_SEED + game * 0x9e3779b9) >>> 0, randomColumn, 120);
  }
  for (let game = 0; game < 6; game += 1) {
    pushGame((CORPUS_SEED ^ (0x1234567 + game * 0x85ebca6b)) >>> 0, searchColumn, 90);
  }
  return states;
}

const STATES = collectStates();

function every<T>(items: readonly T[], step: number): T[] {
  return items.filter((_, index) => index % step === 0);
}

/**
 * Gray-heavy boards can have tens of thousands of exact outcomes for one
 * column (every reveal multiplies the tree by seven), so enumeration is
 * bounded the way the search bounds it: a counting stop check, identical on
 * both sides. The comparison covers the outcomes emitted before the abort and
 * whether an abort happened at all.
 */
const OUTCOME_BUDGET = 4_000;

function budgetedStop() {
  let calls = 0;
  return () => ++calls > OUTCOME_BUDGET;
}

function referenceOutcomes(state: GameState, column: number): Outcome[] {
  const outcomes: Outcome[] = [];
  try {
    forEachMoveOutcome(state, column, (outcome) => {
    outcomes.push({
      board: serializeBoard(outcome.state.board),
      nextDisc: outcome.state.nextDisc,
      level: outcome.state.level,
      movesRemaining: outcome.state.movesRemaining,
      gameOver: outcome.state.gameOver,
      scoreDelta: outcome.scoreDelta,
      probability: outcome.probability,
    });
    }, budgetedStop());
  } catch {
    outcomes.push({ board: "aborted", nextDisc: 0, level: 0, movesRemaining: 0, gameOver: false, scoreDelta: 0, probability: 0 });
  }
  return outcomes;
}

function fastOutcomes(state: SearchState, column: number): Outcome[] {
  const outcomes: Outcome[] = [];
  try {
    fastForEachMoveOutcome(state, column, (board, nextDisc, level, movesRemaining, gameOver, scoreDelta, probability) => {
      outcomes.push({
        board: board.join(""),
        nextDisc,
        level,
        movesRemaining,
        gameOver,
        scoreDelta,
        probability,
      });
    }, budgetedStop());
  } catch {
    outcomes.push({ board: "aborted", nextDisc: 0, level: 0, movesRemaining: 0, gameOver: false, scoreDelta: 0, probability: 0 });
  }
  return outcomes;
}

function stripElapsed<T extends { elapsedMs: number }>(result: T) {
  const { elapsedMs, ...rest } = result;
  void elapsedMs;
  return rest;
}

test("corpus covers live and terminal positions with covers in play", () => {
  assert.ok(STATES.length > 800, `only ${STATES.length} states`);
  assert.ok(STATES.some((state) => state.gameOver));
  assert.ok(STATES.some((state) => state.movesRemaining === 1));
  assert.ok(STATES.some((state) => state.board.includes(9)));
});

test("fast leaf is bit-identical to evaluateHeuristic for every profile", () => {
  for (const profile of PROFILES) {
    const leaf = new FastLeaf(HEURISTIC_PROFILES[profile]);
    let checked = 0;
    for (const state of STATES) {
      if (state.gameOver) continue;
      const expected = evaluateHeuristic(state, profile);
      const actual = leaf.evaluate(state.board);
      assert.ok(
        Object.is(expected, actual),
        `${profile}: ${serializeBoard(state.board)} expected ${expected} got ${actual}`,
      );
      checked += 1;
    }
    assert.ok(checked > 700);
  }
});

test("fast leaf sees every chance outcome identically, not just root boards", () => {
  const leaf = new FastLeaf(HEURISTIC_PROFILES.combined);
  let checked = 0;
  for (const state of every(STATES, 9)) {
    for (const column of legalColumns(state.board)) {
      try {
        forEachMoveOutcome(state, column, (outcome) => {
          if (outcome.state.gameOver) return;
          const expected = evaluateHeuristic(outcome.state, "combined");
          const actual = leaf.evaluate(outcome.state.board);
          assert.ok(Object.is(expected, actual), serializeBoard(outcome.state.board));
          checked += 1;
        }, budgetedStop());
      } catch (error) {
        if (!(error instanceof SearchAbortedError)) throw error;
      }
    }
  }
  assert.ok(checked > 5_000, `only ${checked} leaves`);
});

test("fast move generator streams the reference outcomes in order with exact probabilities", () => {
  let compared = 0;
  let aborted = 0;
  for (const state of every(STATES, 3)) {
    for (const column of legalColumns(state.board)) {
      const expected = referenceOutcomes(state, column);
      const actual = fastOutcomes(state, column);
      assert.deepEqual(actual, expected, `${serializeBoard(state.board)} disc ${state.nextDisc} column ${column}`);
      compared += expected.length;
      if (expected.at(-1)?.board === "aborted") aborted += 1;
    }
  }
  assert.ok(compared > 20_000, `only ${compared} outcomes`);
  assert.ok(aborted > 0, "expected at least one budget-bounded enumeration");
});

test("fast move generator settles a caller-supplied unsettled board like the reference", () => {
  // A floating 5 in column 0 and a floating gray in column 6; dropping the 3
  // completes a row of three and the wave's gravity must settle every column.
  const rows = [
    "0000000",
    "5000000",
    "0000008",
    "0000000",
    "0000000",
    "0000000",
    "0330000",
  ];
  const board = rows.join("").split("").map(Number) as unknown as Board;
  const state: GameState = {
    board,
    nextDisc: 3 as DiscValue,
    score: 0,
    level: 1,
    movesRemaining: 3,
    movesPlayed: 0,
    gameOver: false,
  };
  assert.deepEqual(fastOutcomes(state, 3), referenceOutcomes(state, 3));
  assert.deepEqual(fastOutcomes(state, 0), referenceOutcomes(state, 0));
});

test("fast move generator ignores illegal columns and terminal states like the reference", () => {
  const state = STATES.find((candidate) => legalColumns(candidate.board).length < 7 && !candidate.gameOver);
  assert.ok(state);
  const full = [0, 1, 2, 3, 4, 5, 6].find((column) => state.board[column] !== 0)!;
  assert.deepEqual(fastOutcomes(state, full), []);
  assert.deepEqual(fastOutcomes(state, -1), []);
  assert.deepEqual(fastOutcomes(state, 7), []);
  assert.deepEqual(fastOutcomes(state, 2.5), []);
  const over = STATES.find((candidate) => candidate.gameOver)!;
  assert.deepEqual(fastOutcomes(over, 0), referenceOutcomes(over, 0));
});

test("completed depth-2 searches match the reference exactly", () => {
  for (const state of every(STATES, 10)) {
    const options = { maxDepth: 2, maxWork: 120_000, now: () => 0 };
    const expected = stripElapsed(evaluateMoves(state, options));
    const actual = stripElapsed(fastEvaluateMoves(state, options));
    assert.deepEqual(actual, expected, serializeBoard(state.board));
  }
});

test("completed depth-3 searches match the reference exactly, per profile", () => {
  const sample = every(STATES.filter((state) => !state.gameOver), 61);
  for (const [index, state] of sample.entries()) {
    const heuristicProfile = PROFILES[index % PROFILES.length];
    const options = { maxDepth: 3, maxWork: 600_000, heuristicProfile, now: () => 0 };
    const expected = stripElapsed(evaluateMoves(state, options));
    const actual = stripElapsed(fastEvaluateMoves(state, options));
    assert.deepEqual(actual, expected, `${heuristicProfile} ${serializeBoard(state.board)}`);
  }
});

test("a depth-4 search matches the reference exactly, including cache eviction", () => {
  const state = STATES.filter((candidate) => !candidate.gameOver && candidate.movesRemaining === 3)[5];
  assert.ok(state);
  const options = { maxDepth: 4, maxWork: 3_200_000, now: () => 0 };
  const expected = stripElapsed(evaluateMoves(state, options));
  const actual = stripElapsed(fastEvaluateMoves(state, options));
  assert.ok(expected.work > 100_000, `work ${expected.work}`);
  assert.ok(expected.cacheEntries > 0);
  assert.deepEqual(actual, expected);
});

test("work-limited searches abort at the same point and report the same partial result", () => {
  const budgets = [1_000, 7_777, 40_000, 150_000];
  const sample = every(STATES.filter((state) => !state.gameOver), 29);
  for (const [index, state] of sample.entries()) {
    const maxWork = budgets[index % budgets.length];
    const options = { maxDepth: 4, maxWork, now: () => 0 };
    const expected = stripElapsed(evaluateMoves(state, options));
    const actual = stripElapsed(fastEvaluateMoves(state, options));
    assert.deepEqual(actual, expected, `${maxWork} ${serializeBoard(state.board)}`);
  }
});

test("depth-complete callbacks fire with identical intermediate results", () => {
  const state = STATES.filter((candidate) => !candidate.gameOver)[77];
  const expected: unknown[] = [];
  const actual: unknown[] = [];
  evaluateMoves(state, { maxDepth: 3, maxWork: 600_000, now: () => 0, onDepthComplete: (r) => expected.push(stripElapsed(r)) });
  fastEvaluateMoves(state, { maxDepth: 3, maxWork: 600_000, now: () => 0, onDepthComplete: (r) => actual.push(stripElapsed(r)) });
  assert.ok(expected.length >= 2, `only ${expected.length} depths completed`);
  assert.deepEqual(actual, expected);
});

test("timing: fast search against reference on the same decisions (informational)", () => {
  const sample = every(STATES.filter((state) => !state.gameOver && state.movesRemaining !== 1), 53).slice(0, 10);
  const options = { maxDepth: 3, maxWork: 600_000, now: () => 0 };
  let referenceMs = 0;
  let fastMs = 0;
  // Interleaved so a drifting machine load cannot fake a ratio (finding-13 §1).
  for (let repeat = 0; repeat < 2; repeat += 1) {
    for (const state of sample) {
      let started = performance.now();
      evaluateMoves(state, options);
      referenceMs += performance.now() - started;
      started = performance.now();
      fastEvaluateMoves(state, options);
      fastMs += performance.now() - started;
    }
  }
  const ratio = referenceMs / fastMs;
  console.log(
    `depth-3 decisions × ${sample.length * 2}: reference ${referenceMs.toFixed(0)} ms, fast ${fastMs.toFixed(0)} ms, ratio ${ratio.toFixed(2)}x (this machine, not exclusive; informational)`,
  );
  assert.ok(Number.isFinite(ratio));
});
