import {
  BOARD_SIZE,
  EMPTY,
  SearchAbortedError,
  forEachMoveOutcome,
  legalColumns,
  playMove,
  serializeBoard,
  type Board,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "./heuristic.ts";

export interface ColumnEvaluation {
  column: number;
  value: number;
  expectedScore: number;
}

export interface EvaluationResult {
  bestColumn: number | null;
  columns: readonly ColumnEvaluation[];
  depth: number;
  requestedDepth: number;
  complete: boolean;
  nodes: number;
  work: number;
  cacheEntries: number;
  cacheHits: number;
  elapsedMs: number;
}

export interface EvaluationOptions {
  maxDepth?: number;
  timeLimitMs?: number;
  /** Deterministic alternative to a wall-clock cutoff for local experiments. */
  maxWork?: number;
  heuristicProfile?: HeuristicProfileName;
  now?: () => number;
  onDepthComplete?: (result: EvaluationResult) => void;
}

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const GAME_OVER_UTILITY = HEURISTIC_GAME_OVER_UTILITY;
const MAX_CACHE_ENTRIES = 40_000;

/**
 * Iterative-deepening expectimax. Drop7 has player decision nodes and chance
 * nodes (new discs and gray reveals), but no adversarial move; "expectiminimax"
 * is the conventional umbrella name for the same search family.
 */
export function evaluateMoves(
  state: GameState,
  options: EvaluationOptions = {},
): EvaluationResult {
  const maxDepth = clampInteger(options.maxDepth ?? 4, 1, 8);
  const timeLimitMs = normalizeLimit(options.timeLimitMs ?? 1_000, 1);
  const maxWork = normalizeLimit(
    options.maxWork ?? Number.POSITIVE_INFINITY,
    1,
  );
  const heuristicProfile =
    options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const now = options.now ?? (() => performance.now());
  const startedAt = now();
  const deadline = startedAt + timeLimitMs;
  let nodes = 0;
  let work = 0;
  let cacheHits = 0;
  let completedDepth = 0;
  let completedColumns: ColumnEvaluation[] = [];
  const cache = new Map<string, number>();

  // Reading the high-resolution clock at every reveal is surprisingly costly.
  // Cascades call this frequently, so checking every 128 operations preserves
  // a tight deadline without making the clock a search bottleneck.
  const shouldStop = () => {
    if (work >= maxWork) return true;
    work += 1;
    return (work & 127) === 0 && now() >= deadline;
  };

  const bestFutureValue = (position: GameState, depth: number): number => {
    nodes += 1;
    if (shouldStop()) throw new SearchAbortedError();

    const cacheKey = stateCacheKey(position, position.gameOver ? 0 : depth);
    const cached = cache.get(cacheKey);
    if (cached !== undefined) {
      cacheHits += 1;
      // Refreshing the key makes the fixed-size map a small LRU cache. This
      // favors the positions reused by the current iterative-deepening pass.
      cache.delete(cacheKey);
      cache.set(cacheKey, cached);
      return cached;
    }

    if (depth === 0 || position.gameOver) {
      const utility = boardUtility(position, heuristicProfile);
      setCachedValue(cache, cacheKey, utility);
      return utility;
    }

    let best = Number.NEGATIVE_INFINITY;
    for (const column of orderedLegalColumns(position)) {
      const { value } = evaluateColumnOutcomes(
        position,
        column,
        depth,
        bestFutureValue,
        shouldStop,
      );
      if (value > best) best = value;
    }
    if (best === Number.NEGATIVE_INFINITY) best = GAME_OVER_UTILITY;
    setCachedValue(cache, cacheKey, best);
    return best;
  };

  for (let depth = 1; depth <= maxDepth; depth += 1) {
    try {
      const nextColumns: ColumnEvaluation[] = [];
      for (const column of orderedLegalColumns(state)) {
        const result = evaluateColumnOutcomes(
          state,
          column,
          depth,
          bestFutureValue,
          shouldStop,
        );
        nextColumns.push({
          column,
          value: result.value,
          expectedScore: result.expectedScore,
        });
      }
      completedColumns = nextColumns.sort((a, b) => a.column - b.column);
      completedDepth = depth;
      options.onDepthComplete?.(
        createEvaluationResult(
          completedColumns,
          completedDepth,
          maxDepth,
          nodes,
          work,
          cache.size,
          cacheHits,
          Math.max(0, now() - startedAt),
        ),
      );
    } catch (error) {
      if (!(error instanceof SearchAbortedError)) throw error;
      break;
    }
  }

  // A pathological reveal can consume the whole first budget. Keep the UI
  // useful with a deterministic one-path estimate, clearly reported as depth 0.
  if (completedColumns.length === 0) {
    completedColumns = legalColumns(state.board).map((column) => {
      const result = playMove(state, column, () => 0.5, {
        captureAnimation: false,
      });
      return {
        column,
        value: result
          ? result.scoreDelta + boardUtility(result.state, heuristicProfile)
          : GAME_OVER_UTILITY,
        expectedScore: result?.scoreDelta ?? 0,
      };
    });
  }

  return createEvaluationResult(
    completedColumns,
    completedDepth,
    maxDepth,
    nodes,
    work,
    cache.size,
    cacheHits,
    Math.max(0, now() - startedAt),
  );
}

function createEvaluationResult(
  columns: readonly ColumnEvaluation[],
  depth: number,
  requestedDepth: number,
  nodes: number,
  work: number,
  cacheEntries: number,
  cacheHits: number,
  elapsedMs: number,
): EvaluationResult {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of COLUMN_ORDER) {
    const evaluation = columns.find((item) => item.column === column);
    if (evaluation && evaluation.value > bestValue) {
      bestValue = evaluation.value;
      bestColumn = column;
    }
  }

  return {
    bestColumn,
    columns,
    depth,
    requestedDepth,
    complete: depth === requestedDepth,
    nodes,
    work,
    cacheEntries,
    cacheHits,
    elapsedMs,
  };
}

