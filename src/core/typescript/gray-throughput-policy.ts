import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  SOLID,
  contiguousLineLength,
  isNumbered,
  playMove,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
  type MoveResult,
} from "./engine.ts";

/** Long-run queueing target: one disc/drop plus seven covers/five drops. */
export const REQUIRED_CLEAR_THROUGHPUT = 1 + BOARD_SIZE / 5;
export const REQUIRED_REVEAL_THROUGHPUT = BOARD_SIZE / 5;

export interface GrayStateFeatures {
  occupied: number;
  covers: number;
  solids: number;
  cracked: number;
  highCoverLoad: number;
  aboveBandLoad: number;
  peakExcess: number;
  meanExcess: number;
  risePressure: number;
  lowCaps: number;
  adjacentLowCaps: number;
  exposedCoverTopology: number;
  liveNumberTopology: number;
}

export interface GrayTransitionFeatures {
  clearSurplus: number;
  revealSurplus: number;
  crackedCovers: number;
  revealedCovers: number;
  clearedDiscs: number;
  chainDepth: number;
  score: number;
}

export type GrayStateWeights = Readonly<
  Record<keyof GrayStateFeatures, number>
>;
export type GrayTransitionWeights = Readonly<
  Record<keyof GrayTransitionFeatures, number>
>;

export interface GrayThroughputWeights {
  state: GrayStateWeights;
  transition: GrayTransitionWeights;
  continuationWeight: number;
}

export interface GrayThroughputOptions {
  /** Root reveal scenarios. */
  samples?: number;
  /** Reveal scenarios used to choose the observable next move. */
  continuationSamples?: number;
  /** One or two; depth two replans after the sampled next disc is visible. */
  depth?: 1 | 2;
  policySeed?: number;
  weights?: GrayThroughputWeights;
}

export interface GrayMoveEvaluation {
  column: number;
  mean: number;
}

export interface GrayThroughputResult {
  bestColumn: number | null;
  columns: readonly GrayMoveEvaluation[];
  samples: number;
  continuationSamples: number;
  depth: 1 | 2;
  work: number;
}

export const DEFAULT_GRAY_THROUGHPUT_WEIGHTS: GrayThroughputWeights = {
  state: {
    occupied: -380,
    covers: -260,
    solids: -120,
    cracked: 30,
    highCoverLoad: -360,
    aboveBandLoad: -720,
    peakExcess: -2_400,
    meanExcess: -750,
    risePressure: -900,
    lowCaps: -800,
    adjacentLowCaps: -1_150,
    exposedCoverTopology: 520,
    liveNumberTopology: 180,
  },
  transition: {
    clearSurplus: 650,
    revealSurplus: 520,
    crackedCovers: 380,
    revealedCovers: 620,
    clearedDiscs: 220,
    chainDepth: 180,
    score: 0.035,
  },
  continuationWeight: 0.72,
};

const DEFAULT_SAMPLES = 4;
const DEFAULT_CONTINUATION_SAMPLES = 2;
const DEFAULT_POLICY_SEED = 0x6772_6179;
const MAX_SAMPLES = 16;
const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const REVEAL_DOMAIN = 0x4752_4556;
const DISC_DOMAIN = 0x4744_4953;
const ROOT_DOMAIN = 0x4752_4f54;
const CONTINUATION_DOMAIN = 0x4743_4f4e;
const EVENT_MULTIPLIER = 0xc2b2_ae35;

/**
 * Seed-blind receding-horizon policy for steady gray-disc throughput.
 *
 * The action objective is a Lyapunov-style drift estimate: keep occupied
 * height close to a two-to-three-row operating band while revealing at least
 * 1.4 covers and clearing at least 2.4 discs per move over time. Score is a
 * small tie-breaker. At depth two, the second action may react to the sampled
 * board and newly visible disc, but never to any unobserved game seed.
 */
