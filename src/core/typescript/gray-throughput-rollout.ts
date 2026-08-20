import {
  BOARD_SIZE,
  EMPTY,
  playMove,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS,
  evaluateGrayThroughputMoves,
  scoreGrayState,
  scoreGrayTransition,
  type GrayThroughputWeights,
} from "./gray-throughput-policy.ts";

export interface GrayRolloutOptions {
  scenarios?: number;
  horizon?: number;
  guideSamples?: number;
  policySeed?: number;
  riskAversion?: number;
  weights?: GrayThroughputWeights;
}

export interface GrayRolloutMoveEvaluation {
  column: number;
  mean: number;
  standardDeviation: number;
  utility: number;
}

export interface GrayRolloutResult {
  bestColumn: number | null;
  columns: readonly GrayRolloutMoveEvaluation[];
  scenarios: number;
  horizon: number;
  work: number;
}

const DEFAULT_SCENARIOS = 4;
const DEFAULT_HORIZON = 20;
const DEFAULT_GUIDE_SAMPLES = 2;
const DEFAULT_POLICY_SEED = 0x6772_6f6c;
const MAX_SCENARIOS = 16;
const MAX_HORIZON = 40;
const MAX_GUIDE_SAMPLES = 8;
const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const ROOT_REVEAL_DOMAIN = 0x524f_4f54;
const FUTURE_REVEAL_DOMAIN = 0x4652_4556;
const FUTURE_DISC_DOMAIN = 0x4644_4953;
const EVENT_MULTIPLIER = 0xc2b2_ae35;
const SCENARIO_MULTIPLIER = 0x9e37_79b9;
const PLY_MULTIPLIER = 0x85eb_ca6b;
const TERMINAL_UTILITY = -20_000_000;

/**
 * Receding-horizon Monte Carlo policy whose continuation controller is the
 * seed-blind gray-throughput policy. A scenario is only a possible future:
 * it is derived from observable state and a fixed solver seed, is shared by
 * every root action, and is never related to the headless game's hidden seed.
 *
 * Unlike an open-loop column sequence, each simulated continuation observes
 * its sampled next disc before choosing the next move. This avoids strategy
 * fusion while still approximating the value of replanning in the real game.
 */
export function evaluateGrayRolloutMoves(
  state: GameState,
  options: GrayRolloutOptions = {},
): GrayRolloutResult {
  const scenarios = boundedInteger(
    options.scenarios ?? DEFAULT_SCENARIOS,
    1,
    MAX_SCENARIOS,
    "scenarios",
  );
  const horizon = boundedInteger(
    options.horizon ?? DEFAULT_HORIZON,
    1,
    MAX_HORIZON,
    "horizon",
  );
  const guideSamples = boundedInteger(
    options.guideSamples ?? DEFAULT_GUIDE_SAMPLES,
    1,
    MAX_GUIDE_SAMPLES,
    "guideSamples",
  );
  const policySeed = uint32(options.policySeed ?? DEFAULT_POLICY_SEED);
  const riskAversion = options.riskAversion ?? 0.35;
  if (!Number.isFinite(riskAversion) || riskAversion < 0) {
    throw new Error("riskAversion must be a non-negative finite number");
  }
  const weights = options.weights ?? DEFAULT_GRAY_THROUGHPUT_WEIGHTS;
  if (state.gameOver) {
    return {
      bestColumn: null,
      columns: [],
      scenarios,
      horizon,
      work: 0,
    };
  }

  const canonical = canonicalizeState(state);
  const rootHash = hashObservable(canonical.state, policySeed);
  const columns: GrayRolloutMoveEvaluation[] = [];
  let work = 0;

  for (const column of COLUMN_ORDER) {
    if (canonical.state.board[column] !== EMPTY) continue;
    const utilities: number[] = [];
    for (let scenario = 0; scenario < scenarios; scenario += 1) {
      const rollout = rolloutRoot(
        canonical.state,
        column,
        scenario,
        scenarios,
        horizon,
        guideSamples,
        policySeed,
        rootHash,
        weights,
      );
      utilities.push(rollout.utility);
      work += rollout.work;
    }
    const average = mean(utilities);
    const deviation = standardDeviation(utilities, average);
    columns.push({
      column: canonical.reflected ? BOARD_SIZE - 1 - column : column,
      mean: average,
      standardDeviation: deviation,
      utility: average - riskAversion * deviation,
    });
  }

  columns.sort(
    (first, second) =>
      columnOrderIndex(state.board, first.column) -
      columnOrderIndex(state.board, second.column),
  );
  let bestColumn: number | null = null;
  let bestUtility = Number.NEGATIVE_INFINITY;
  for (const column of columns) {
    if (column.utility > bestUtility) {
      bestUtility = column.utility;
      bestColumn = column.column;
    }
  }
  return { bestColumn, columns, scenarios, horizon, work };
}

