import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  SOLID,
  isNumbered,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";

export interface RecursivePotentialFeatures {
  physicalSeedEnergy: number;
  physicallySeededDiscs: number;
  firstPropagationEnergy: number;
  propagatedDiscEnergy: number;
  deepChainEnergy: number;
  crackedExposure: number;
  solidCrackPotential: number;
  solidRevealPotential: number;
  deepCoverExposure: number;
  propagationWaves: number;
  maxActivation: number;
}

export interface RecursivePotentialAnalysis {
  features: RecursivePotentialFeatures;
  /** Readiness by mirror-canonical board index; non-numbered cells stay zero. */
  activation: readonly number[];
  /** Physical-addition readiness by canonical index before propagation. */
  physicalSeeds: readonly number[];
}

export interface RecursivePotentialWeights {
  deepChainEnergy: number;
  deepCoverExposure: number;
}

export const MAX_RECURSIVE_PROPAGATION_WAVES = 8;
export const RECURSIVE_POTENTIAL_WEIGHTS: RecursivePotentialWeights = {
  deepChainEnergy: 480,
  deepCoverExposure: 160,
};

const PROPAGATION_DISCOUNT = 0.78;
const CONVERGENCE_EPSILON = 1e-8;

interface LineAnalysis {
  lengths: number[];
  starts: number[];
  ends: number[];
}

interface DiscNode {
  index: number;
  row: number;
  column: number;
  value: number;
  seed: number;
  horizontalExcess: number;
  verticalExcess: number;
  horizontalSupporters: number[];
  verticalSupporters: number[];
}

interface CoverEnergy {
  crackedExposure: number;
  solidCrackPotential: number;
  solidRevealPotential: number;
  weighted: number;
}

/**
 * Propagate physically seeded activation through overloaded disc components.
 *
 * A numbered disc is initially live only when a legal sequence of additions
 * can grow one of its current lines to the disc value. An overloaded disc can
 * then inherit readiness from numbered supporters that may disappear first.
 * Iteration begins at those physical seeds, so a closed dependency cycle of
 * ones or twos remains exactly dormant instead of creating energy from itself.
 * Eight monotone waves bound both feedback and runtime on the 7x7 board.
 */
export function analyzeRecursivePotential(
  state: GameState,
): RecursivePotentialAnalysis {
  const board = canonicalBoard(state.board);
  const horizontal = analyzeLines(board, "horizontal");
  const vertical = analyzeLines(board, "vertical");
  const columnHeights = getColumnHeights(board);
  const nodes: DiscNode[] = [];
  const nodeByIndex = new Map<number, DiscNode>();

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = row * BOARD_SIZE + column;
      const value = board[index];
      if (!isNumbered(value)) continue;

      const horizontalLength = horizontal.lengths[index];
      const verticalLength = vertical.lengths[index];
      const verticalAddition =
        value > columnHeights[column]
          ? readiness(value - columnHeights[column])
          : 0;
      const horizontalCost = minimumHorizontalAdditionCost(
        row,
        value,
        horizontal.starts[index],
        horizontal.ends[index],
        horizontalLength,
        columnHeights,
      );
      const horizontalAddition =
        horizontalCost === null ? 0 : readiness(horizontalCost);
      const alreadyActive =
        value === horizontalLength || value === verticalLength ? 1 : 0;
      const node: DiscNode = {
        index,
        row,
        column,
        value,
        seed: Math.max(
          alreadyActive,
          unionReadiness(horizontalAddition, verticalAddition),
        ),
        horizontalExcess: Math.max(0, horizontalLength - value),
        verticalExcess: Math.max(0, verticalLength - value),
        horizontalSupporters: [],
        verticalSupporters: [],
      };
      nodes.push(node);
      nodeByIndex.set(index, node);
    }
  }

  for (const node of nodes) {
    node.horizontalSupporters = lineSupporters(
      node,
      horizontal,
      nodeByIndex,
      "horizontal",
    );
    node.verticalSupporters = lineSupporters(
      node,
      vertical,
      nodeByIndex,
      "vertical",
    );
  }

  const physicalSeeds = Array<number>(board.length).fill(0);
  let activation = Array<number>(board.length).fill(0);
  for (const node of nodes) {
    physicalSeeds[node.index] = node.seed;
    activation[node.index] = node.seed;
  }

  let firstActivation = activation.slice();
  let firstPropagationEnergy = 0;
  let deepChainEnergy = 0;
  let propagationWaves = 0;

  for (
    let wave = 1;
    wave <= MAX_RECURSIVE_PROPAGATION_WAVES;
    wave += 1
  ) {
    const next = activation.slice();
    let waveEnergy = 0;
    for (const node of nodes) {
      const horizontalRelease = releaseReadiness(
        node.horizontalExcess,
        node.horizontalSupporters,
        activation,
      );
      const verticalRelease = releaseReadiness(
        node.verticalExcess,
        node.verticalSupporters,
        activation,
      );
      const propagated =
        unionReadiness(horizontalRelease, verticalRelease) *
        PROPAGATION_DISCOUNT;
      const candidate = Math.max(node.seed, propagated);
      if (candidate <= activation[node.index]) continue;
      const delta = candidate - activation[node.index];
      next[node.index] = candidate;
      waveEnergy += delta;
    }

    if (waveEnergy <= CONVERGENCE_EPSILON) break;
    activation = next;
    propagationWaves = wave;
    if (wave === 1) {
      firstPropagationEnergy = waveEnergy;
      firstActivation = activation.slice();
    } else {
      // Later activations represent successively deeper chain waves. Reward
      // that depth, but much less aggressively than Drop7's realized score.
      deepChainEnergy += waveEnergy * (1 + wave * 0.5);
    }
  }

  const firstCover = coverEnergy(board, firstActivation);
  const finalCover = coverEnergy(board, activation);
  let physicalSeedEnergy = 0;
  let physicallySeededDiscs = 0;
  let propagatedDiscEnergy = 0;
  let maxActivation = 0;
  for (const node of nodes) {
    const seed = physicalSeeds[node.index];
    const final = activation[node.index];
    physicalSeedEnergy += seed;
    if (seed > 0) physicallySeededDiscs += 1;
    propagatedDiscEnergy += final - seed;
    maxActivation = Math.max(maxActivation, final);
  }

  return {
    features: {
      physicalSeedEnergy,
      physicallySeededDiscs,
      firstPropagationEnergy,
      propagatedDiscEnergy,
      deepChainEnergy,
      crackedExposure: finalCover.crackedExposure,
      solidCrackPotential: finalCover.solidCrackPotential,
      solidRevealPotential: finalCover.solidRevealPotential,
      deepCoverExposure: Math.max(
        0,
        finalCover.weighted - firstCover.weighted,
      ),
      propagationWaves,
      maxActivation,
    },
    activation,
    physicalSeeds,
  };
}