export function evaluateGrayThroughputMoves(
  state: GameState,
  options: GrayThroughputOptions = {},
): GrayThroughputResult {
  const samples = boundedSamples(options.samples ?? DEFAULT_SAMPLES, "samples");
  const continuationSamples = boundedSamples(
    options.continuationSamples ?? DEFAULT_CONTINUATION_SAMPLES,
    "continuationSamples",
  );
  const depth = options.depth ?? 2;
  if (depth !== 1 && depth !== 2) {
    throw new Error("gray throughput depth must be 1 or 2");
  }
  const policySeed = uint32(options.policySeed ?? DEFAULT_POLICY_SEED);
  const weights = options.weights ?? DEFAULT_GRAY_THROUGHPUT_WEIGHTS;
  validateWeights(weights);
  if (state.gameOver) {
    return {
      bestColumn: null,
      columns: [],
      samples,
      continuationSamples,
      depth,
      work: 0,
    };
  }

  const canonical = canonicalizeState(state);
  const observableHash = hashObservable(canonical.state, policySeed);
  const baseline = scoreGrayState(canonical.state, weights.state);
  const columns: GrayMoveEvaluation[] = [];
  let work = 0;

  for (const column of COLUMN_ORDER) {
    if (canonical.state.board[column] !== EMPTY) continue;
    let total = 0;
    for (let sample = 0; sample < samples; sample += 1) {
      const move = playMove(
        canonical.state,
        column,
        scenarioRandom(
          observableHash,
          sample,
          samples,
          ROOT_DOMAIN ^ REVEAL_DOMAIN,
        ),
        { captureAnimation: true },
      );
      work += 1;
      if (!move || move.state.gameOver) {
        total -= 10_000_000;
        continue;
      }
      const nextState = {
        ...move.state,
        score: 0,
        nextDisc: scenarioDisc(
          observableHash,
          sample,
          samples,
          DISC_DOMAIN,
        ),
      };
      const immediate = transitionAdvantage(
        canonical.state,
        move,
        nextState,
        baseline,
        weights,
      );
      if (depth === 1) {
        total += immediate;
        continue;
      }
      const continuation = bestContinuationAdvantage(
        nextState,
        continuationSamples,
        policySeed,
        weights,
      );
      work += continuation.work;
      total += immediate + weights.continuationWeight * continuation.value;
    }
    columns.push({
      column: canonical.reflected ? BOARD_SIZE - 1 - column : column,
      mean: total / samples,
    });
  }

  columns.sort((first, second) =>
    columnOrderIndex(state.board, first.column) -
    columnOrderIndex(state.board, second.column),
  );
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of columns) {
    if (column.mean > bestValue) {
      bestValue = column.mean;
      bestColumn = column.column;
    }
  }
  return {
    bestColumn,
    columns,
    samples,
    continuationSamples,
    depth,
    work,
  };
}

export function extractGrayStateFeatures(state: GameState): GrayStateFeatures {
  const heights = columnHeights(state.board);
  const peak = Math.max(...heights);
  const meanHeight = heights.reduce((sum, height) => sum + height, 0) / BOARD_SIZE;
  const features: GrayStateFeatures = {
    occupied: 0,
    covers: 0,
    solids: 0,
    cracked: 0,
    highCoverLoad: 0,
    aboveBandLoad: 0,
    peakExcess: Math.max(0, peak - 3) ** 3,
    meanExcess: Math.max(0, meanHeight - 2.5) ** 2,
    risePressure:
      Math.max(0, peak + 1 - 3) ** 2 / Math.max(1, state.movesRemaining),
    lowCaps: 0,
    adjacentLowCaps: 0,
    exposedCoverTopology: 0,
    liveNumberTopology: 0,
  };
  const lowCaps = Array<boolean>(BOARD_SIZE).fill(false);

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = row * BOARD_SIZE + column;
      const cell = state.board[index];
      if (cell === EMPTY) continue;
      features.occupied += 1;
      features.aboveBandLoad += Math.max(0, elevation - 3) ** 2;
      if (cell === SOLID || cell === CRACKED) {
        features.covers += 1;
        if (cell === SOLID) features.solids += 1;
        else features.cracked += 1;
        features.highCoverLoad +=
          Math.max(0, elevation - 2) ** 2 * (cell === SOLID ? 1 : 0.65);
        continue;
      }
      if (!isNumbered(cell)) continue;
      const rowLength = contiguousLineLength(
        state.board,
        row,
        column,
        "row",
      );
      const columnLength = heights[column];
      const distance = Math.min(
        Math.abs(cell - rowLength),
        Math.abs(cell - columnLength),
      );
      const readiness = 1 / (1 + distance);
      if (cell >= rowLength || cell >= columnLength) {
        features.liveNumberTopology += readiness;
      }
      let adjacentCovers = 0;
      for (const [rowDelta, columnDelta] of DIRECTIONS) {
        const neighborRow = row + rowDelta;
        const neighborColumn = column + columnDelta;
        if (!inside(neighborRow, neighborColumn)) continue;
        const neighbor = state.board[
          neighborRow * BOARD_SIZE + neighborColumn
        ];
        if (neighbor === SOLID || neighbor === CRACKED) adjacentCovers += 1;
      }
      features.exposedCoverTopology += readiness * adjacentCovers;
    }
  }

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (heights[column] === 0) continue;
    const cap = state.board[(BOARD_SIZE - heights[column]) * BOARD_SIZE + column];
    if (cap !== 1 && cap !== 2) continue;
    lowCaps[column] = true;
    features.lowCaps += heights[column] ** 2 * (cap === 1 ? 1.5 : 1);
    if (column > 0 && lowCaps[column - 1]) {
      features.adjacentLowCaps +=
        Math.min(heights[column - 1], heights[column]) ** 2;
    }
  }
  return features;
}

