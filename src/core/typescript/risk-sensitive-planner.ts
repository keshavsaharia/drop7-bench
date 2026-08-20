import {
  BOARD_SIZE,
  EMPTY,
  playMove,
  serializeBoard,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";

export type RiskSensitiveEvaluator = (state: GameState) => number;

export interface RiskSensitivePlannerOptions {
  /** Common-random chance outcomes evaluated for every legal root action. */
  scenarios: number;
  /** Ordinary risk-neutral expectimax plies after the sampled root move. */
  continuationDepth: number;
  /** Stratified reveal/next-disc samples at continuation chance nodes. */
  chanceSamples: number;
  /** Fraction of the worst outcomes included in conditional value at risk. */
  tailFraction?: number;
  /** 0 selects the mean, 1 selects CVaR, and intermediate values blend them. */
  riskWeight?: number;
  /** Solver-local seed; no game seed or future chance tape is accepted. */
  seed: number;
  evaluator?: RiskSensitiveEvaluator;
  terminalUtility?: number;
  maxWork?: number;
  maxCacheEntries?: number;
}

export interface RiskDistribution {
  mean: number;
  lowerQuantile: number;
  cvar: number;
  selectionValue: number;
}

export interface RiskSensitiveColumnEvaluation extends RiskDistribution {
  column: number;
  scenarios: number;
}

export interface RiskSensitivePlannerResult {
  bestColumn: number | null;
  columns: readonly RiskSensitiveColumnEvaluation[];
  requestedScenarios: number;
  completedScenarios: number;
  continuationDepth: number;
  chanceSamples: number;
  tailFraction: number;
  riskWeight: number;
  complete: boolean;
  stopReason: "complete" | "work";
  work: number;
  transitions: number;
  evaluatedStates: number;
  cacheEntries: number;
  cacheHits: number;
  peakUtilityValues: number;
}

export const MAX_RISK_SCENARIOS = 256;
export const MAX_RISK_CONTINUATION_DEPTH = 4;
export const MAX_RISK_CHANCE_SAMPLES = 16;
export const MAX_RISK_WORK = 5_000_000;
export const MAX_RISK_CACHE_ENTRIES = 100_000;
export const DEFAULT_RISK_WORK = 500_000;
export const DEFAULT_RISK_CACHE_ENTRIES = 40_000;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;
const ROOT_SCENARIO_DOMAIN = 0x5253_434e;
const ROOT_REVEAL_DOMAIN = 0x5252_564c;
const ROOT_DISC_DOMAIN = 0x5252_4443;
const INNER_REVEAL_DOMAIN = 0x4952_564c;
const INNER_DISC_DOMAIN = 0x4944_5343;
const SAMPLE_MULTIPLIER = 0x9e37_79b9;
const EVENT_MULTIPLIER = 0x85eb_ca6b;
const JITTER_DOMAIN = 0x4a49_5452;

interface CanonicalAction {
  state: GameState;
  column: number;
}

interface MutableWork {
  limit: number;
  total: number;
  transitions: number;
  evaluatedStates: number;
  cacheHits: number;
}

interface SearchContext {
  chanceSamples: number;
  seed: number;
  evaluator: RiskSensitiveEvaluator;
  terminalUtility: number;
  maxCacheEntries: number;
  cache: Map<string, number>;
  work: MutableWork;
}

class WorkLimitReached extends Error {}

/**
 * A tail-aware root around an ordinary observable-state expectimax policy.
 *
 * Every root action receives the same stratified chance scenario. Once that
 * chance outcome is visible, continuation moves maximize the usual expected
 * utility; they never receive the outer scenario number or a game RNG. The
 * root alone blends mean utility with lower-tail CVaR, which avoids teaching
 * deeper policy nodes to act as though they know which bad future they occupy.
 *
 * Memory is bounded by `maxCacheEntries + legalColumns * scenarios`. Work is
 * checked before every simulated transition and leaf evaluation. If a limit
 * lands during a scenario, that entire scenario is discarded for every root
 * so common-random comparisons remain paired.
 */
export function evaluateRiskSensitiveMoves(
  input: GameState,
  options: RiskSensitivePlannerOptions,
): RiskSensitivePlannerResult {
  const scenarios = boundedInteger(
    options.scenarios,
    "scenarios",
    1,
    MAX_RISK_SCENARIOS,
  );
  const continuationDepth = boundedInteger(
    options.continuationDepth,
    "continuationDepth",
    0,
    MAX_RISK_CONTINUATION_DEPTH,
  );
  const chanceSamples = boundedInteger(
    options.chanceSamples,
    "chanceSamples",
    1,
    MAX_RISK_CHANCE_SAMPLES,
  );
  const tailFraction = options.tailFraction ?? 0.25;
  if (
    !Number.isFinite(tailFraction) ||
    tailFraction <= 0 ||
    tailFraction > 1
  ) {
    throw new Error("tailFraction must be greater than 0 and at most 1");
  }
  const riskWeight = options.riskWeight ?? 0;
  if (!Number.isFinite(riskWeight) || riskWeight < 0 || riskWeight > 2) {
    throw new Error("riskWeight must be finite and between 0 and 2");
  }
  const seed = unsignedSeed(options.seed);
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new Error("terminalUtility must be finite");
  }
  const maxWork = boundedInteger(
    options.maxWork ?? DEFAULT_RISK_WORK,
    "maxWork",
    1,
    MAX_RISK_WORK,
  );
  const maxCacheEntries = boundedInteger(
    options.maxCacheEntries ?? DEFAULT_RISK_CACHE_ENTRIES,
    "maxCacheEntries",
    1,
    MAX_RISK_CACHE_ENTRIES,
  );
  const evaluator = options.evaluator ?? evaluateHeuristic;
  const work: MutableWork = {
    limit: maxWork,
    total: 0,
    transitions: 0,
    evaluatedStates: 0,
    cacheHits: 0,
  };
  const context: SearchContext = {
    chanceSamples,
    seed,
    evaluator,
    terminalUtility,
    maxCacheEntries,
    cache: new Map(),
    work,
  };
  const rootOrder = input.gameOver
    ? []
    : columnOrderForBoard(input.board).filter(
        (column) => input.board[column] === EMPTY,
      );
  const utilities = new Map<number, number[]>(
    rootOrder.map((column) => [column, []]),
  );
  const observableRootSeed = stateSeed(
    canonicalizeState(withoutScore(input)),
    seed ^ ROOT_SCENARIO_DOMAIN,
    continuationDepth + 1,
  );
  let completedScenarios = 0;
  let stopReason: "complete" | "work" = "complete";

  for (let scenario = 0; scenario < scenarios; scenario += 1) {
    const batch: number[] = [];
    try {
      for (const column of rootOrder) {
        const canonical = canonicalizeRootAction(input, column);
        batch.push(
          evaluateRootScenario(
            canonical,
            scenario,
            scenarios,
            continuationDepth,
            observableRootSeed,
            context,
          ),
        );
      }
    } catch (error) {
      if (!(error instanceof WorkLimitReached)) throw error;
      stopReason = "work";
      break;
    }
    for (let index = 0; index < rootOrder.length; index += 1) {
      utilities.get(rootOrder[index])!.push(batch[index]);
    }
    completedScenarios += 1;
  }

  const columns = rootOrder.map((column) => {
    const values = utilities.get(column)!;
    const distribution = summarizeRiskDistribution(
      values,
      tailFraction,
      riskWeight,
    );
    return {
      column,
      scenarios: values.length,
      ...distribution,
    };
  });
  const bestColumn =
    rootOrder.length === 0 ? null : chooseBestColumn(input.board, columns);

  return {
    bestColumn,
    columns,
    requestedScenarios: scenarios,
    completedScenarios,
    continuationDepth,
    chanceSamples,
    tailFraction,
    riskWeight,
    complete: completedScenarios === scenarios,
    stopReason,
    work: work.total,
    transitions: work.transitions,
    evaluatedStates: work.evaluatedStates,
    cacheEntries: context.cache.size,
    cacheHits: work.cacheHits,
    peakUtilityValues: rootOrder.length * completedScenarios,
  };
}

