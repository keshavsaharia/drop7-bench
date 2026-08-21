/**
 * The browser solver: `evaluateMoves` from src/core/typescript/solver.ts with
 * the move generator and leaf replaced by faster, value-identical versions.
 *
 * This is a port of the board-state optimisations recorded in
 * docs/exploratory/finding-13-fast-engine.md (a C++ result) to TypeScript:
 *
 * - O2  wave scores and readiness come from tables (`fast-tables.ts`);
 * - O3  poppers come from one board pass that builds seven row and seven
 *       column occupancy masks, then a 128-entry run-length table, instead of
 *       two rescans per numbered cell per wave;
 * - O4  gravity runs in place and only on columns that lost a disc (reveals
 *       overwrite a cover without making a hole), and the cover-hit scan uses
 *       a flag array instead of a `Set`;
 * - O5  the search tracks the wave count instead of building a wave list per
 *       outcome, and never spreads a `GameState` per chance branch;
 * - O1  the transposition table keys on the position packed into seven 32-bit
 *       words in canonical (mirror-aware) orientation and lives in typed
 *       arrays with an intrusive LRU list (`fast-cache.ts`), instead of a
 *       `Map` keyed by a string serialised twice per node — in V8 that string
 *       key and Map churn were ~85% of a depth-3 decision;
 * - O7  the leaf is allocation-free (`fast-leaf.ts`).
 *
 * What it must preserve — and what `fast-search.test.ts` checks on real
 * positions — is the reference's semantics exactly: the same outcomes in the
 * same order with the same probabilities, the same `shouldStop` call sequence
 * (so `work`, `nodes` and deadline behaviour match), the same cache equivalence
 * classes and LRU policy, and bit-identical column values. A depth-N search
 * here picks the column the reference would pick.
 *
 * Playground tooling, not research evidence: a search that runs in a visitor's
 * browser is a demonstration of the policy, never a tier measurement.
 */

import {
  CLEAR_BONUS,
  CRACKED,
  EMPTY,
  LEVEL_BONUS,
  MOVES_PER_LEVEL,
  SOLID,
  SearchAbortedError,
  legalColumns,
  playMove,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  HEURISTIC_PROFILES,
  evaluateHeuristic,
} from "../../../src/core/typescript/heuristic.ts";
import type {
  ColumnEvaluation,
  EvaluationOptions,
  EvaluationResult,
} from "../../../src/core/typescript/solver.ts";
import { TranspositionTable } from "./fast-cache.ts";
import { FastLeaf } from "./fast-leaf.ts";
import {
  BOARD_SIZE,
  CELL_COUNT,
  RUN_LENGTH,
  SETTLED_COLUMN,
  waveScore,
} from "./fast-tables.ts";

export type { ColumnEvaluation, EvaluationOptions, EvaluationResult };

/** A mutable 49-cell board in the engine's cell encoding. */
export type MutableBoard = number[];

/** The part of a `GameState` the search reads. */
export interface SearchState {
  board: ArrayLike<number>;
  nextDisc: number;
  level: number;
  movesRemaining: number;
  gameOver: boolean;
}

/**
 * Positional visitor for one exact chance outcome of a move. The board is
 * owned by the generator and must not be retained after the call returns.
 */
export type FastOutcomeVisitor = (
  board: MutableBoard,
  nextDisc: number,
  level: number,
  movesRemaining: number,
  gameOver: boolean,
  scoreDelta: number,
  probability: number,
) => void;

type CascadeVisitor = (
  board: MutableBoard,
  score: number,
  probability: number,
  waves: number,
) => void;

type StopCheck = () => boolean;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MAX_CACHE_ENTRIES = 40_000;
const NEVER_STOP: StopCheck = () => false;

/* ------------------------------------------------------------------------ */
/* Board primitives                                                          */
/* ------------------------------------------------------------------------ */

const ROW_MASKS = new Uint8Array(BOARD_SIZE);
const COL_MASKS = new Uint8Array(BOARD_SIZE);
const POP_FLAG = new Uint8Array(CELL_COUNT);
/** Reused across decisions (one search runs at a time); cleared on entry. */
const TABLE = new TranspositionTable(MAX_CACHE_ENTRIES, 65_536);