function evaluateColumnOutcomes(
  state: GameState,
  column: number,
  depth: number,
  bestFutureValue: (state: GameState, depth: number) => number,
  shouldStop: () => boolean,
) {
  let value = 0;
  let expectedScore = 0;
  forEachMoveOutcome(
    state,
    column,
    (outcome) => {
      const future = bestFutureValue(outcome.state, depth - 1);
      value += outcome.probability * (outcome.scoreDelta + future);
      expectedScore += outcome.probability * outcome.scoreDelta;
    },
    shouldStop,
  );
  return { value, expectedScore };
}

function orderedLegalColumns(state: GameState) {
  return COLUMN_ORDER.filter((column) => state.board[column] === EMPTY);
}

function setCachedValue(
  cache: Map<string, number>,
  key: string,
  value: number,
) {
  if (cache.has(key)) cache.delete(key);
  while (cache.size >= MAX_CACHE_ENTRIES) {
    const oldest = cache.keys().next().value;
    if (oldest === undefined) break;
    cache.delete(oldest);
  }
  cache.set(key, value);
}

function stateCacheKey(state: GameState, depth: number) {
  return [
    canonicalBoardKey(state.board),
    // Every built-in horizon heuristic is board-only. The seven possible next
    // discs therefore share one leaf entry; above the horizon the disc still
    // changes the available move and remains part of the key.
    depth === 0 || state.gameOver ? 0 : state.nextDisc,
    state.movesRemaining,
    state.gameOver ? 1 : 0,
    depth,
  ].join(":");
}

/** Mirrored boards have identical futures, so they share one cache entry. */
function canonicalBoardKey(board: Board) {
  const forward = serializeBoard(board);
  let mirrored = "";
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored += board[offset + column];
    }
  }
  return mirrored < forward ? mirrored : forward;
}

/** The selected profile is evaluated only at the iterative search horizon. */
export function boardUtility(
  state: GameState,
  heuristicProfile: HeuristicProfileName = DEFAULT_HEURISTIC_PROFILE,
) {
  return evaluateHeuristic(state, heuristicProfile);
}

function clampInteger(value: number, minimum: number, maximum: number) {
  if (!Number.isFinite(value)) return minimum;
  return Math.min(maximum, Math.max(minimum, Math.trunc(value)));
}

function normalizeLimit(value: number, minimum: number) {
  if (value === Number.POSITIVE_INFINITY) return value;
  if (!Number.isFinite(value)) return minimum;
  return Math.max(minimum, Math.trunc(value));
}