export function summarizeRiskDistribution(
  values: readonly number[],
  tailFraction: number,
  riskWeight: number,
): RiskDistribution {
  if (values.length === 0) {
    return {
      mean: Number.NEGATIVE_INFINITY,
      lowerQuantile: Number.NEGATIVE_INFINITY,
      cvar: Number.NEGATIVE_INFINITY,
      selectionValue: Number.NEGATIVE_INFINITY,
    };
  }
  const sorted = [...values].sort((first, second) => first - second);
  const mean = sorted.reduce((sum, value) => sum + value, 0) / sorted.length;
  const lowerQuantile = quantile(sorted, tailFraction);
  const tailMass = sorted.length * tailFraction;
  const whole = Math.floor(tailMass);
  const fraction = tailMass - whole;
  let tailTotal = 0;
  for (let index = 0; index < whole; index += 1) {
    tailTotal += sorted[index];
  }
  if (fraction > 0) tailTotal += sorted[whole] * fraction;
  const cvar = tailTotal / tailMass;
  return {
    mean,
    lowerQuantile,
    cvar,
    selectionValue: mean + riskWeight * (cvar - mean),
  };
}

function evaluateRootScenario(
  action: CanonicalAction,
  scenario: number,
  scenarioCount: number,
  continuationDepth: number,
  rootSeed: number,
  context: SearchContext,
) {
  spendWork(context.work, "transitions");
  const move = playMove(
    action.state,
    action.column,
    stratifiedRandom(
      rootSeed,
      scenario,
      scenarioCount,
      ROOT_REVEAL_DOMAIN,
    ),
    { captureAnimation: false },
  );
  if (!move) return context.terminalUtility;
  if (move.state.gameOver) {
    return move.scoreDelta + context.terminalUtility;
  }
  const next = canonicalizeState({
    ...move.state,
    score: 0,
    nextDisc: stratifiedDisc(
      rootSeed,
      scenario,
      scenarioCount,
      ROOT_DISC_DOMAIN,
    ),
  });
  return (
    move.scoreDelta + bestContinuationValue(next, continuationDepth, context)
  );
}

