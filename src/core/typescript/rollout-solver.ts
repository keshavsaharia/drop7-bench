import {
  BOARD_SIZE,
  EMPTY,
  playMove,
  seededRandom,
  type Board,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "./heuristic.ts";

export type RolloutEvaluator = (state: GameState) => number;
/**
 * A seed-blind continuation policy. It receives only the state visible to a
 * player after the preceding simulated move, never the rollout seed or chance
 * tape. Returning null ends that sampled continuation with terminal utility.
 */
export type RolloutContinuationPolicy = (
  state: Readonly<GameState>,
) => number | null;

export interface RolloutSolverOptions {
  rollouts: number;
  /** Total simulated moves including the candidate root move. */
  horizon: number;
  /** Reveal samples per greedy policy candidate; defaults to one midpoint. */
  continuationSamples?: number;
  /** Unsigned 32-bit seed used for paired future-disc and reveal samples. */
  seed: number;
  heuristicProfile?: HeuristicProfileName;
  /** Overrides heuristicProfile. It should not depend on state.score. */
  evaluator?: RolloutEvaluator;
  /**
   * Optional observable-state policy for moves after the candidate root.
   * When omitted, the existing sampled greedy continuation is unchanged.
   */
  continuationPolicy?: RolloutContinuationPolicy;
  terminalUtility?: number;
  /**
   * Optional lower-confidence-bound penalty in standard deviations. A
   * positive value favors moves whose sampled continuations are consistently
   * good, which can reduce short-horizon survival failures.
   */
  riskAversion?: number;
  /** Latin-hypercube chance tapes; experimental and off by default. */
  stratifiedSamples?: boolean;
}

export interface RolloutColumnEvaluation {
  column: number;
  mean: number;
  /** Population variance across the sampled continuation utilities. */
  variance: number;
  rollouts: number;
}

export interface RolloutEvaluationResult {
  bestColumn: number | null;
  columns: readonly RolloutColumnEvaluation[];
  rollouts: number;
  horizon: number;
  continuationSamples: number;
  riskAversion: number;
  stratifiedSamples: boolean;
  seed: number;
  /** Simulated moves plus non-terminal heuristic evaluations. */
  work: number;
}

export const MAX_ROLLOUT_HORIZON = 40;
export const MAX_ROLLOUT_COUNT = 100_000;
export const MAX_CONTINUATION_SAMPLES = 8;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;
const DISC_DOMAIN = 0x44495343;
const REVEAL_DOMAIN = 0x5245564c;
const POLICY_DOMAIN = 0x504f4c59;
const JITTER_DOMAIN = 0x4a495454;
const ROLLOUT_MULTIPLIER = 0x9e3779b9;
const PLY_MULTIPLIER = 0x85ebca6b;
const EVENT_MULTIPLIER = 0xc2b2ae35;

interface RunningStats {
  count: number;
  mean: number;
  squaredDeviation: number;
}

interface WorkCounter {
  value: number;
}

/**
 * Bounded-memory sparse-sampling planner for longer Drop7 horizons.
 *
 * Only seven online statistics are retained. Chance is sampled by calling the
 * ordinary engine transition with a deterministic reveal stream; the exact
 * reveal tree is never constructed. Every root column receives the same
 * future-disc sample for a given rollout and ply, reducing comparison noise.
 */
export function evaluateRolloutMoves(
  state: GameState,
  options: RolloutSolverOptions,
): RolloutEvaluationResult {
  const rollouts = boundedPositiveInteger(
    options.rollouts,
    "rollouts",
    MAX_ROLLOUT_COUNT,
  );
  const horizon = boundedPositiveInteger(
    options.horizon,
    "horizon",
    MAX_ROLLOUT_HORIZON,
  );
  const continuationSamples = boundedPositiveInteger(
    options.continuationSamples ?? 1,
    "continuationSamples",
    MAX_CONTINUATION_SAMPLES,
  );
  const seed = unsignedSeed(options.seed);
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new Error("terminalUtility must be finite");
  }
  const riskAversion = options.riskAversion ?? 0;
  if (!Number.isFinite(riskAversion) || riskAversion < 0) {
    throw new Error("riskAversion must be a non-negative finite number");
  }
  const stratifiedSamples = options.stratifiedSamples ?? false;

  const profile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const evaluator =
    options.evaluator ??
    ((position: GameState) => evaluateHeuristic(position, profile));
  const work: WorkCounter = { value: 0 };
  const rootStats: Array<{ column: number; stats: RunningStats }> = [];

  if (!state.gameOver) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      if (state.board[column] !== EMPTY) continue;
      const stats = createRunningStats();
      for (let rollout = 0; rollout < rollouts; rollout += 1) {
        const utility = sampleRootContinuation(
          state,
          column,
          rollout,
          rollouts,
          horizon,
          continuationSamples,
          seed,
          stratifiedSamples,
          evaluator,
          options.continuationPolicy,
          terminalUtility,
          work,
        );
        updateRunningStats(stats, utility);
      }
      rootStats.push({ column, stats });
    }
  }

  const columns = rootStats.map(({ column, stats }) => ({
    column,
    mean: stats.mean,
    variance:
      stats.count === 0 ? 0 : stats.squaredDeviation / stats.count,
    rollouts: stats.count,
  }));
  const bestColumn = chooseBestRootColumn(
    state.board,
    columns,
    riskAversion,
  );

  return {
    bestColumn,
    columns,
    rollouts,
    horizon,
    continuationSamples,
    riskAversion,
    stratifiedSamples,
    seed,
    work: work.value,
  };
}

