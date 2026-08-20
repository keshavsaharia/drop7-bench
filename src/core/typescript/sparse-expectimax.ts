import {
  BOARD_SIZE,
  EMPTY,
  playMove,
  seededRandom,
  serializeBoard,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "./heuristic.ts";

export type SparseExpectimaxEvaluator = (state: GameState) => number;

export interface SparseExpectimaxOptions {
  maxDepth: number;
  chanceSamples: number;
  maxWork?: number;
  maxCacheEntries?: number;
  seed: number;
  /** Latin-hypercube chance draws; disable only for sampling ablations. */
  stratifiedSamples?: boolean;
  heuristicProfile?: HeuristicProfileName;
  evaluator?: SparseExpectimaxEvaluator;
  terminalUtility?: number;
  now?: () => number;
  timeLimitMs?: number;
}

export interface SparseColumnEvaluation {
  column: number;
  value: number;
  expectedScore: number;
}

export interface SparseExpectimaxResult {
  bestColumn: number | null;
  columns: readonly SparseColumnEvaluation[];
  depth: number;
  requestedDepth: number;
  chanceSamples: number;
  stratifiedSamples: boolean;
  complete: boolean;
  nodes: number;
  work: number;
  cacheEntries: number;
  cacheHits: number;
  elapsedMs: number;
}

export const MAX_SPARSE_EXPECTIMAX_DEPTH = 8;
export const MAX_SPARSE_EXPECTIMAX_SAMPLES = 32;
export const MAX_SPARSE_EXPECTIMAX_CACHE_ENTRIES = 100_000;
export const DEFAULT_SPARSE_EXPECTIMAX_CACHE_ENTRIES = 40_000;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const REVEAL_DOMAIN = 0x5245_564c;
const DISC_DOMAIN = 0x4449_5343;
const SAMPLE_MULTIPLIER = 0x9e37_79b9;
const DEPTH_MULTIPLIER = 0x85eb_ca6b;

class WorkLimitReached extends Error {}

interface SearchContext {
  chanceSamples: number;
  stratifiedSamples: boolean;
  seed: number;
  evaluator: SparseExpectimaxEvaluator;
  terminalUtility: number;
  maxWork: number;
  deadline: number;
  now: () => number;
  maxCacheEntries: number;
  cache: Map<string, number>;
  nodes: number;
  work: number;
  cacheHits: number;
}

/**
 * Iterative-deepening sparse-sampling expectimax.
 *
 * Every decision branch receives a small, common set of chance samples. A
 * sampled successor then gets its own maximization node, so future moves may
 * react to the disc and reveals that became visible; this is the important
 * distinction from a fixed rollout. Runtime is O((7s)^d), but memory is only
 * the DFS stack plus a bounded transposition cache.
 */
export function evaluateSparseExpectimaxMoves(
  input: GameState,
  options: SparseExpectimaxOptions,
): SparseExpectimaxResult {
  const requestedDepth = boundedPositiveInteger(
    options.maxDepth,
    "maxDepth",
    MAX_SPARSE_EXPECTIMAX_DEPTH,
  );
  const chanceSamples = boundedPositiveInteger(
    options.chanceSamples,
    "chanceSamples",
    MAX_SPARSE_EXPECTIMAX_SAMPLES,
  );
  const stratifiedSamples = options.stratifiedSamples ?? true;
  const maxCacheEntries = boundedPositiveInteger(
    options.maxCacheEntries ?? DEFAULT_SPARSE_EXPECTIMAX_CACHE_ENTRIES,
    "maxCacheEntries",
    MAX_SPARSE_EXPECTIMAX_CACHE_ENTRIES,
  );
  const maxWork = normalizePositiveLimit(
    options.maxWork ?? Number.POSITIVE_INFINITY,
    "maxWork",
  );
  const timeLimitMs = normalizePositiveLimit(
    options.timeLimitMs ?? Number.POSITIVE_INFINITY,
    "timeLimitMs",
  );
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new TypeError("terminalUtility must be finite");
  }
  const seed = unsignedSeed(options.seed);
  const profile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const evaluator =
    options.evaluator ??
    ((state: GameState) => evaluateHeuristic(state, profile));
  const now = options.now ?? (() => performance.now());
  const startedAt = now();
  const canonicalRoot = canonicalizeState(withoutScore(input));
  const context: SearchContext = {
    chanceSamples,
    stratifiedSamples,
    seed,
    evaluator,
    terminalUtility,
    maxWork,
    deadline: startedAt + timeLimitMs,
    now,
    maxCacheEntries,
    cache: new Map(),
    nodes: 0,
    work: 0,
    cacheHits: 0,
  };
  let completedDepth = 0;
  let completedColumns: SparseColumnEvaluation[] = [];

  if (!input.gameOver) {
    for (let depth = 1; depth <= requestedDepth; depth += 1) {
      try {
        const nextColumns: SparseColumnEvaluation[] = [];
        for (const column of COLUMN_ORDER) {
          if (canonicalRoot.state.board[column] !== EMPTY) continue;
          const evaluation = evaluateAction(
            canonicalRoot.state,
            column,
            depth,
            context,
          );
          nextColumns.push({
            column: canonicalRoot.mirrored
              ? BOARD_SIZE - 1 - column
              : column,
            value: evaluation.value,
            expectedScore: evaluation.expectedScore,
          });
        }
        completedColumns = nextColumns.sort(
          (first, second) => first.column - second.column,
        );
        completedDepth = depth;
      } catch (error) {
        if (!(error instanceof WorkLimitReached)) throw error;
        break;
      }
    }
  }

  if (completedColumns.length === 0 && !input.gameOver) {
    completedColumns = fallbackColumns(input, evaluator, terminalUtility);
  }

  return {
    bestColumn: chooseBestColumn(input.board, completedColumns),
    columns: completedColumns,
    depth: completedDepth,
    requestedDepth,
    chanceSamples,
    stratifiedSamples,
    complete: completedDepth === requestedDepth,
    nodes: context.nodes,
    work: context.work,
    cacheEntries: context.cache.size,
    cacheHits: context.cacheHits,
    elapsedMs: Math.max(0, now() - startedAt),
  };
}