function bestContinuationValue(
  state: GameState,
  depth: number,
  context: SearchContext,
): number {
  if (state.gameOver) return context.terminalUtility;
  if (depth === 0) return evaluateLeaf(state, context);
  const key = `${dynamicStateKey(state)}:${depth}`;
  const cached = context.cache.get(key);
  if (cached !== undefined) {
    context.work.cacheHits += 1;
    context.cache.delete(key);
    context.cache.set(key, cached);
    return cached;
  }
  let best = Number.NEGATIVE_INFINITY;
  for (const column of COLUMN_ORDER) {
    if (state.board[column] !== EMPTY) continue;
    best = Math.max(best, expectedActionValue(state, column, depth, context));
  }
  if (best === Number.NEGATIVE_INFINITY) best = context.terminalUtility;
  setCache(context, key, best);
  return best;
}

function expectedActionValue(
  state: GameState,
  column: number,
  depth: number,
  context: SearchContext,
) {
  const observableSeed = stateSeed(state, context.seed, depth);
  let value = 0;
  for (let sample = 0; sample < context.chanceSamples; sample += 1) {
    spendWork(context.work, "transitions");
    const move = playMove(
      state,
      column,
      stratifiedRandom(
        observableSeed,
        sample,
        context.chanceSamples,
        INNER_REVEAL_DOMAIN,
      ),
      { captureAnimation: false },
    );
    if (!move) {
      value += context.terminalUtility;
      continue;
    }
    if (move.state.gameOver) {
      value += move.scoreDelta + context.terminalUtility;
      continue;
    }
    const next = canonicalizeState({
      ...move.state,
      score: 0,
      nextDisc: stratifiedDisc(
        observableSeed,
        sample,
        context.chanceSamples,
        INNER_DISC_DOMAIN,
      ),
    });
    value +=
      move.scoreDelta + bestContinuationValue(next, depth - 1, context);
  }
  return value / context.chanceSamples;
}

function evaluateLeaf(state: GameState, context: SearchContext) {
  spendWork(context.work, "evaluatedStates");
  const value = context.evaluator(withoutScore(state));
  if (!Number.isFinite(value)) {
    throw new Error("risk-sensitive evaluator must return a finite number");
  }
  return value;
}

function chooseBestColumn(
  board: Board,
  columns: readonly RiskSensitiveColumnEvaluation[],
) {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of columnOrderForBoard(board)) {
    const candidate = columns.find((item) => item.column === column);
    if (candidate && candidate.selectionValue > bestValue) {
      bestValue = candidate.selectionValue;
      bestColumn = column;
    }
  }
  return (
    bestColumn ??
    columnOrderForBoard(board).find((column) => board[column] === EMPTY) ??
    null
  );
}