export function extractRecursivePotentialFeatures(state: GameState) {
  return analyzeRecursivePotential(state).features;
}

/** Combined heuristic plus only the multi-wave residual not already scored. */
export function evaluateRecursivePotential(
  state: GameState,
  scale = 1,
) {
  if (!Number.isFinite(scale) || scale < 0) {
    throw new Error("recursive potential scale must be non-negative and finite");
  }
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;

  const board = canonicalBoard(state.board);
  const canonicalState = board === state.board ? state : { ...state, board };
  const features = extractRecursivePotentialFeatures(canonicalState);
  const recursiveBonus =
    features.deepChainEnergy *
      RECURSIVE_POTENTIAL_WEIGHTS.deepChainEnergy +
    features.deepCoverExposure *
      RECURSIVE_POTENTIAL_WEIGHTS.deepCoverExposure;
  return evaluateHeuristic(canonicalState, "combined") + scale * recursiveBonus;
}

function releaseReadiness(
  excess: number,
  supporterIndexes: readonly number[],
  activation: readonly number[],
) {
  if (excess <= 0 || supporterIndexes.length < excess) return 0;
  return probabilityAtLeast(
    supporterIndexes.map((index) => activation[index]),
    excess,
  );
}

function probabilityAtLeast(
  probabilities: readonly number[],
  threshold: number,
) {
  if (threshold <= 0) return 1;
  if (probabilities.length < threshold) return 0;
  const distribution = Array<number>(probabilities.length + 1).fill(0);
  distribution[0] = 1;
  let processed = 0;
  for (const probability of probabilities) {
    for (let count = processed; count >= 0; count -= 1) {
      const mass = distribution[count];
      distribution[count] = mass * (1 - probability);
      distribution[count + 1] += mass * probability;
    }
    processed += 1;
  }
  let result = 0;
  for (let count = threshold; count <= probabilities.length; count += 1) {
    result += distribution[count];
  }
  return Math.max(0, Math.min(1, result));
}

