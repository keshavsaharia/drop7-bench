import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  isNumbered,
  serializeBoard,
  type Board,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_PROFILES,
  HEURISTIC_GAME_OVER_UTILITY,
  extractHeuristicFeatures,
  scoreHeuristicFeatures,
  type HeuristicFeatures,
} from "./heuristic.ts";

/** Five drops arrive between rises; each rise adds seven covered discs. */
export const REQUIRED_CLEAR_RATE = 1 + BOARD_SIZE / MOVES_PER_LEVEL;
export const REQUIRED_REVEAL_RATE = BOARD_SIZE / MOVES_PER_LEVEL;

const OPERATING_OCCUPANCY = BOARD_SIZE * 2;
const OPERATING_HEIGHT = 3;

export interface PhaseHorizonFeatures {
  /** Occupancy projected to the next rise at the sustainable clear rate. */
  projectedOccupancyDebt: number;
  /** Covers which would remain when the next seven-cover row arrives. */
  residualCoverDebt: number;
  coverAltitudeDebt: number;
  imminentCoverAltitudeDebt: number;
  peakHeightRisk: number;
  lowCapLoad: number;
  adjacentLowCapLoad: number;
  directBuildInventory: number;
  quietBuildOptions: number;
  quietDirectGain: number;
  triggerReadiness: number;
  releaseReadiness: number;
}

export type PhaseHorizonWeights = Readonly<
  Record<keyof PhaseHorizonFeatures, number>
> & {
  readonly baselineScale: number;
};

export const DEFAULT_PHASE_HORIZON_WEIGHTS: PhaseHorizonWeights = {
  baselineScale: 1,
  projectedOccupancyDebt: -120,
  residualCoverDebt: -100,
  coverAltitudeDebt: -25,
  imminentCoverAltitudeDebt: -35,
  peakHeightRisk: -900,
  lowCapLoad: -120,
  adjacentLowCapLoad: -180,
  directBuildInventory: 220,
  quietBuildOptions: 300,
  quietDirectGain: 600,
  triggerReadiness: 300,
  releaseReadiness: 220,
};

export interface PhaseHorizonEvaluatorOptions {
  weights?: PhaseHorizonWeights;
  /** A small external leaf cache compensates for uncached depth-zero leaves. */
  maxCacheEntries?: number;
}

interface PlacementInventory {
  quietOptions: number;
  bestQuietDirectGain: number;
  triggerOptions: number;
  triggerStrength: number;
}

interface PhaseHorizonAnalysis {
  features: PhaseHorizonFeatures;
  heuristic: HeuristicFeatures;
}

/**
 * Extract phase-aware queueing, build, and release features from a stable board.
 *
 * `projectedOccupancyDebt` is not raw occupancy. It assumes the policy clears
 * the sustainable 2.4 discs/drop until the next rise: one newly dropped disc
 * plus seven covers every five drops. A board one move from a rise therefore
 * carries substantially more debt than the same board five moves away.
 *
 * Quiet build gain is deliberately observable and cheap. For each legal drop
 * which does not immediately trigger a numbered disc, it measures the added
 * one-step readiness of only the row and column affected by that drop. This
 * preserves the useful "store energy without firing" distinction without
 * simulating another chance cascade inside every expectimax leaf.
 */
export function extractPhaseHorizonFeatures(
  state: GameState,
): PhaseHorizonFeatures {
  return analyzePhaseHorizon(state).features;
}