function copyBoard(board: ArrayLike<number>): MutableBoard {
  const copy = Array<number>(CELL_COUNT);
  for (let index = 0; index < CELL_COUNT; index += 1) copy[index] = board[index];
  return copy;
}

function isBoardEmpty(board: MutableBoard) {
  for (let index = 0; index < CELL_COUNT; index += 1) {
    if (board[index] !== EMPTY) return false;
  }
  return true;
}

function hasLegalColumn(board: MutableBoard) {
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (board[column] === EMPTY) return true;
  }
  return false;
}

/**
 * Poppers in row-major order, or null on a quiet board. Leaves ROW_MASKS and
 * COL_MASKS describing `board` for the caller; they are valid only until the
 * next call.
 */
function findPoppers(board: MutableBoard): number[] | null {
  COL_MASKS.fill(0);
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    let mask = 0;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      if (board[offset + column] !== EMPTY) {
        mask |= 1 << column;
        COL_MASKS[column] |= 1 << row;
      }
    }
    ROW_MASKS[row] = mask;
  }

  let poppers: number[] | null = null;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const rowMask = ROW_MASKS[row];
    if (rowMask === 0) continue;
    const offset = row * BOARD_SIZE;
    const rowBase = rowMask * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = board[offset + column];
      if (cell === EMPTY || cell > 7) continue;
      if (
        RUN_LENGTH[rowBase + column] === cell ||
        RUN_LENGTH[COL_MASKS[column] * BOARD_SIZE + row] === cell
      ) {
        (poppers ??= []).push(offset + column);
      }
    }
  }
  return poppers;
}

let lastSettleMask = 0;

/**
 * Clear one simultaneous wave in place: poppers become empty, covers beside a
 * popper crack or are queued for reveal (row-major). Records in
 * `lastSettleMask` which columns gravity must visit afterwards. Must run
 * directly after `findPoppers` on the same board.
 */
function clearWave(board: MutableBoard, poppers: readonly number[]): number[] | null {
  let settleMask = 0;
  for (let k = 0; k < poppers.length; k += 1) {
    const index = poppers[k];
    POP_FLAG[index] = 1;
    board[index] = EMPTY;
    settleMask |= 1 << index % BOARD_SIZE;
  }
  // Boards reached through the engine are always settled, but an arbitrary
  // caller-supplied position may not be; the reference settles every column,
  // so any column that is not already settled is visited too.
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (!SETTLED_COLUMN[COL_MASKS[column]]) settleMask |= 1 << column;
  }
  lastSettleMask = settleMask;

  let reveals: number[] | null = null;
  for (let index = 0; index < CELL_COUNT; index += 1) {
    const cell = board[index];
    if (cell !== SOLID && cell !== CRACKED) continue;
    const row = (index / BOARD_SIZE) | 0;
    const column = index - row * BOARD_SIZE;
    let hits = 0;
    if (row > 0 && POP_FLAG[index - BOARD_SIZE]) hits += 1;
    if (row + 1 < BOARD_SIZE && POP_FLAG[index + BOARD_SIZE]) hits += 1;
    if (column > 0 && POP_FLAG[index - 1]) hits += 1;
    if (column + 1 < BOARD_SIZE && POP_FLAG[index + 1]) hits += 1;
    if (hits === 0) continue;
    if (hits >= (cell === SOLID ? 2 : 1)) {
      (reveals ??= []).push(index);
    } else {
      board[index] = CRACKED;
    }
  }

  for (let k = 0; k < poppers.length; k += 1) POP_FLAG[poppers[k]] = 0;
  return reveals;
}

