import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  playMove,
  seededRandom,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";
import {
  evaluateTunnelingAction,
  evaluateTunnelingState,
  extractTunnelingActionFeatures,
  extractTunnelingFeatures,
} from "./tunneling-heuristic.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(
  board: Board,
  nextDisc: DiscValue = 4,
  overrides: Partial<GameState> = {},
): GameState {
  return {
    board,
    nextDisc,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
    ...overrides,
  };
}

test("higher covers carry more danger and edge danger is explicit", () => {
  const low = singleCellBoard(6, 3, SOLID);
  const high = singleCellBoard(2, 3, SOLID);
  const center = extractTunnelingFeatures(position(low));
  const edge = extractTunnelingFeatures(
    position(singleCellBoard(6, 0, SOLID)),
  );

  assert.ok(
    extractTunnelingFeatures(position(high)).coveredAltitudeRisk >
      center.coveredAltitudeRisk,
  );
  assert.equal(edge.coveredAltitudeRisk, center.coveredAltitudeRisk);
  assert.ok(edge.edgeCoveredAltitudeRisk > center.edgeCoveredAltitudeRisk);
});

test("a trench earns access only for a covered cliff, not roughness alone", () => {
  const coveredWall = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, SOLID, E, E, E, E),
    row(E, E, SOLID, E, E, E, E),
    row(E, E, SOLID, E, E, E, E),
  ]);
  const blockedChannel = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, 7, SOLID, 7, E, E, E),
    row(E, 7, SOLID, 7, E, E, E),
    row(E, 7, SOLID, 7, E, E, E),
  ]);
  const numberedWall = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 7, E, E, E, E),
    row(E, E, 7, E, E, E, E),
    row(E, E, 7, E, E, E, E),
  ]);

  assert.ok(
    extractTunnelingFeatures(position(coveredWall)).coveredCliffAccess > 0,
  );
  assert.equal(
    extractTunnelingFeatures(position(blockedChannel)).coveredCliffAccess,
    0,
  );
  assert.equal(
    extractTunnelingFeatures(position(numberedWall)).coveredCliffAccess,
    0,
  );
});

test("coherent high-number columns store more tunnel work than split discs", () => {
  const together = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, 6, E, E, E),
    row(E, E, E, 6, E, E, E),
  ]);
  const split = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 6, E, 6, E, E),
  ]);

  assert.ok(
    extractTunnelingFeatures(position(together))
      .highNumberColumnCohesion >
      extractTunnelingFeatures(position(split)).highNumberColumnCohesion,
  );
});

test("action features value high-disc trench work and high edge damage", () => {
  const trench = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, SOLID, E, E, E, E),
    row(E, E, SOLID, E, E, E, E),
    row(E, E, SOLID, 6, E, E, E),
  ]);
  const trenchState = position(trench, 6);
  const trenchMove = playMove(trenchState, 3, seededRandom(1), {
    captureAnimation: true,
  });
  assert.ok(trenchMove);
  const trenchFeatures = extractTunnelingActionFeatures(
    trenchState,
    3,
    trenchMove,
  );
  assert.ok(trenchFeatures.highNumberCommitment > 0);
  assert.ok(trenchFeatures.highNumberTrenchProgress > 0);

  const centerState = position(singleCellBoard(6, 3, SOLID), 1);
  const edgeState = position(singleCellBoard(6, 0, SOLID), 1);
  const centerMove = playMove(centerState, 3, seededRandom(2), {
    captureAnimation: true,
  });
  const edgeMove = playMove(edgeState, 0, seededRandom(2), {
    captureAnimation: true,
  });
  assert.ok(centerMove && edgeMove);
  const centerHit = extractTunnelingActionFeatures(
    centerState,
    3,
    centerMove,
  );
  const edgeHit = extractTunnelingActionFeatures(edgeState, 0, edgeMove);
  assert.equal(edgeHit.coveredCrackAltitude, centerHit.coveredCrackAltitude);
  assert.ok(edgeHit.edgeCoverDamage > centerHit.edgeCoverDamage);
  assert.ok(
    evaluateTunnelingAction(edgeState, 0, edgeMove) >
      evaluateTunnelingAction(centerState, 3, centerMove),
  );
});

test("state and action features reflect exactly", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
    row(SOLID, 6, E, E, E, E, E),
  ]);
  const state = position(board, 6);
  const move = playMove(state, 1, seededRandom(3), {
    captureAnimation: true,
  });
  const reflectedState = position(mirrorBoard(board), 6);
  const reflectedMove = playMove(
    reflectedState,
    BOARD_SIZE - 1 - 1,
    seededRandom(3),
    { captureAnimation: true },
  );
  assert.ok(move && reflectedMove);

  assert.deepEqual(
    extractTunnelingFeatures(state),
    extractTunnelingFeatures(reflectedState),
  );
  assert.deepEqual(
    extractTunnelingActionFeatures(state, 1, move),
    extractTunnelingActionFeatures(reflectedState, 5, reflectedMove),
  );
  assert.equal(
    evaluateTunnelingState(state),
    evaluateTunnelingState(reflectedState),
  );
});

test("tunneling scales are bounded residuals over combined", () => {
  const state = position(singleCellBoard(3, 0, SOLID));
  assert.equal(
    evaluateTunnelingState(state),
    evaluateHeuristic(state, "combined"),
  );
  assert.equal(
    evaluateTunnelingState(state, 0),
    evaluateHeuristic(state, "combined"),
  );
  assert.equal(
    evaluateTunnelingState({ ...state, gameOver: true }),
    HEURISTIC_GAME_OVER_UTILITY,
  );
  assert.throws(() => evaluateTunnelingState(state, -1), /non-negative/);
  assert.throws(
    () => evaluateTunnelingState(state, Number.NaN),
    /finite/,
  );
});

function singleCellBoard(rowIndex: number, column: number, cell: Cell) {
  return boardFromRows(
    Array.from({ length: BOARD_SIZE }, (_, rowIndexCandidate) =>
      Array.from({ length: BOARD_SIZE }, (_, columnCandidate) =>
        rowIndexCandidate === rowIndex && columnCandidate === column
          ? cell
          : E,
      ),
    ),
  );
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
