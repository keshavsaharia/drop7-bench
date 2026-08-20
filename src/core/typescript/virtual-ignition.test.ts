import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  findPoppers,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";
import {
  analyzeVirtualIgnition,
  evaluateVirtualIgnition,
} from "./virtual-ignition.ts";

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

test("a stored 7/6/5 column contains a two-wave virtual cascade", () => {
  const stored = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, 7, E, E, E),
    row(E, E, E, 6, E, E, E),
    row(E, E, E, 5, E, E, E),
  ]);
  const broken = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, 7, E, E, E),
    row(E, E, E, 3, E, E, E),
    row(E, E, E, 5, E, E, E),
  ]);
  assert.deepEqual(findPoppers(stored), []);
  const storedAnalysis = analyzeVirtualIgnition(position(stored));
  const brokenAnalysis = analyzeVirtualIgnition(position(broken));
  const seven = storedAnalysis.seeds.find((seed) => seed.value === 7);

  assert.ok(seven);
  assert.equal(seven.additionCost, 4);
  assert.equal(seven.readiness, 0.125);
  assert.equal(seven.downstreamWaves, 2);
  assert.equal(seven.downstreamClears, 2);
  assert.ok(seven.cascadeDepthEnergy > 0);
  assert.ok(
    storedAnalysis.features.cascadeDepthEnergy >
      brokenAnalysis.features.cascadeDepthEnergy,
  );
  assert.ok(
    evaluateVirtualIgnition(position(stored)) >
      evaluateVirtualIgnition(position(broken)),
  );
});

test("virtual ignition applies one-hit solid cracks and cracked reveals", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, CRACKED, 1, SOLID, E, E),
  ]);
  const analysis = analyzeVirtualIgnition(position(board));
  const seed = analysis.seeds.find((candidate) => candidate.value === 1);

  assert.ok(seed);
  assert.equal(seed.additionCost, 0);
  assert.equal(seed.initialCoverCracks, 1);
  assert.equal(seed.initialCoverReveals, 1);
  assert.ok(seed.coverReduction >= 1);
});

test("closed low-number cycles cannot ignite themselves", () => {
  const coverRow = row(SOLID, SOLID, E, SOLID, SOLID, SOLID, E);
  const board = boardFromRows([
    coverRow,
    coverRow,
    coverRow,
    coverRow,
    coverRow,
    coverRow,
    row(1, 1, E, 2, 2, 2, E),
  ]);
  assert.deepEqual(findPoppers(board), []);
  const analysis = analyzeVirtualIgnition(position(board));

  assert.deepEqual(analysis.seeds, []);
  assert.ok(
    Object.values(analysis.features).every((feature) => feature === 0),
  );
});

test("virtual ignition is mirror exact and independent of accumulated score", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, 7, E, E, E, E, E),
    row(E, 6, SOLID, E, E, E, E),
    row(E, 5, CRACKED, E, E, E, E),
  ]);
  const state = position(board);
  const forward = analyzeVirtualIgnition(state);
  const mirrored = analyzeVirtualIgnition(position(mirrorBoard(board)));

  assert.deepEqual(mirrored, forward);
  assert.equal(
    evaluateVirtualIgnition({ ...state, score: 1_000_000 }),
    evaluateVirtualIgnition(state),
  );
});

test("virtual ignition is a bounded residual over combined", () => {
  const state = position(boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, 7, E, E, E),
    row(E, E, E, 6, E, E, E),
    row(E, E, E, 5, E, E, E),
  ]));
  assert.equal(
    evaluateVirtualIgnition(state, 0),
    evaluateHeuristic(state, "combined"),
  );
  assert.equal(
    evaluateVirtualIgnition({ ...state, gameOver: true }),
    HEURISTIC_GAME_OVER_UTILITY,
  );
  assert.throws(() => evaluateVirtualIgnition(state, -1), /non-negative/);
  assert.throws(
    () => analyzeVirtualIgnition(state, { scenarios: 0 }),
    /scenarios/,
  );
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