function sampleRootContinuation(
  initial: GameState,
  rootColumn: number,
  rollout: number,
  rolloutCount: number,
  horizon: number,
  continuationSamples: number,
  seed: number,
  stratifiedSamples: boolean,
  evaluator: RolloutEvaluator,
  continuationPolicy: RolloutContinuationPolicy | undefined,
  terminalUtility: number,
  work: WorkCounter,
) {
  let total = 0;
  let state = initial;
  let column = rootColumn;

  for (let ply = 0; ply < horizon; ply += 1) {
    const move = playMove(
      state,
      column,
      stratifiedSamples
        ? stratifiedRevealRandom(seed, rollout, rolloutCount, ply)
        : seededRandom(revealSeed(seed, rollout, ply)),
      { captureAnimation: false },
    );
    work.value += 1;
    if (!move) return total + terminalUtility;

    total += move.scoreDelta;
    state = move.state;
    if (state.gameOver) return total + terminalUtility;

    // playMove samples its own next disc after resolving reveals. Replacing it
    // here domain-separates future discs from the number of reveals and gives
    // every candidate root column the same disc at this rollout and ply.
    state.nextDisc = rolloutDisc(
      seed,
      rollout,
      rolloutCount,
      ply + 1,
      stratifiedSamples,
    );
    if (ply + 1 === horizon) break;
    const nextColumn = continuationPolicy
      ? choosePolicyColumn(state, continuationPolicy)
      : chooseGreedyColumn(
          state,
          seed,
          rollout,
          ply + 1,
          continuationSamples,
          evaluator,
          terminalUtility,
          work,
        );
    if (nextColumn === null) return total + terminalUtility;
    column = nextColumn;
  }

  return total + evaluateLeaf(state, evaluator, terminalUtility, work);
}

function choosePolicyColumn(
  state: GameState,
  policy: RolloutContinuationPolicy,
) {
  const column = policy(state);
  if (column === null) return null;
  if (
    !Number.isSafeInteger(column) ||
    column < 0 ||
    column >= BOARD_SIZE ||
    state.board[column] !== EMPTY
  ) {
    throw new Error(
      `Rollout continuation policy returned illegal column ${String(column)}`,
    );
  }
  return column;
}