/** In-place gravity on the columns in `mask`; order within a column is preserved. */
function settle(board: MutableBoard, mask: number) {
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (!((mask >> column) & 1)) continue;
    let destination = BOARD_SIZE - 1;
    for (let row = BOARD_SIZE - 1; row >= 0; row -= 1) {
      const index = row * BOARD_SIZE + column;
      const cell = board[index];
      if (cell === EMPTY) continue;
      if (destination !== row) {
        board[destination * BOARD_SIZE + column] = cell;
        board[index] = EMPTY;
      }
      destination -= 1;
    }
  }
}

/** Shift every row up one and add a covered row, or null when the top row is occupied. */
function raiseCoveredRow(board: MutableBoard): MutableBoard | null {
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (board[column] !== EMPTY) return null;
  }
  const raised = Array<number>(CELL_COUNT);
  for (let index = 0; index < CELL_COUNT - BOARD_SIZE; index += 1) {
    raised[index] = board[index + BOARD_SIZE];
  }
  for (let index = CELL_COUNT - BOARD_SIZE; index < CELL_COUNT; index += 1) {
    raised[index] = SOLID;
  }
  return raised;
}

/* ------------------------------------------------------------------------ */
/* Exact chance enumeration, mirroring forEachCascadeOutcome/forEachMoveOutcome */
/* ------------------------------------------------------------------------ */

/**
 * Resolve `board` (owned; mutated) through every wave and every equally likely
 * reveal, visiting each terminal quiet board. The `shouldStop` call sequence
 * is the reference's: once on entry to each wave and once per reveal-assignment
 * step, including the terminal step.
 */
function cascade(
  board: MutableBoard,
  depth: number,
  score: number,
  probability: number,
  waves: number,
  visit: CascadeVisitor,
  shouldStop: StopCheck,
) {
  if (shouldStop()) throw new SearchAbortedError();
  const poppers = findPoppers(board);
  if (poppers === null) {
    visit(board, score, probability, waves);
    return;
  }
  const points = poppers.length * waveScore(depth);
  const reveals = clearWave(board, poppers);
  assignReveals(
    board,
    reveals,
    0,
    lastSettleMask,
    depth,
    score + points,
    probability,
    waves + 1,
    visit,
    shouldStop,
  );
}

function assignReveals(
  board: MutableBoard,
  reveals: number[] | null,
  revealIndex: number,
  settleMask: number,
  depth: number,
  score: number,
  probability: number,
  waves: number,
  visit: CascadeVisitor,
  shouldStop: StopCheck,
) {
  if (shouldStop()) throw new SearchAbortedError();
  if (reveals === null || revealIndex === reveals.length) {
    const settled = copyBoard(board);
    settle(settled, settleMask);
    cascade(settled, depth + 1, score, probability, waves, visit, shouldStop);
    return;
  }
  const boardIndex = reveals[revealIndex];
  for (let value = 1; value <= BOARD_SIZE; value += 1) {
    board[boardIndex] = value;
    assignReveals(
      board,
      reveals,
      revealIndex + 1,
      settleMask,
      depth,
      score,
      probability / BOARD_SIZE,
      waves,
      visit,
      shouldStop,
    );
  }
}

function emitNextDiscOutcomes(
  visit: FastOutcomeVisitor,
  board: MutableBoard,
  parentDisc: number,
  level: number,
  movesRemaining: number,
  scoreDelta: number,
  probability: number,
) {
  if (!hasLegalColumn(board)) {
    visit(board, parentDisc, level, movesRemaining, true, scoreDelta, probability);
    return;
  }
  for (let value = 1; value <= BOARD_SIZE; value += 1) {
    visit(board, value, level, movesRemaining, false, scoreDelta, probability / BOARD_SIZE);
  }
}

/**
 * Stream every exact outcome of dropping the next disc in `column`: each
 * cascade reveal combination, the rise when this is the fifth drop, and the
 * seven equally likely next discs. Same outcomes, order and probabilities as
 * the engine's `forEachMoveOutcome`.
 */