function analyzePhaseHorizon(state: GameState): PhaseHorizonAnalysis {
  const board = state.board;
  const heights = columnHeights(board);
  const peakHeight = Math.max(...heights);
  const movesUntilRise = boundedMovesUntilRise(state.movesRemaining);
  const riseUrgency =
    (MOVES_PER_LEVEL - movesUntilRise) / (MOVES_PER_LEVEL - 1);
  let occupied = 0;
  let covers = 0;
  let coverAltitudeDebt = 0;

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = board[row * BOARD_SIZE + column];
      if (cell === EMPTY) continue;
      occupied += 1;
      if (cell !== SOLID && cell !== CRACKED) continue;
      covers += 1;
      const coverFactor = cell === SOLID ? 1 : 0.65;
      const edgeFactor = column === 0 || column === BOARD_SIZE - 1 ? 1.3 : 1;
      coverAltitudeDebt += elevation ** 2 * coverFactor * edgeFactor;
    }
  }

  const projectedOccupancy =
    occupied +
    BOARD_SIZE -
    (REQUIRED_CLEAR_RATE - 1) * movesUntilRise;
  const projectedOccupancyDebt = Math.max(
    0,
    projectedOccupancy - OPERATING_OCCUPANCY,
  ) ** 2;
  const residualCovers = Math.max(
    0,
    covers - REQUIRED_REVEAL_RATE * movesUntilRise,
  );
  const residualCoverDebt = residualCovers ** 2;
  const peakHeightRisk = Math.max(
    0,
    peakHeight + riseUrgency - OPERATING_HEIGHT,
  ) ** 3;
  const { lowCapLoad, adjacentLowCapLoad } = lowCapDebt(board, heights);
  const heuristic = extractHeuristicFeatures(state);
  const placements = placementInventory(state, heights);

  return {
    heuristic,
    features: {
      projectedOccupancyDebt,
      residualCoverDebt,
      coverAltitudeDebt,
      imminentCoverAltitudeDebt: coverAltitudeDebt * riseUrgency,
      peakHeightRisk,
      lowCapLoad,
      adjacentLowCapLoad,
      directBuildInventory: heuristic.directPotential,
      quietBuildOptions: placements.quietOptions,
      quietDirectGain: placements.bestQuietDirectGain,
      triggerReadiness:
        placements.triggerOptions * 0.5 + placements.triggerStrength,
      releaseReadiness:
        heuristic.latentChainPotential +
        heuristic.crackedExposure +
        heuristic.solidExposure * 0.35,
    },
  };
}

export function scorePhaseHorizonFeatures(
  features: PhaseHorizonFeatures,
  weights: PhaseHorizonWeights = DEFAULT_PHASE_HORIZON_WEIGHTS,
) {
  validateWeights(weights);
  let value = 0;
  for (const key of Object.keys(features) as (keyof PhaseHorizonFeatures)[]) {
    value += features[key] * weights[key];
  }
  return value;
}

/** Combined evaluator plus a phase-aware five-move throughput residual. */
export function evaluatePhaseHorizon(
  state: GameState,
  weights: PhaseHorizonWeights = DEFAULT_PHASE_HORIZON_WEIGHTS,
) {
  validateWeights(weights);
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;
  const analysis = analyzePhaseHorizon(state);
  return (
    scoreHeuristicFeatures(analysis.heuristic, HEURISTIC_PROFILES.combined) *
      weights.baselineScale +
    scorePhaseHorizonFeatures(analysis.features, weights)
  );
}

export function createPhaseHorizonEvaluator(
  options: PhaseHorizonEvaluatorOptions = {},
) {
  const weights = options.weights ?? DEFAULT_PHASE_HORIZON_WEIGHTS;
  validateWeights(weights);
  const maximum = options.maxCacheEntries ?? 40_000;
  if (!Number.isSafeInteger(maximum) || maximum < 1 || maximum > 200_000) {
    throw new Error("phase horizon maxCacheEntries must be from 1 to 200000");
  }
  const cache = new Map<string, number>();
  return (state: GameState) => {
    if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;
    const key = `${serializeBoard(state.board)}:${state.nextDisc}:${state.movesRemaining}`;
    const cached = cache.get(key);
    if (cached !== undefined) {
      cache.delete(key);
      cache.set(key, cached);
      return cached;
    }
    const value = evaluatePhaseHorizon(state, weights);
    while (cache.size >= maximum) {
      const oldest = cache.keys().next().value;
      if (oldest === undefined) break;
      cache.delete(oldest);
    }
    cache.set(key, value);
    return value;
  };
}

function lowCapDebt(board: Board, heights: readonly number[]) {
  let lowCapLoad = 0;
  let adjacentLowCapLoad = 0;
  const lowCaps = Array<boolean>(BOARD_SIZE).fill(false);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const height = heights[column];
    if (height === 0) continue;
    const cap = board[(BOARD_SIZE - height) * BOARD_SIZE + column];
    if (cap !== 1 && cap !== 2) continue;
    lowCaps[column] = true;
    lowCapLoad += height ** 2 * (cap === 1 ? 1.5 : 1);
    if (column > 0 && lowCaps[column - 1]) {
      adjacentLowCapLoad += Math.min(heights[column - 1], height) ** 2;
    }
  }
  return { lowCapLoad, adjacentLowCapLoad };
}

