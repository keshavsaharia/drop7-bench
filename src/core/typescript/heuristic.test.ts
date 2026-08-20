import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  contiguousLineLength,
  findPoppers,
  isNumbered,
  playMove,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  extractHeuristicFeatures,
  type HeuristicProfileName,
} from "./heuristic.ts";

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

test("legacy profile exactly preserves the former horizon utility", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    row(E, E, 1, E, E, E, E),
    row(E, E, 7, E, E, E, E),
    row(E, 3, SOLID, E, E, E, E),
    row(E, 6, CRACKED, E, E, E, E),
    row(E, 4, 2, E, E, E, E),
  ]);
  const state = position(board);

  assert.equal(evaluateHeuristic(state, "legacy"), legacyReference(state));
  assert.equal(
    evaluateHeuristic({ ...state, gameOver: true }, "legacy"),
    HEURISTIC_GAME_OVER_UTILITY,
  );
});

test("combined is the default heuristic profile", () => {
  const state = position(boardFromRows(Array.from({ length: 7 }, blank)));
  assert.equal(DEFAULT_HEURISTIC_PROFILE, "combined");
  assert.equal(evaluateHeuristic(state), evaluateHeuristic(state, "combined"));
});

test("an externally triggerable tower carries latent chain energy", () => {
  const loaded = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(4, E, E, E, E, E, E),
    row(4, E, E, E, E, E, E),
    row(2, E, E, E, E, E, E),
  ]);
  const neutral = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(6, E, E, E, E, E, E),
    row(7, E, E, E, E, E, E),
    row(2, E, E, E, E, E, E),
  ]);

  assert.deepEqual(findPoppers(loaded), []);
  assert.deepEqual(findPoppers(neutral), []);
  const loadedFeatures = extractHeuristicFeatures(position(loaded));
  const neutralFeatures = extractHeuristicFeatures(position(neutral));
  assert.ok(loadedFeatures.directPotential > neutralFeatures.directPotential);
  assert.ok(
    loadedFeatures.latentChainPotential >
      neutralFeatures.latentChainPotential,
  );
  assert.ok(
    evaluateHeuristic(position(loaded), "potential") >
      evaluateHeuristic(position(neutral), "potential"),
  );

  const move = playMove(position(loaded, { nextDisc: 7 }), 0, () => 0.5);
  assert.ok(move);
  assert.deepEqual(
    move.waves.map((wave) => wave.cleared),
    [2, 1],
  );
});

test("a quiet placement can store a larger chain than an immediate clear", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(5, E, E, E, E, E, E),
    row(5, E, E, E, E, E, E),
    row(2, E, 6, 6, 6, 6, E),
  ]);
  const state = position(board, { nextDisc: 5, movesRemaining: 4 });
  const setup = playMove(state, 0, () => 0.5);
  const immediate = playMove(state, 6, () => 0.5);

  assert.ok(setup);
  assert.ok(immediate);
  assert.deepEqual(setup.waves, []);
  assert.equal(immediate.scoreDelta, 7);
  const setupFeatures = extractHeuristicFeatures(setup.state);
  const immediateFeatures = extractHeuristicFeatures(immediate.state);
  assert.ok(
    setupFeatures.directPotential + setupFeatures.latentChainPotential >
      immediateFeatures.directPotential +
        immediateFeatures.latentChainPotential,
  );
  assert.ok(
    evaluateHeuristic(setup.state, "potential") >
      immediate.scoreDelta + evaluateHeuristic(immediate.state, "potential"),
  );

  const released = playMove(
    { ...setup.state, nextDisc: 7 },
    0,
    () => 0.5,
  );
  assert.ok(released);
  assert.deepEqual(
    released.waves.map((wave) => wave.cleared),
    [3, 1],
  );
  assert.equal(released.scoreDelta, 60);
});

test("buried adjacent ones retain an escape-aware edge penalty", () => {
  const adjacent = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 7, 7, 7, E, E),
    row(E, E, 1, 1, 7, E, E),
  ]);
  const separated = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 7, 7, 7, E, E),
    row(E, E, 1, 7, 1, E, E),
  ]);

  assert.deepEqual(findPoppers(adjacent), []);
  assert.deepEqual(findPoppers(separated), []);
  assert.ok(extractHeuristicFeatures(position(adjacent)).adjacentOnes > 0.8);
  assert.equal(extractHeuristicFeatures(position(separated)).adjacentOnes, 0);
  assert.ok(
    evaluateHeuristic(position(adjacent), "anti-clog") <
      evaluateHeuristic(position(separated), "anti-clog"),
  );
});