export function fastForEachMoveOutcome(
  state: SearchState,
  column: number,
  visit: FastOutcomeVisitor,
  shouldStop: StopCheck = NEVER_STOP,
) {
  if (state.gameOver) return;
  if (
    !Number.isInteger(column) ||
    column < 0 ||
    column >= BOARD_SIZE ||
    state.board[column] !== EMPTY
  ) {
    return;
  }

  const placed = copyBoard(state.board);
  let row = BOARD_SIZE - 1;
  while (placed[row * BOARD_SIZE + column] !== EMPTY) row -= 1;
  placed[row * BOARD_SIZE + column] = state.nextDisc;

  const { nextDisc, level, movesRemaining } = state;
  cascade(
    placed,
    1,
    0,
    1,
    0,
    (board, score, probability, waves) => {
      if (shouldStop()) throw new SearchAbortedError();
      let reward = score + (isBoardEmpty(board) ? CLEAR_BONUS : 0);
      const remaining = movesRemaining - 1;

      if (remaining === 0) {
        const raised = raiseCoveredRow(board);
        if (raised === null) {
          visit(board, nextDisc, level, 0, true, reward, probability);
          return;
        }
        reward += LEVEL_BONUS;
        cascade(
          raised,
          waves + 1,
          0,
          1,
          0,
          (levelBoard, levelScore, levelProbability) => {
            let branchReward = reward + levelScore;
            if (isBoardEmpty(levelBoard)) branchReward += CLEAR_BONUS;
            emitNextDiscOutcomes(
              visit,
              levelBoard,
              nextDisc,
              level + 1,
              MOVES_PER_LEVEL,
              branchReward,
              probability * levelProbability,
            );
          },
          shouldStop,
        );
      } else {
        emitNextDiscOutcomes(visit, board, nextDisc, level, remaining, reward, probability);
      }
    },
    shouldStop,
  );
}

/* ------------------------------------------------------------------------ */
/* Transposition key                                                         */
/* ------------------------------------------------------------------------ */

/**
 * Pack the position into `TABLE.key`. Same equivalence classes as the
 * reference's `stateCacheKey`: a board and its mirror image share a key (the
 * lexicographically smaller orientation is packed), the next disc is dropped
 * at the horizon and on terminal states, and moves remaining, the terminal
 * flag and the depth are included. Eight cells per word, 4 bits each; the
 * seventh word holds cell 48 and the scalar fields.
 */
function packKey(
  board: ArrayLike<number>,
  nextDisc: number,
  movesRemaining: number,
  gameOver: boolean,
  depth: number,
) {
  let mirror = false;
  scan: for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (forward !== mirrored) {
        mirror = mirrored < forward;
        break scan;
      }
    }
  }
  const key = TABLE.key;
  let word = 0;
  let shift = 0;
  let wordIndex = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = board[mirror ? offset + BOARD_SIZE - 1 - column : offset + column];
      word |= cell << shift;
      shift += 4;
      if (shift === 32) {
        key[wordIndex] = word >>> 0;
        wordIndex += 1;
        word = 0;
        shift = 0;
      }
    }
  }
  // Cell 48 sits in bits 0–3 of the last word; the fields follow.
  const disc = depth === 0 || gameOver ? 0 : nextDisc;
  word |= disc << 4;
  word |= movesRemaining << 8;
  word |= (gameOver ? 1 : 0) << 12;
  word |= depth << 13;
  key[wordIndex] = word >>> 0;
}

/* ------------------------------------------------------------------------ */
/* Iterative-deepening expectimax                                            */
/* ------------------------------------------------------------------------ */