/** A deterministic, chance-blind one-step policy for continuation rollouts. */
function chooseGreedyColumn(
  state: GameState,
  seed: number,
  rollout: number,
  ply: number,
  continuationSamples: number,
  evaluator: RolloutEvaluator,
  terminalUtility: number,
  work: WorkCounter,
) {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of columnOrderForBoard(state.board)) {
    if (state.board[column] !== EMPTY) continue;

    let value = 0;
    for (let sample = 0; sample < continuationSamples; sample += 1) {
      const reveal = continuationRevealSample(
        seed,
        rollout,
        ply,
        sample,
        continuationSamples,
      );
      // The policy probes use a separate random domain from the actual rollout
      // transition, so they estimate a move without peeking at its outcome.
      // A constant value within each probe also preserves mirror symmetry when
      // one wave reveals several covered discs in reverse scan order.
      const move = playMove(state, column, () => reveal, {
        captureAnimation: false,
      });
      work.value += 1;
      if (!move) {
        value += terminalUtility;
        continue;
      }
      value +=
        move.scoreDelta +
        evaluateLeaf(move.state, evaluator, terminalUtility, work);
    }
    value /= continuationSamples;
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function continuationRevealSample(
  seed: number,
  rollout: number,
  ply: number,
  sample: number,
  sampleCount: number,
) {
  // Preserve the fixed midpoint sampling policy exactly at one sample.
  if (sampleCount === 1) return 0.5;
  const rotation = mixedSample(seed, rollout, ply, POLICY_DOMAIN);
  return ((sample + 0.5) / sampleCount + rotation) % 1;
}

function evaluateLeaf(
  state: GameState,
  evaluator: RolloutEvaluator,
  terminalUtility: number,
  work: WorkCounter,
) {
  if (state.gameOver) return terminalUtility;
  work.value += 1;
  const value = evaluator(state);
  if (!Number.isFinite(value)) {
    throw new Error("Rollout evaluator must return a finite number");
  }
  return value;
}

function chooseBestRootColumn(
  board: Board,
  columns: readonly RolloutColumnEvaluation[],
  riskAversion: number,
) {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of columnOrderForBoard(board)) {
    const evaluation = columns.find((candidate) => candidate.column === column);
    const selectionValue = evaluation
      ? evaluation.mean - riskAversion * Math.sqrt(evaluation.variance)
      : Number.NEGATIVE_INFINITY;
    if (evaluation && selectionValue > bestValue) {
      bestValue = selectionValue;
      bestColumn = column;
    }
  }
  return bestColumn;
}

/**
 * Pick tie order in the board's canonical orientation. Distinct mirrored
 * positions therefore make mirrored tie choices; a perfectly symmetric board
 * necessarily retains the ordinary center-first order.
 */
function columnOrderForBoard(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (forward < mirrored) return COLUMN_ORDER;
      if (forward > mirrored) return MIRRORED_COLUMN_ORDER;
    }
  }
  return COLUMN_ORDER;
}

function rolloutDisc(
  seed: number,
  rollout: number,
  rolloutCount: number,
  ply: number,
  stratified: boolean,
): DiscValue {
  const sample = stratified
    ? stratifiedSample(
        seed,
        rollout,
        rolloutCount,
        ply,
        0,
        DISC_DOMAIN,
      )
    : mixedSample(seed, rollout, ply, DISC_DOMAIN);
  return (Math.floor(sample * BOARD_SIZE) + 1) as DiscValue;
}

function revealSeed(seed: number, rollout: number, ply: number) {
  return mix32(
    seed ^
      Math.imul((rollout + 1) >>> 0, ROLLOUT_MULTIPLIER) ^
      Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
      REVEAL_DOMAIN,
  );
}

function stratifiedRevealRandom(
  seed: number,
  rollout: number,
  rolloutCount: number,
  ply: number,
) {
  let event = 0;
  return () => {
    const sample = stratifiedSample(
      seed,
      rollout,
      rolloutCount,
      ply,
      event,
      REVEAL_DOMAIN,
    );
    event += 1;
    return sample;
  };
}

/** Latin-hypercube sample: every event uses every stratum exactly once. */
function stratifiedSample(
  seed: number,
  rollout: number,
  rolloutCount: number,
  ply: number,
  event: number,
  domain: number,
) {
  const eventSeed =
    seed ^
    Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
    Math.imul((event + 1) >>> 0, EVENT_MULTIPLIER) ^
    domain;
  const rotation = mix32(eventSeed) % rolloutCount;
  const stratum = (rollout + rotation) % rolloutCount;
  const jitter = mix32(eventSeed ^ JITTER_DOMAIN) / 4_294_967_296;
  return (stratum + jitter) / rolloutCount;
}

function mixedSample(
  seed: number,
  rollout: number,
  ply: number,
  domain: number,
) {
  return (
    mix32(
      seed ^
        Math.imul((rollout + 1) >>> 0, ROLLOUT_MULTIPLIER) ^
        Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
        domain,
    ) / 4_294_967_296
  );
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846ca68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function createRunningStats(): RunningStats {
  return { count: 0, mean: 0, squaredDeviation: 0 };
}

function updateRunningStats(stats: RunningStats, value: number) {
  stats.count += 1;
  const difference = value - stats.mean;
  stats.mean += difference / stats.count;
  stats.squaredDeviation += difference * (value - stats.mean);
}

function boundedPositiveInteger(value: number, name: string, maximum: number) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new Error(`${name} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function unsignedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("seed must be a uint32 integer");
  }
  return seed >>> 0;
}
