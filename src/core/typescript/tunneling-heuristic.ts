import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  SOLID,
  isNumbered,
  type Board,
  type GameState,
  type MoveResult,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";

/**
 * Experimental features for the human "tunneling" style of Hardcore play.
 *
 * A cliff is useful only when its exposed wall contains covered discs. This
 * intentionally does not reward generic board roughness: a jagged wall of
 * numbered discs has no tunneling value by itself.
 */
export interface TunnelingFeatures {
  coveredAltitudeRisk: number;
  edgeCoveredAltitudeRisk: number;
  coveredCliffAccess: number;
  edgeCliffAccess: number;
  highNumberColumnCohesion: number;
  highNumberCliffCohesion: number;
}

export interface TunnelingActionFeatures {
  highNumberCommitment: number;
  highNumberTrenchProgress: number;
  coveredCrackAltitude: number;
  coveredRevealAltitude: number;
  edgeCoverDamage: number;
}

export type TunnelingWeights = Readonly<
  Record<keyof TunnelingFeatures, number>
>;

export type TunnelingActionWeights = Readonly<
  Record<keyof TunnelingActionFeatures, number>
>;

export const TUNNELING_WEIGHTS: TunnelingWeights = {
  coveredAltitudeRisk: -45,
  edgeCoveredAltitudeRisk: -65,
  coveredCliffAccess: 90,
  edgeCliffAccess: 110,
  highNumberColumnCohesion: 240,
  highNumberCliffCohesion: 180,
};

export const TUNNELING_ACTION_WEIGHTS: TunnelingActionWeights = {
  highNumberCommitment: 160,
  highNumberTrenchProgress: 200,
  coveredCrackAltitude: 100,
  coveredRevealAltitude: 180,
  edgeCoverDamage: 120,
};

// Paired validation supported the transition bonus, but not the state
// residual. Keep the latter available for ablations without enabling it by
// accident in future callers.
export const DEFAULT_TUNNELING_STATE_SCALE = 0;
export const DEFAULT_TUNNELING_ACTION_SCALE = 0.25;

/**
 * Measure whether the board is keeping dangerous covers attackable.
 *
 * Covered discs become more dangerous quadratically with altitude. Edge
 * covers carry a separate debt because they have one fewer horizontal attack
 * direction. Conversely, a low neighboring column earns cliff access only
 * for the covered cells that can actually be reached from that channel.
 * Finally, repeated 5/6/7 discs in a still-live column get a small cohesion
 * credit: additions advance all of them toward the same vertical trigger.
 */
export function extractTunnelingFeatures(
  state: GameState,
): TunnelingFeatures {
  const board = state.board;
  const heights = columnHeights(board);
  const features: TunnelingFeatures = {
    coveredAltitudeRisk: 0,
    edgeCoveredAltitudeRisk: 0,
    coveredCliffAccess: 0,
    edgeCliffAccess: 0,
    highNumberColumnCohesion: 0,
    highNumberCliffCohesion: 0,
  };

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = board[indexOf(row, column)];
      if (cell !== SOLID && cell !== CRACKED) continue;

      const coverFactor = cell === SOLID ? 1 : 0.68;
      const altitude = elevation * elevation * coverFactor;
      features.coveredAltitudeRisk += altitude;
      if (isEdge(column)) features.edgeCoveredAltitudeRisk += altitude;

      const leftAccess = cliffAccess(elevation, column - 1, heights);
      const rightAccess = cliffAccess(elevation, column + 1, heights);
      const access = unionReadiness(leftAccess, rightAccess) * altitude;
      features.coveredCliffAccess += access;
      if (isEdge(column)) features.edgeCliffAccess += access;
    }
  }

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const height = heights[column];
    const counts = [0, 0, 0];
    for (let row = BOARD_SIZE - height; row < BOARD_SIZE; row += 1) {
      const cell = board[indexOf(row, column)];
      if (cell >= 5 && cell <= 7 && cell > height) counts[cell - 5] += 1;
    }

    const coveredCliffDepth = adjacentCoveredCliffDepth(
      board,
      column,
      height,
      heights,
    );
    for (let offset = 0; offset < counts.length; offset += 1) {
      const count = counts[offset];
      if (count < 2) continue;
      const value = offset + 5;
      const pairs = (count * (count - 1)) / 2;
      const cohesion = pairs * readiness(value - height);
      features.highNumberColumnCohesion += cohesion;
      features.highNumberCliffCohesion +=
        cohesion * Math.min(3, coveredCliffDepth);
    }
  }

  return features;
}

/**
 * Action-only evidence which a settled board cannot retain, notably the
 * altitude and edge location of covers hit before gravity. Pass a move made
 * with `captureAnimation: true` to collect those damage terms.
 */