function coverEnergy(
  board: Board,
  activation: readonly number[],
): CoverEnergy {
  const result: CoverEnergy = {
    crackedExposure: 0,
    solidCrackPotential: 0,
    solidRevealPotential: 0,
    weighted: 0,
  };
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = row * BOARD_SIZE + column;
      const cell = board[index];
      if (cell !== CRACKED && cell !== SOLID) continue;
      const neighbors: number[] = [];
      for (const [rowDelta, columnDelta] of ORTHOGONAL_DIRECTIONS) {
        const neighborRow = row + rowDelta;
        const neighborColumn = column + columnDelta;
        if (!isInside(neighborRow, neighborColumn)) continue;
        const readiness =
          activation[neighborRow * BOARD_SIZE + neighborColumn];
        if (readiness > 0) neighbors.push(readiness);
      }
      const oneHit = unionAll(neighbors);
      if (cell === CRACKED) {
        result.crackedExposure += oneHit;
        result.weighted += oneHit;
      } else {
        const twoHits = probabilityAtLeast(neighbors, 2);
        result.solidCrackPotential += oneHit;
        result.solidRevealPotential += twoHits;
        result.weighted += oneHit * 0.3 + twoHits * 0.7;
      }
    }
  }
  return result;
}

function lineSupporters(
  target: DiscNode,
  lines: LineAnalysis,
  nodeByIndex: ReadonlyMap<number, DiscNode>,
  axis: "horizontal" | "vertical",
) {
  const supporters: number[] = [];
  const start = lines.starts[target.index];
  const end = lines.ends[target.index];
  for (let position = start; position <= end; position += 1) {
    const index =
      axis === "horizontal"
        ? target.row * BOARD_SIZE + position
        : position * BOARD_SIZE + target.column;
    if (index !== target.index && nodeByIndex.has(index)) {
      supporters.push(index);
    }
  }
  return supporters;
}

function analyzeLines(
  board: Board,
  axis: "horizontal" | "vertical",
): LineAnalysis {
  const lengths = Array<number>(board.length).fill(0);
  const starts = Array<number>(board.length).fill(-1);
  const ends = Array<number>(board.length).fill(-1);
  for (let fixed = 0; fixed < BOARD_SIZE; fixed += 1) {
    let cursor = 0;
    while (cursor < BOARD_SIZE) {
      const cursorIndex =
        axis === "horizontal"
          ? fixed * BOARD_SIZE + cursor
          : cursor * BOARD_SIZE + fixed;
      if (board[cursorIndex] === EMPTY) {
        cursor += 1;
        continue;
      }
      const start = cursor;
      while (cursor < BOARD_SIZE) {
        const index =
          axis === "horizontal"
            ? fixed * BOARD_SIZE + cursor
            : cursor * BOARD_SIZE + fixed;
        if (board[index] === EMPTY) break;
        cursor += 1;
      }
      const end = cursor - 1;
      const length = end - start + 1;
      for (let position = start; position <= end; position += 1) {
        const index =
          axis === "horizontal"
            ? fixed * BOARD_SIZE + position
            : position * BOARD_SIZE + fixed;
        lengths[index] = length;
        starts[index] = start;
        ends[index] = end;
      }
    }
  }
  return { lengths, starts, ends };
}

function getColumnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

function minimumHorizontalAdditionCost(
  row: number,
  value: number,
  segmentStart: number,
  segmentEnd: number,
  segmentLength: number,
  columnHeights: readonly number[],
) {
  if (segmentStart < 0 || value <= segmentLength) return null;
  const elevation = BOARD_SIZE - row;
  let best = Number.POSITIVE_INFINITY;
  for (let start = 0; start + value <= BOARD_SIZE; start += 1) {
    const end = start + value - 1;
    if (start > segmentStart || end < segmentEnd) continue;
    if (start > 0 && columnHeights[start - 1] >= elevation) continue;
    if (end + 1 < BOARD_SIZE && columnHeights[end + 1] >= elevation) continue;
    let cost = 0;
    for (let column = start; column <= end; column += 1) {
      cost += Math.max(0, elevation - columnHeights[column]);
    }
    if (cost > 0 && cost < best) best = cost;
  }
  return Number.isFinite(best) ? best : null;
}

function readiness(cost: number) {
  return cost >= 1 ? 2 ** (1 - cost) : 0;
}

function unionReadiness(first: number, second: number) {
  return 1 - (1 - first) * (1 - second);
}

function unionAll(values: readonly number[]) {
  let inverse = 1;
  for (const value of values) inverse *= 1 - value;
  return 1 - inverse;
}

function canonicalBoard(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const reflected = board[offset + BOARD_SIZE - 1 - column];
      if (forward < reflected) return board;
      if (forward > reflected) return mirrorBoard(board);
    }
  }
  return board;
}

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored.push(board[offset + column]);
    }
  }
  return mirrored;
}

const ORTHOGONAL_DIRECTIONS = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
] as const;

function isInside(row: number, column: number) {
  return (
    row >= 0 &&
    row < BOARD_SIZE &&
    column >= 0 &&
    column < BOARD_SIZE
  );
}