test("adjacent-one edges discount perpendicular release routes on both axes", () => {
  const horizontalPair = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(4, 7, E, E, E, E, E),
    row(4, 7, E, E, E, E, E),
    row(1, 1, E, E, E, E, E),
  ]);
  const verticalPair = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(1, 3, E, E, E, E, E),
    row(1, 3, E, E, E, E, E),
  ]);

  assert.deepEqual(findPoppers(horizontalPair), []);
  assert.deepEqual(findPoppers(verticalPair), []);
  assert.equal(
    extractHeuristicFeatures(position(horizontalPair)).adjacentOnes,
    0.5,
  );
  assert.equal(
    extractHeuristicFeatures(position(verticalPair)).adjacentOnes,
    0,
  );

  const horizontalRelease = playMove(
    position(horizontalPair, { nextDisc: 4 }),
    0,
    () => 0.5,
  );
  const verticalRelease = playMove(
    position(verticalPair, { nextDisc: 3 }),
    1,
    () => 0.5,
  );
  assert.ok(horizontalRelease);
  assert.ok(verticalRelease);
  assert.deepEqual(
    horizontalRelease.waves.map((wave) => wave.cleared),
    [3, 1, 1],
  );
  assert.deepEqual(
    verticalRelease.waves.map((wave) => wave.cleared),
    [3, 2],
  );
});

test("buried consecutive twos retain an escape-aware run penalty", () => {
  const triple = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, 6, 6, 6, 6, E, E),
    row(E, 7, 7, 7, 7, E, E),
    row(E, 2, 2, 2, 7, E, E),
  ]);
  const split = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, 6, 6, 6, 6, E, E),
    row(E, 7, 7, 7, 7, E, E),
    row(E, 2, 2, 7, 2, E, E),
  ]);

  assert.deepEqual(findPoppers(triple), []);
  assert.deepEqual(findPoppers(split), []);
  assert.ok(extractHeuristicFeatures(position(triple)).tripleTwos > 0.5);
  assert.equal(extractHeuristicFeatures(position(split)).tripleTwos, 0);
  assert.ok(
    evaluateHeuristic(position(triple), "anti-clog") <
      evaluateHeuristic(position(split), "anti-clog"),
  );
});

test("a bottom-row run of twos is not clogged when it can fire vertically", () => {
  const reachable = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, 2, 2, 2, 7, E, E),
  ]);

  assert.deepEqual(findPoppers(reachable), []);
  const features = extractHeuristicFeatures(position(reachable));
  assert.equal(features.tripleTwos, 0);
  assert.ok(features.directPotential >= 3);
});

test("all named profiles and features are mirror invariant", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    row(E, E, E, E, E, 6, E),
    row(E, 4, E, E, E, 5, E),
    row(E, 4, SOLID, E, E, 7, E),
    row(E, 2, CRACKED, 1, 1, 7, E),
  ]);
  const mirrored = mirrorBoard(board);
  const state = position(board);
  const mirrorState = position(mirrored);

  assert.deepEqual(
    extractHeuristicFeatures(state),
    extractHeuristicFeatures(mirrorState),
  );
  for (const profile of [
    "legacy",
    "survival",
    "potential",
    "anti-clog",
    "combined",
  ] satisfies HeuristicProfileName[]) {
    assert.equal(
      evaluateHeuristic(state, profile),
      evaluateHeuristic(mirrorState, profile),
    );
  }
});

function legacyReference(state: GameState) {
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;

  let utility = 0;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (state.board[column] === EMPTY) utility += 180;
  }
  for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = state.board[rowIndex * BOARD_SIZE + column];
      if (cell === EMPTY) continue;

      const height = BOARD_SIZE - rowIndex;
      utility -= height * height * 10;
      if (cell === SOLID) utility -= 620;
      if (cell === CRACKED) utility -= 220;
      if (!isNumbered(cell)) continue;

      utility -= 18;
      const rowDistance = Math.abs(
        cell -
          contiguousLineLength(state.board, rowIndex, column, "row"),
      );
      const columnDistance = Math.abs(
        cell -
          contiguousLineLength(state.board, rowIndex, column, "column"),
      );
      if (Math.min(rowDistance, columnDistance) === 1) utility += 55;
      if (cell <= 2 && height >= 5) utility -= 90;
    }
  }
  return utility;
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
