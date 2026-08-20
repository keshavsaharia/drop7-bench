import assert from "node:assert/strict";
import test from "node:test";

import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import { HEURISTIC_GAME_OVER_UTILITY } from "./heuristic.ts";
import {
  evaluatePhaseHorizon,
  extractPhaseHorizonFeatures,
} from "./phase-horizon-evaluator.ts";

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

test("the same load carries more projected debt near a rise", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(7, 6, 5, 4, 5, 6, 7),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    row(7, 6, 5, 4, 5, 6, 7),
  ]);
  const far = extractPhaseHorizonFeatures(
    position(board, 4, { movesRemaining: 5 }),
  );
  const imminent = extractPhaseHorizonFeatures(
    position(board, 4, { movesRemaining: 1 }),
  );

  assert.ok(imminent.projectedOccupancyDebt > far.projectedOccupancyDebt);
  assert.ok(imminent.residualCoverDebt > far.residualCoverDebt);
  assert.ok(
    imminent.imminentCoverAltitudeDebt > far.imminentCoverAltitudeDebt,
  );
  assert.ok(imminent.peakHeightRisk > far.peakHeightRisk);
});

test("covered altitude and low-number caps are explicit debt", () => {
  const clear = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 6, 6, E, E, E),
    row(E, E, SOLID, SOLID, E, E, E),
  ]);
  const capped = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 1, 2, E, E, E),
    row(E, E, SOLID, SOLID, E, E, E),
  ]);
  const clearFeatures = extractPhaseHorizonFeatures(position(clear));
  const cappedFeatures = extractPhaseHorizonFeatures(position(capped));

  assert.equal(clearFeatures.lowCapLoad, 0);
  assert.ok(cappedFeatures.lowCapLoad > 0);
  assert.ok(cappedFeatures.adjacentLowCapLoad > 0);
  assert.ok(evaluatePhaseHorizon(position(clear)) > evaluatePhaseHorizon(position(capped)));
});

test("quiet build inventory is distinct from immediate triggers", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const quiet = extractPhaseHorizonFeatures(position(board, 7));
  const trigger = extractPhaseHorizonFeatures(position(board, 2));

  assert.equal(quiet.quietBuildOptions, BOARD_SIZE);
  assert.ok(quiet.quietDirectGain > 0);
  assert.equal(quiet.triggerReadiness, 0);
  assert.equal(trigger.quietBuildOptions, 0);
  assert.ok(trigger.triggerReadiness > 0);
});

test("phase features and evaluation are mirror invariant", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, 5, E, E, E),
    row(2, E, E, 4, E, E, E),
    row(SOLID, E, 6, SOLID, E, E, E),
  ]);
  const forward = position(board, 6, { movesRemaining: 2 });
  const reverse = position(mirrorBoard(board), 6, { movesRemaining: 2 });

  assert.deepEqual(
    extractPhaseHorizonFeatures(forward),
    extractPhaseHorizonFeatures(reverse),
  );
  assert.equal(evaluatePhaseHorizon(forward), evaluatePhaseHorizon(reverse));
});

test("terminal and invalid weights are bounded", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const state = position(board);

  assert.equal(
    evaluatePhaseHorizon({ ...state, gameOver: true }),
    HEURISTIC_GAME_OVER_UTILITY,
  );
  assert.throws(
    () =>
      evaluatePhaseHorizon(state, {
        baselineScale: Number.NaN,
        projectedOccupancyDebt: 0,
        residualCoverDebt: 0,
        coverAltitudeDebt: 0,
        imminentCoverAltitudeDebt: 0,
        peakHeightRisk: 0,
        lowCapLoad: 0,
        adjacentLowCapLoad: 0,
        directBuildInventory: 0,
        quietBuildOptions: 0,
        quietDirectGain: 0,
        triggerReadiness: 0,
        releaseReadiness: 0,
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