export function extractGrayTransitionFeatures(
  before: GameState,
  move: MoveResult,
): GrayTransitionFeatures {
  let clearedDiscs = 0;
  let revealedCovers = 0;
  for (const wave of move.waves) {
    clearedDiscs += wave.cleared;
    revealedCovers += wave.revealed;
  }
  let crackedCovers = 0;
  for (const frame of move.animation) {
    if (frame.kind !== "impact") continue;
    for (const index of frame.indexes) {
      if (frame.board[index] === CRACKED) crackedCovers += 1;
    }
  }
  // A level-up row is exogenous input, not a failure of this action's clear
  // rate. Throughput surplus is measured against the long-run arrival rate.
  const elapsedMoves = Math.max(1, move.state.movesPlayed - before.movesPlayed);
  return {
    clearSurplus: clearedDiscs - REQUIRED_CLEAR_THROUGHPUT * elapsedMoves,
    revealSurplus:
      revealedCovers - REQUIRED_REVEAL_THROUGHPUT * elapsedMoves,
    crackedCovers,
    revealedCovers,
    clearedDiscs,
    chainDepth: Math.max(0, move.waves.length - 1) ** 2,
    score: move.scoreDelta,
  };
}

export function scoreGrayState(
  state: GameState,
  weights: GrayStateWeights = DEFAULT_GRAY_THROUGHPUT_WEIGHTS.state,
) {
  return dotFeatures(extractGrayStateFeatures(state), weights);
}

export function scoreGrayTransition(
  before: GameState,
  move: MoveResult,
  weights: GrayTransitionWeights =
    DEFAULT_GRAY_THROUGHPUT_WEIGHTS.transition,
) {
  return dotFeatures(extractGrayTransitionFeatures(before, move), weights);
}

function transitionAdvantage(
  before: GameState,
  move: MoveResult,
  after: GameState,
  baseline: number,
  weights: GrayThroughputWeights,
) {
  return (
    scoreGrayTransition(before, move, weights.transition) +
    scoreGrayState(after, weights.state) -
    baseline
  );
}

function bestContinuationAdvantage(
  state: GameState,
  samples: number,
  policySeed: number,
  weights: GrayThroughputWeights,
) {
  const baseline = scoreGrayState(state, weights.state);
  const observableHash = hashObservable(state, policySeed ^ CONTINUATION_DOMAIN);
  let best = Number.NEGATIVE_INFINITY;
  let work = 0;
  for (const column of COLUMN_ORDER) {
    if (state.board[column] !== EMPTY) continue;
    let total = 0;
    for (let sample = 0; sample < samples; sample += 1) {
      const move = playMove(
        state,
        column,
        scenarioRandom(
          observableHash,
          sample,
          samples,
          CONTINUATION_DOMAIN ^ REVEAL_DOMAIN,
        ),
        { captureAnimation: true },
      );
      work += 1;
      if (!move || move.state.gameOver) {
        total -= 10_000_000;
        continue;
      }
      total += transitionAdvantage(
        state,
        move,
        { ...move.state, score: 0 },
        baseline,
        weights,
      );
    }
    best = Math.max(best, total / samples);
  }
  return { value: best, work };
}