export function extractTunnelingActionFeatures(
  before: GameState,
  column: number,
  move: MoveResult,
): TunnelingActionFeatures {
  if (!Number.isInteger(column) || column < 0 || column >= BOARD_SIZE) {
    throw new Error("tunneling action column must be an integer from 0 to 6");
  }

  const heights = columnHeights(before.board);
  const height = heights[column];
  const disc = before.nextDisc;
  const sameHighDiscs = countColumnValue(before.board, column, disc);
  const canAdvanceVertically = disc >= 5 && height + 1 <= disc;
  const highNumberCommitment = canAdvanceVertically
    ? (sameHighDiscs + 1) * readiness(Math.max(1, disc - height - 1))
    : 0;
  const coveredCliffDepth = adjacentCoveredCliffDepth(
    before.board,
    column,
    height,
    heights,
  );

  const features: TunnelingActionFeatures = {
    highNumberCommitment,
    highNumberTrenchProgress:
      highNumberCommitment * Math.min(3, coveredCliffDepth),
    coveredCrackAltitude: 0,
    coveredRevealAltitude: 0,
    edgeCoverDamage: 0,
  };

  for (const frame of move.animation) {
    if (frame.kind !== "impact") continue;
    for (const index of frame.indexes) {
      const cell = frame.board[index];
      if (cell !== CRACKED && !isNumbered(cell)) continue;
      const row = Math.floor(index / BOARD_SIZE);
      const impactedColumn = index % BOARD_SIZE;
      const altitude = (BOARD_SIZE - row) ** 2;
      if (cell === CRACKED) features.coveredCrackAltitude += altitude;
      else features.coveredRevealAltitude += altitude;
      if (isEdge(impactedColumn)) features.edgeCoverDamage += altitude;
    }
  }

  return features;
}

export function scoreTunnelingFeatures(
  features: TunnelingFeatures,
  weights: TunnelingWeights = TUNNELING_WEIGHTS,
) {
  return weightedSum(features, weights);
}

export function scoreTunnelingActionFeatures(
  features: TunnelingActionFeatures,
  weights: TunnelingActionWeights = TUNNELING_ACTION_WEIGHTS,
) {
  return weightedSum(features, weights);
}

/** Combined horizon evaluator plus an independently scalable tunnel residual. */
export function evaluateTunnelingState(
  state: GameState,
  scale = DEFAULT_TUNNELING_STATE_SCALE,
) {
  validateScale(scale);
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;
  return (
    evaluateHeuristic(state, "combined") +
    scale * scoreTunnelingFeatures(extractTunnelingFeatures(state))
  );
}

/** A shaping bonus for comparing candidate transitions from the same state. */
export function evaluateTunnelingAction(
  before: GameState,
  column: number,
  move: MoveResult,
  scale = DEFAULT_TUNNELING_ACTION_SCALE,
) {
  validateScale(scale);
  return (
    scale *
    scoreTunnelingActionFeatures(
      extractTunnelingActionFeatures(before, column, move),
    )
  );
}

function adjacentCoveredCliffDepth(
  board: Board,
  column: number,
  height: number,
  heights: readonly number[],
) {
  let depth = 0;
  for (const neighbor of [column - 1, column + 1]) {
    if (neighbor < 0 || neighbor >= BOARD_SIZE) continue;
    const neighborHeight = heights[neighbor];
    for (
      let elevation = height + 1;
      elevation <= neighborHeight;
      elevation += 1
    ) {
      const cell = board[indexOf(BOARD_SIZE - elevation, neighbor)];
      if (cell === SOLID || cell === CRACKED) depth += 1;
    }
  }
  return depth;
}

function cliffAccess(
  elevation: number,
  neighboringColumn: number,
  heights: readonly number[],
) {
  if (
    neighboringColumn < 0 ||
    neighboringColumn >= BOARD_SIZE ||
    heights[neighboringColumn] >= elevation
  ) {
    return 0;
  }
  // A cover at elevation e is hit from the side when the e-th disc is placed
  // in the neighboring channel. One required placement is fully actionable;
  // each extra filler halves the credit.
  return readiness(elevation - heights[neighboringColumn]);
}

function columnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[indexOf(row, column)] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

function countColumnValue(board: Board, column: number, value: number) {
  let count = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    if (board[indexOf(row, column)] === value) count += 1;
  }
  return count;
}

function readiness(requiredPlacements: number) {
  return requiredPlacements >= 1 ? 2 ** (1 - requiredPlacements) : 0;
}

function unionReadiness(first: number, second: number) {
  return 1 - (1 - first) * (1 - second);
}

function weightedSum<T extends Record<keyof T, number>>(
  features: T,
  weights: Readonly<Record<keyof T, number>>,
) {
  let result = 0;
  for (const key of Object.keys(features) as (keyof T)[]) {
    result += features[key] * weights[key];
  }
  return result;
}

function validateScale(scale: number) {
  if (!Number.isFinite(scale) || scale < 0) {
    throw new Error("tunneling scale must be non-negative and finite");
  }
}

function isEdge(column: number) {
  return column === 0 || column === BOARD_SIZE - 1;
}

function indexOf(row: number, column: number) {
  return row * BOARD_SIZE + column;
}