/** Drop-in replacement for `evaluateMoves`; same options, same result shape. */
export function fastEvaluateMoves(
  state: GameState,
  options: EvaluationOptions = {},
): EvaluationResult {
  const maxDepth = clampInteger(options.maxDepth ?? 4, 1, 8);
  const timeLimitMs = normalizeLimit(options.timeLimitMs ?? 1_000, 1);
  const maxWork = normalizeLimit(options.maxWork ?? Number.POSITIVE_INFINITY, 1);
  const heuristicProfile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const leaf = new FastLeaf(HEURISTIC_PROFILES[heuristicProfile]);
  const now = options.now ?? (() => performance.now());
  const startedAt = now();
  const deadline = startedAt + timeLimitMs;
  let nodes = 0;
  let work = 0;
  let cacheHits = 0;
  let completedDepth = 0;
  let completedColumns: ColumnEvaluation[] = [];
  const cache = TABLE;
  cache.clear();

  const shouldStop = () => {
    if (work >= maxWork) return true;
    work += 1;
    return (work & 127) === 0 && now() >= deadline;
  };

  let expectedScoreOfLastColumn = 0;

  const bestFutureValue = (
    board: ArrayLike<number>,
    nextDisc: number,
    movesRemaining: number,
    gameOver: boolean,
    depth: number,
  ): number => {
    nodes += 1;
    if (shouldStop()) throw new SearchAbortedError();

    packKey(board, nextDisc, movesRemaining, gameOver, gameOver ? 0 : depth);
    const cached = cache.get();
    if (cached !== undefined) {
      cacheHits += 1;
      return cached;
    }

    if (depth === 0 || gameOver) {
      const utility = gameOver ? HEURISTIC_GAME_OVER_UTILITY : leaf.evaluate(board);
      packKey(board, nextDisc, movesRemaining, gameOver, gameOver ? 0 : depth);
      cache.set(utility);
      return utility;
    }

    let best = Number.NEGATIVE_INFINITY;
    for (const column of COLUMN_ORDER) {
      if (board[column] !== EMPTY) continue;
      const value = columnValue(board, nextDisc, movesRemaining, false, column, depth);
      if (value > best) best = value;
    }
    if (best === Number.NEGATIVE_INFINITY) best = HEURISTIC_GAME_OVER_UTILITY;
    // The recursion above overwrote the shared key; pack this node again.
    packKey(board, nextDisc, movesRemaining, gameOver, depth);
    cache.set(best);
    return best;
  };

  const columnValue = (
    board: ArrayLike<number>,
    nextDisc: number,
    movesRemaining: number,
    gameOver: boolean,
    column: number,
    depth: number,
  ) => {
    let value = 0;
    let expectedScore = 0;
    fastForEachMoveOutcome(
      { board, nextDisc, level: 0, movesRemaining, gameOver },
      column,
      (outcomeBoard, outcomeDisc, _level, outcomeMoves, outcomeOver, scoreDelta, probability) => {
        const future = bestFutureValue(outcomeBoard, outcomeDisc, outcomeMoves, outcomeOver, depth - 1);
        value += probability * (scoreDelta + future);
        expectedScore += probability * scoreDelta;
      },
      shouldStop,
    );
    expectedScoreOfLastColumn = expectedScore;
    return value;
  };

  for (let depth = 1; depth <= maxDepth; depth += 1) {
    try {
      const nextColumns: ColumnEvaluation[] = [];
      for (const column of COLUMN_ORDER) {
        if (state.board[column] !== EMPTY) continue;
        // A terminal root is degenerate (every column is worth 0) but the
        // reference evaluates it that way rather than refusing, so match it.
        const value = columnValue(state.board, state.nextDisc, state.movesRemaining, state.gameOver, column, depth);
        nextColumns.push({ column, value, expectedScore: expectedScoreOfLastColumn });
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

  // Same depth-0 fallback as the reference, computed with the reference's own
  // primitives so the rare path cannot diverge.
  if (completedColumns.length === 0) {
    completedColumns = legalColumns(state.board).map((column) => {
      const result = playMove(state, column, () => 0.5, { captureAnimation: false });
      return {
        column,
        value: result
          ? result.scoreDelta + evaluateHeuristic(result.state, heuristicProfile)
          : HEURISTIC_GAME_OVER_UTILITY,
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

function clampInteger(value: number, minimum: number, maximum: number) {
  if (!Number.isFinite(value)) return minimum;
  return Math.min(maximum, Math.max(minimum, Math.trunc(value)));
}

function normalizeLimit(value: number, minimum: number) {
  if (value === Number.POSITIVE_INFINITY) return value;
  if (!Number.isFinite(value)) return minimum;
  return Math.max(minimum, Math.trunc(value));
}