function placementInventory(
  state: GameState,
  heights: readonly number[],
): PlacementInventory {
  const result: PlacementInventory = {
    quietOptions: 0,
    bestQuietDirectGain: 0,
    triggerOptions: 0,
    triggerStrength: 0,
  };
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const oldVerticalLength = heights[column];
    if (oldVerticalLength >= BOARD_SIZE) continue;
    const newVerticalLength = oldVerticalLength + 1;
    const landingRow = BOARD_SIZE - newVerticalLength;
    let leftRun = 0;
    for (let target = column - 1; target >= 0; target -= 1) {
      if (state.board[landingRow * BOARD_SIZE + target] === EMPTY) break;
      leftRun += 1;
    }
    let rightRun = 0;
    for (let target = column + 1; target < BOARD_SIZE; target += 1) {
      if (state.board[landingRow * BOARD_SIZE + target] === EMPTY) break;
      rightRun += 1;
    }
    const newHorizontalLength = leftRun + 1 + rightRun;
    const triggerIndexes = new Set<number>();
    let directGain = 0;

    const placed = {
      triggers:
        state.nextDisc === newHorizontalLength ||
        state.nextDisc === newVerticalLength
          ? 1
          : 0,
      gain: unionReadiness(
        additionReadiness(state.nextDisc, newHorizontalLength),
        additionReadiness(state.nextDisc, newVerticalLength),
      ),
    };
    if (placed.triggers > 0) {
      triggerIndexes.add(landingRow * BOARD_SIZE + column);
    }
    directGain += placed.gain;

    // The new disc joins at most the two row segments immediately beside it.
    // Their old lengths are known from the gap; every affected cell shares the
    // merged length after placement.
    for (
      let targetColumn = column - leftRun;
      targetColumn <= column + rightRun;
      targetColumn += 1
    ) {
      if (targetColumn === column) continue;
      const value = state.board[landingRow * BOARD_SIZE + targetColumn];
      if (!isNumbered(value)) continue;
      const oldHorizontalLength =
        targetColumn < column ? leftRun : rightRun;
      const change = readinessChange(
        value,
        oldHorizontalLength,
        heights[targetColumn],
        newHorizontalLength,
        heights[targetColumn],
      );
      if (change.triggers > 0) {
        triggerIndexes.add(landingRow * BOARD_SIZE + targetColumn);
      }
      directGain += change.gain;
    }

    // Every numbered disc already in this column gets one unit closer to its
    // vertical target. Its horizontal line is unchanged, so the cheap proxy
    // counts only the new vertical readiness. This can overestimate gain when
    // horizontal readiness already exists, but it stays mirror-safe and avoids
    // embedding a second full line analysis in every expectimax leaf.
    for (let row = BOARD_SIZE - oldVerticalLength; row < BOARD_SIZE; row += 1) {
      const value = state.board[row * BOARD_SIZE + column];
      if (!isNumbered(value)) continue;
      if (value === newVerticalLength) {
        triggerIndexes.add(row * BOARD_SIZE + column);
      }
      directGain += Math.max(
        0,
        additionReadiness(value, newVerticalLength) -
          additionReadiness(value, oldVerticalLength),
      );
    }
    const triggers = triggerIndexes.size;
    if (triggers > 0) {
      result.triggerOptions += 1;
      result.triggerStrength += triggers;
    } else {
      result.quietOptions += 1;
      result.bestQuietDirectGain = Math.max(
        result.bestQuietDirectGain,
        directGain,
      );
    }
  }
  return result;
}

function readinessChange(
  value: number,
  oldHorizontal: number,
  oldVertical: number,
  newHorizontal: number,
  newVertical: number,
) {
  const oldReadiness = unionReadiness(
    additionReadiness(value, oldHorizontal),
    additionReadiness(value, oldVertical),
  );
  const newReadiness = unionReadiness(
    additionReadiness(value, newHorizontal),
    additionReadiness(value, newVertical),
  );
  return {
    triggers: value === newHorizontal || value === newVertical ? 1 : 0,
    gain: Math.max(0, newReadiness - oldReadiness),
  };
}

function additionReadiness(value: number, lineLength: number) {
  const cost = value - lineLength;
  return cost >= 1 ? 2 ** (1 - cost) : 0;
}

function unionReadiness(first: number, second: number) {
  return 1 - (1 - first) * (1 - second);
}

function columnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

function boundedMovesUntilRise(value: number) {
  if (!Number.isSafeInteger(value)) return MOVES_PER_LEVEL;
  return Math.max(1, Math.min(MOVES_PER_LEVEL, value));
}

function validateWeights(weights: PhaseHorizonWeights) {
  for (const [key, value] of Object.entries(weights)) {
    if (!Number.isFinite(value)) {
      throw new Error(`phase horizon weight ${key} must be finite`);
    }
  }
}