function bestFutureValue(
  state: GameState,
  depth: number,
  context: SearchContext,
): number {
  context.nodes += 1;
  checkBudget(context);
  if (state.gameOver) return context.terminalUtility;
  if (depth === 0) return evaluateLeaf(state, context);

  const key = `${dynamicStateKey(state)}:${depth}`;
  const cached = context.cache.get(key);
  if (cached !== undefined) {
    context.cacheHits += 1;
    context.cache.delete(key);
    context.cache.set(key, cached);
    return cached;
  }

  let best = Number.NEGATIVE_INFINITY;
  for (const column of COLUMN_ORDER) {
    if (state.board[column] !== EMPTY) continue;
    const candidate = evaluateAction(state, column, depth, context).value;
    if (candidate > best) best = candidate;
  }
  if (best === Number.NEGATIVE_INFINITY) best = context.terminalUtility;
  setCachedValue(context, key, best);
  return best;
}

function evaluateAction(
  state: GameState,
  column: number,
  depth: number,
  context: SearchContext,
) {
  const stateSeed = scenarioSeedForState(state, context.seed, depth);
  let value = 0;
  let expectedScore = 0;

  for (let sample = 0; sample < context.chanceSamples; sample += 1) {
    checkBudget(context);
    const move = playMove(
      state,
      column,
      context.stratifiedSamples
        ? stratifiedRandom(
            stateSeed,
            sample,
            context.chanceSamples,
            REVEAL_DOMAIN,
          )
        : seededRandom(
            mix32(
              stateSeed ^
                Math.imul(sample + 1, SAMPLE_MULTIPLIER) ^
                REVEAL_DOMAIN,
            ),
          ),
      { captureAnimation: false },
    );
    context.work += 1;
    if (!move) {
      value += context.terminalUtility;
      continue;
    }

    const scoreDelta = move.scoreDelta;
    expectedScore += scoreDelta;
    if (move.state.gameOver) {
      value += scoreDelta + context.terminalUtility;
      continue;
    }
    const next = canonicalizeState({
      ...move.state,
      score: 0,
      nextDisc: sampledDisc(
        stateSeed,
        sample,
        context.chanceSamples,
        context.stratifiedSamples,
      ),
    }).state;
    value += scoreDelta + bestFutureValue(next, depth - 1, context);
  }

  return {
    value: value / context.chanceSamples,
    expectedScore: expectedScore / context.chanceSamples,
  };
}

function sampledDisc(
  seed: number,
  sample: number,
  sampleCount: number,
  stratified: boolean,
): DiscValue {
  const value = stratified
    ? stratifiedUnit(seed, sample, sampleCount, DISC_DOMAIN, 0)
    : mix32(seed ^ Math.imul(sample + 1, SAMPLE_MULTIPLIER) ^ DISC_DOMAIN) /
      4_294_967_296;
  return (Math.floor(value * BOARD_SIZE) + 1) as DiscValue;
}

