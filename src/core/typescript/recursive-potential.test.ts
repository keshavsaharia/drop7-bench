import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  findPoppers,
  playMove,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";
import {
  MAX_RECURSIVE_PROPAGATION_WAVES,
  analyzeRecursivePotential,
  evaluateRecursivePotential,
} from "./recursive-potential.ts";

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

test("a physical seed propagates through multiple dependent waves", () => {
  const board = deepChainBoard();
  assert.deepEqual(findPoppers(board), []);
  const analysis = analyzeRecursivePotential(position(board));
  // Analysis is returned in its mirror-canonical orientation. This fixture
  // canonicalizes to the reflection of the board used by the engine below.
  const first = 6 * BOARD_SIZE + 6;
  const second = first - 1;
  const third = 5 * BOARD_SIZE + 5;

  assert.equal(analysis.physicalSeeds[first], 1);
  assert.equal(analysis.physicalSeeds[second], 0);
  assert.equal(analysis.physicalSeeds[third], 0);
  assert.ok(analysis.activation[second] > 0.7);
  assert.ok(analysis.activation[third] > 0.5);
  assert.ok(analysis.features.firstPropagationEnergy > 0);
  assert.ok(analysis.features.deepChainEnergy > 0);
  assert.ok(analysis.features.deepCoverExposure > 0);
  assert.ok(analysis.features.propagationWaves >= 2);
  assert.ok(
    analysis.features.propagationWaves <=
      MAX_RECURSIVE_PROPAGATION_WAVES,
  );

  const release = playMove(position(board, { nextDisc: 4 }), 3, () => 0.5);
  assert.ok(release);
  assert.deepEqual(
    release.waves.slice(0, 3).map((wave) => wave.cleared),
    [2, 1, 1],
  );
});

test("closed one and two cycles cannot create their own activation", () => {
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
  const analysis = analyzeRecursivePotential(position(board));
  assert.ok(analysis.physicalSeeds.every((value) => value === 0));
  assert.ok(analysis.activation.every((value) => value === 0));
  assert.equal(analysis.features.physicalSeedEnergy, 0);
  assert.equal(analysis.features.propagatedDiscEnergy, 0);
  assert.equal(analysis.features.deepChainEnergy, 0);
  assert.equal(analysis.features.propagationWaves, 0);
});

test("recursive analysis and evaluation are exactly mirror safe", () => {
  const board = deepChainBoard();
  const mirrored = mirrorBoard(board);
  const forward = analyzeRecursivePotential(position(board));
  const reverse = analyzeRecursivePotential(position(mirrored));

  assert.deepEqual(forward.features, reverse.features);
  assert.deepEqual(forward.activation, reverse.activation);
  assert.deepEqual(forward.physicalSeeds, reverse.physicalSeeds);
  assert.equal(
    evaluateRecursivePotential(position(board)),
    evaluateRecursivePotential(position(mirrored)),
  );
});

test("recursive profile is a bounded residual over combined", () => {
  const state = position(deepChainBoard());
  assert.equal(
    evaluateRecursivePotential(state, 0),
    evaluateHeuristic(state, "combined"),
  );
  assert.ok(
    evaluateRecursivePotential(state) > evaluateHeuristic(state, "combined"),
  );
  assert.equal(
    evaluateRecursivePotential({ ...state, gameOver: true }),
    HEURISTIC_GAME_OVER_UTILITY,
  );
  assert.throws(() => evaluateRecursivePotential(state, -1), /non-negative/);
  assert.throws(
    () => evaluateRecursivePotential(state, Number.NaN),
    /finite/,
  );
});

function deepChainBoard() {
  return boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, SOLID, E, E, E, E, E),
    row(SOLID, 2, SOLID, E, E, E, E),
    row(4, 2, SOLID, E, E, E, E),
  ]);
}

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored.push(board[rowIndex * BOARD_SIZE + column]);
    }
  }
  return mirrored;
}