function spendWork(
  work: MutableWork,
  kind: "transitions" | "evaluatedStates",
) {
  if (work.total >= work.limit) throw new WorkLimitReached();
  work.total += 1;
  work[kind] += 1;
}

function setCache(context: SearchContext, key: string, value: number) {
  while (context.cache.size >= context.maxCacheEntries) {
    const oldest = context.cache.keys().next().value;
    if (oldest === undefined) break;
    context.cache.delete(oldest);
  }
  context.cache.set(key, value);
}

function canonicalizeRootAction(
  state: GameState,
  column: number,
): CanonicalAction {
  const comparison = compareBoardWithMirror(state.board);
  const reflected =
    comparison > 0 ||
    (comparison === 0 && column > Math.floor(BOARD_SIZE / 2));
  return reflected
    ? {
        state: canonicalScorelessState(state, mirrorBoard(state.board)),
        column: BOARD_SIZE - 1 - column,
      }
    : { state: withoutScore(state), column };
}

function canonicalizeState(state: GameState) {
  return compareBoardWithMirror(state.board) > 0
    ? canonicalScorelessState(state, mirrorBoard(state.board))
    : withoutScore(state);
}

function canonicalScorelessState(state: GameState, board: Board): GameState {
  return { ...state, board, score: 0 };
}

function columnOrderForBoard(board: Board) {
  return compareBoardWithMirror(board) <= 0
    ? COLUMN_ORDER
    : MIRRORED_COLUMN_ORDER;
}

function compareBoardWithMirror(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const reverse = board[offset + BOARD_SIZE - 1 - column];
      if (forward < reverse) return -1;
      if (forward > reverse) return 1;
    }
  }
  return 0;
}

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored.push(board[row * BOARD_SIZE + column]);
    }
  }
  return mirrored;
}

function dynamicStateKey(state: GameState) {
  return `${serializeBoard(state.board)}:${state.nextDisc}:${state.level}:${state.movesRemaining}:${state.movesPlayed}`;
}

function stateSeed(state: GameState, seed: number, depth: number) {
  let hash = (seed ^ Math.imul(depth + 1, EVENT_MULTIPLIER)) >>> 0;
  for (const cell of state.board) {
    hash = Math.imul(hash ^ cell, 0x0100_0193) >>> 0;
  }
  for (const value of [
    state.nextDisc,
    state.level,
    state.movesRemaining,
    state.movesPlayed,
    state.gameOver ? 1 : 0,
  ]) {
    hash = Math.imul(hash ^ value, 0x0100_0193) >>> 0;
  }
  return mix32(hash);
}

function stratifiedDisc(
  seed: number,
  sample: number,
  sampleCount: number,
  domain: number,
): DiscValue {
  return (
    Math.floor(stratifiedUnit(seed, sample, sampleCount, domain, 0) * BOARD_SIZE) +
    1
  ) as DiscValue;
}

function stratifiedRandom(
  seed: number,
  sample: number,
  sampleCount: number,
  domain: number,
) {
  let event = 0;
  return () => {
    const value = stratifiedUnit(seed, sample, sampleCount, domain, event);
    event += 1;
    return value;
  };
}

function stratifiedUnit(
  seed: number,
  sample: number,
  sampleCount: number,
  domain: number,
  event: number,
) {
  const eventSeed = mix32(
    seed ^ domain ^ Math.imul(event + 1, EVENT_MULTIPLIER),
  );
  const rotation = eventSeed % sampleCount;
  const stratum = (sample + rotation) % sampleCount;
  const jitter =
    mix32(
      eventSeed ^
        Math.imul(sample + 1, SAMPLE_MULTIPLIER) ^
        JITTER_DOMAIN,
    ) / 4_294_967_296;
  return (stratum + jitter) / sampleCount;
}

function quantile(sorted: readonly number[], fraction: number) {
  const position = fraction * (sorted.length - 1);
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  const mix = position - lower;
  return sorted[lower] * (1 - mix) + sorted[upper] * mix;
}

function withoutScore(state: GameState): GameState {
  return state.score === 0 ? state : { ...state, score: 0 };
}

function boundedInteger(
  value: number,
  name: string,
  minimum: number,
  maximum: number,
) {
  if (
    !Number.isSafeInteger(value) ||
    value < minimum ||
    value > maximum
  ) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`);
  }
  return value;
}

function unsignedSeed(value: number) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error("seed must be a uint32 integer");
  }
  return value >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}