function rolloutRoot(
  state: GameState,
  rootColumn: number,
  scenario: number,
  scenarios: number,
  horizon: number,
  guideSamples: number,
  policySeed: number,
  rootHash: number,
  weights: GrayThroughputWeights,
) {
  let current = state;
  let column = rootColumn;
  let transitionUtility = 0;
  let work = 0;

  for (let ply = 0; ply < horizon; ply += 1) {
    const before = current;
    const move = playMove(
      current,
      column,
      scenarioRandom(rootHash, scenario, scenarios, ply),
      { captureAnimation: true },
    );
    work += 1;
    if (!move || move.state.gameOver) {
      const unplayed = horizon - ply;
      return {
        utility: TERMINAL_UTILITY - unplayed * 500_000 + transitionUtility,
        work,
      };
    }
    transitionUtility += scoreGrayTransition(
      before,
      move,
      weights.transition,
    );
    current = {
      ...move.state,
      score: 0,
      nextDisc: scenarioDisc(rootHash, scenario, scenarios, ply),
    };
    if (ply + 1 >= horizon) break;
    const guide = evaluateGrayThroughputMoves(current, {
      samples: guideSamples,
      continuationSamples: 1,
      depth: 1,
      policySeed: mix32(policySeed ^ rootHash ^ Math.imul(ply + 1, PLY_MULTIPLIER)),
      weights,
    });
    work += guide.work;
    if (guide.bestColumn === null) {
      const unplayed = horizon - ply - 1;
      return {
        utility: TERMINAL_UTILITY - unplayed * 500_000 + transitionUtility,
        work,
      };
    }
    column = guide.bestColumn;
  }

  // The terminal state matters most; accumulated transition value provides a
  // smaller path-dependent signal for two rollouts ending at similar loads.
  return {
    utility:
      scoreGrayState(current, weights.state) + transitionUtility * 0.18,
    work,
  };
}

function scenarioDisc(
  hash: number,
  scenario: number,
  scenarios: number,
  ply: number,
): DiscValue {
  return (Math.floor(
    stratified(hash, scenario, scenarios, ply, FUTURE_DISC_DOMAIN) * BOARD_SIZE,
  ) + 1) as DiscValue;
}

function scenarioRandom(
  hash: number,
  scenario: number,
  scenarios: number,
  ply: number,
) {
  let event = 0;
  return () => {
    const value = stratified(
      hash,
      scenario,
      scenarios,
      ply,
      (ply === 0 ? ROOT_REVEAL_DOMAIN : FUTURE_REVEAL_DOMAIN) ^
        Math.imul(event + 1, EVENT_MULTIPLIER),
    );
    event += 1;
    return value;
  };
}

function stratified(
  hash: number,
  scenario: number,
  scenarios: number,
  ply: number,
  domain: number,
) {
  const plyHash = mix32(hash ^ domain ^ Math.imul(ply + 1, PLY_MULTIPLIER));
  const rotation = plyHash % scenarios;
  const stratum = (scenario + rotation) % scenarios;
  const jitter = mix32(
    plyHash ^ Math.imul(scenario + 1, SCENARIO_MULTIPLIER),
  );
  return (stratum + jitter / 4_294_967_296) / scenarios;
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
  const order =
    compareBoardWithMirror(board) <= 0
      ? COLUMN_ORDER
      : [...COLUMN_ORDER].map((value) => BOARD_SIZE - 1 - value);
  return order.indexOf(column as (typeof COLUMN_ORDER)[number]);
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function standardDeviation(values: readonly number[], average: number) {
  return Math.sqrt(
    mean(values.map((value) => (value - average) * (value - average))),
  );
}

function boundedInteger(
  value: number,
  minimum: number,
  maximum: number,
  name: string,
) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`);
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