function dotFeatures<T extends Record<keyof T, number>>(
  features: T,
  weights: Readonly<Record<keyof T, number>>,
) {
  let total = 0;
  for (const key of Object.keys(features) as (keyof T)[]) {
    total += features[key] * weights[key];
  }
  return total;
}

function validateWeights(weights: GrayThroughputWeights) {
  for (const value of [
    ...Object.values(weights.state),
    ...Object.values(weights.transition),
    weights.continuationWeight,
  ]) {
    if (!Number.isFinite(value)) {
      throw new Error("gray throughput weights must be finite");
    }
  }
  if (weights.continuationWeight < 0 || weights.continuationWeight > 1) {
    throw new Error("continuationWeight must be from 0 to 1");
  }
}

function canonicalizeState(state: GameState) {
  const reflected = compareBoardWithMirror(state.board) > 0;
  return {
    reflected,
    state: reflected
      ? { ...state, board: mirrorBoard(state.board), score: 0 }
      : state.score === 0
        ? state
        : { ...state, score: 0 },
  };
}

function hashObservable(state: GameState, seed: number) {
  let hash = seed >>> 0;
  for (const cell of state.board) {
    hash = Math.imul(hash ^ (cell + 1), 0x0100_0193) >>> 0;
  }
  for (const value of [
    state.nextDisc,
    state.level,
    state.movesRemaining,
    state.movesPlayed,
  ]) {
    hash = Math.imul(hash ^ value, 0x0100_0193) >>> 0;
  }
  return mix32(hash);
}

function scenarioDisc(
  hash: number,
  sample: number,
  samples: number,
  domain: number,
): DiscValue {
  return (Math.floor(stratified(hash, sample, samples, domain) * BOARD_SIZE) +
    1) as DiscValue;
}

function scenarioRandom(
  hash: number,
  sample: number,
  samples: number,
  domain: number,
) {
  let event = 0;
  return () => {
    const value = stratified(
      hash,
      sample,
      samples,
      domain ^ Math.imul(event + 1, EVENT_MULTIPLIER),
    );
    event += 1;
    return value;
  };
}

function stratified(
  hash: number,
  sample: number,
  samples: number,
  domain: number,
) {
  const rotation = mix32(hash ^ domain) % samples;
  const stratum = (sample + rotation) % samples;
  const jitter = mix32(hash ^ domain ^ Math.imul(sample + 1, 0x9e37_79b9));
  return (stratum + jitter / 4_294_967_296) / samples;
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

function compareBoardWithMirror(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[row * BOARD_SIZE + column];
      const reflected = board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
      if (forward < reflected) return -1;
      if (forward > reflected) return 1;
    }
  }
  return 0;
}

function mirrorBoard(board: Board): Board {
  const result: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      result.push(board[row * BOARD_SIZE + column]);
    }
  }
  return result;
}

function columnOrderIndex(board: Board, column: number) {
  const order = compareBoardWithMirror(board) <= 0
    ? COLUMN_ORDER
    : [...COLUMN_ORDER].map((value) => BOARD_SIZE - 1 - value);
  return order.indexOf(column as (typeof COLUMN_ORDER)[number]);
}

function boundedSamples(value: number, name: string) {
  if (!Number.isSafeInteger(value) || value < 1 || value > MAX_SAMPLES) {
    throw new Error(`${name} must be an integer from 1 to ${MAX_SAMPLES}`);
  }
  return value;
}

function uint32(value: number) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error("policySeed must be a uint32");
  }
  return value >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function inside(row: number, column: number) {
  return (
    row >= 0 &&
    row < BOARD_SIZE &&
    column >= 0 &&
    column < BOARD_SIZE
  );
}

const DIRECTIONS = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
] as const;