/**
 * Each random event receives one point from every stratum before any stratum
 * repeats. This makes seven samples enumerate all seven disc values exactly,
 * while the event-specific rotation and jitter avoid coupling separate gray
 * reveals to one another.
 */
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
    seed ^ domain ^ Math.imul(event + 1, DEPTH_MULTIPLIER),
  );
  const rotation = eventSeed % sampleCount;
  const stratum = (sample + rotation) % sampleCount;
  const jitter =
    mix32(eventSeed ^ Math.imul(sample + 1, SAMPLE_MULTIPLIER)) /
    4_294_967_296;
  return (stratum + jitter) / sampleCount;
}

function evaluateLeaf(state: GameState, context: SearchContext) {
  checkBudget(context);
  context.work += 1;
  const value = context.evaluator(withoutScore(state));
  if (!Number.isFinite(value)) {
    throw new TypeError("Sparse expectimax evaluator must return a finite number");
  }
  return value;
}

function fallbackColumns(
  state: GameState,
  evaluator: SparseExpectimaxEvaluator,
  terminalUtility: number,
) {
  const canonical = canonicalizeState(withoutScore(state));
  const columns: SparseColumnEvaluation[] = [];
  for (const column of COLUMN_ORDER) {
    if (canonical.state.board[column] !== EMPTY) continue;
    const move = playMove(canonical.state, column, () => 0.5, {
      captureAnimation: false,
    });
    if (!move) continue;
    const value =
      move.scoreDelta +
      (move.state.gameOver ? terminalUtility : evaluator(withoutScore(move.state)));
    if (!Number.isFinite(value)) {
      throw new TypeError("Sparse expectimax evaluator must return a finite number");
    }
    columns.push({
      column: canonical.mirrored ? BOARD_SIZE - 1 - column : column,
      value,
      expectedScore: move.scoreDelta,
    });
  }
  return columns.sort((first, second) => first.column - second.column);
}

function chooseBestColumn(
  board: Board,
  columns: readonly SparseColumnEvaluation[],
) {
  const canonical = mirroredRepresentationIsSmaller(board);
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const canonicalColumn of COLUMN_ORDER) {
    const column = canonical
      ? BOARD_SIZE - 1 - canonicalColumn
      : canonicalColumn;
    const evaluation = columns.find((candidate) => candidate.column === column);
    if (evaluation && evaluation.value > bestValue) {
      bestValue = evaluation.value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function checkBudget(context: SearchContext) {
  if (context.work >= context.maxWork) throw new WorkLimitReached();
  if ((context.work & 127) === 0 && context.now() >= context.deadline) {
    throw new WorkLimitReached();
  }
}

function setCachedValue(context: SearchContext, key: string, value: number) {
  if (context.cache.has(key)) context.cache.delete(key);
  while (context.cache.size >= context.maxCacheEntries) {
    const oldest = context.cache.keys().next().value;
    if (oldest === undefined) break;
    context.cache.delete(oldest);
  }
  context.cache.set(key, value);
}

function scenarioSeedForState(state: GameState, seed: number, depth: number) {
  let hash = 0x811c_9dc5;
  for (const cell of state.board) {
    hash ^= cell + 1;
    hash = Math.imul(hash, 0x0100_0193);
  }
  hash ^= state.nextDisc;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.movesRemaining;
  return mix32(hash ^ seed ^ Math.imul(depth + 1, DEPTH_MULTIPLIER));
}

function canonicalizeState(state: GameState) {
  const mirrored = mirroredRepresentationIsSmaller(state.board);
  return {
    state: mirrored ? { ...state, board: mirrorBoard(state.board) } : state,
    mirrored,
  };
}

function mirroredRepresentationIsSmaller(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (mirrored < forward) return true;
      if (mirrored > forward) return false;
    }
  }
  return false;
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

function dynamicStateKey(state: GameState) {
  return `${serializeBoard(state.board)}:${state.nextDisc}:${state.movesRemaining}`;
}

function withoutScore(state: GameState): GameState {
  return state.score === 0 ? state : { ...state, score: 0 };
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function boundedPositiveInteger(value: number, name: string, maximum: number) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new RangeError(`${name} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function normalizePositiveLimit(value: number, name: string) {
  if (value === Number.POSITIVE_INFINITY) return value;
  if (!Number.isFinite(value) || value < 1) {
    throw new RangeError(`${name} must be a positive finite number or Infinity`);
  }
  return Math.trunc(value);
}

function unsignedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new RangeError("seed must be a uint32 integer");
  }
  return seed >>> 0;
}
