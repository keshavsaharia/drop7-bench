/**
 * Game-tree model for the interactive explorer (web/components/GameTreeExplorer.tsx).
 *
 * Everything here is computed by the repository's TypeScript engine at
 * runtime, in the browser, for one position:
 *
 *   root (a public state)
 *     └─ choice: drop the visible disc in column c           (a MAX node)
 *          └─ outcome: one exact chance outcome of that move  (a CHANCE branch)
 *               value = score gained + the leaf's opinion of the board that
 *                       results, or a shallow search from it
 *
 * The numbers are the browser solver's — the same leaf and the same exact
 * expectation the `/play` solver uses — and they are a demonstration of the
 * mechanics, never research evidence. No value is invented: the expectation
 * streams every exact chance outcome with its probability through the
 * solver's own leaf; the outcomes that are *listed* are the most probable
 * merged ones, and the probability mass that is not listed is reported.
 * Deeper values come from `fastEvaluateMoves` with a fixed work bound so they
 * are deterministic.
 */
import {
  BOARD_SIZE,
  createGame,
  enumerateMoveOutcomes,
  legalColumns,
  playMove,
  seededRandom,
  serializeBoard,
  type GameState,
  type MoveAnimationFrame,
  type RandomSource,
} from "../../../src/core/typescript/engine.ts";
import { HEURISTIC_PROFILES } from "../../../src/core/typescript/heuristic.ts";
import { boardUtility } from "../../../src/core/typescript/solver.ts";
import { FastLeaf } from "./fast-leaf.ts";
import { fastEvaluateMoves, fastForEachMoveOutcome } from "./fast-search.ts";

/** The solver's own tie-break order, centre first. */
export const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;

/** Work bound for the deeper-than-leaf values so they are deterministic. */
export const TREE_SEARCH_MAX_WORK = 400_000;
/** Work bound for the moves that build an opening position from a seed. */
export const POSITION_SEARCH_MAX_WORK = 60_000;
/** Columns whose exact outcome stream is longer than this are not listed. */
export const LIST_STREAM_CAP = 40_000;
/** Deeper-than-leaf values are only computed when a column merges to at most this many outcomes. */
export const DEEP_MERGED_CAP = 2_000;

export interface TreeOutcome {
  /** Stable id within the tree: `${column}:${index}`. */
  id: string;
  state: GameState;
  probability: number;
  scoreDelta: number;
  /** The leaf's opinion of `state` (or a shallow search from it when the choice is deep). */
  leafValue: number;
  /** scoreDelta + leafValue, the quantity the expectation averages. */
  branchValue: number;
  revealedCells: number;
}

export interface TreeChoice {
  column: number;
  legal: boolean;
  /** Exact expected value over every chance outcome: Σ probability × (scoreDelta + value of the result). */
  value: number;
  expectedScore: number;
  /** The most probable merged outcomes, for display. */
  outcomes: TreeOutcome[];
  /** Probability mass of the outcomes not listed. */
  hiddenProbability: number;
  /** Merged outcomes in the full distribution (0 when the stream was too long to merge). */
  mergedOutcomes: number;
  /** Exact chance outcomes streamed for the expectation. */
  streamedOutcomes: number;
  /** True when the stream was too long to list any outcome. */
  unlisted: boolean;
  /** Plies of search below each outcome that this column's value used (0 = leaf only). */
  depthUsed: number;
}

export interface GameTree {
  root: GameState;
  choices: TreeChoice[];
  bestColumn: number | null;
  /** Plies requested below each outcome; a heavy column may use fewer (see TreeChoice.depthUsed). */
  leafDepth: number;
}

/**
 * The first disc of a game is drawn by `createGame` from a fresh generator;
 * the runtime generator skips that draw so the rest of the game continues the
 * same stream — the same convention as the playable game, so a seed here is
 * the same game as `/play?seed=…`.
 */
export function runtimeRandom(seed: number): RandomSource {
  const random = seededRandom(seed >>> 0);
  random();
  return random;
}

/** Reproduce the position `moves` plies into the seeded game, with a shallow solver choosing. */
export function positionFromSeed(seed: number, moves: number): GameState {
  let state = createGame(seededRandom(seed >>> 0));
  const random = runtimeRandom(seed);
  for (let played = 0; played < moves && !state.gameOver; played += 1) {
    const legal = legalColumns(state.board);
    if (legal.length === 0) break;
    const evaluation = fastEvaluateMoves(state, { maxDepth: 2, maxWork: POSITION_SEARCH_MAX_WORK, timeLimitMs: 60_000 });
    const column = evaluation.bestColumn ?? legal[0];
    const result = playMove(state, column, random, { captureAnimation: false });
    if (!result) break;
    state = result.state;
  }
  return state;
}

function countRevealed(before: GameState, after: GameState): number {
  let revealed = 0;
  for (let index = 0; index < before.board.length; index += 1) {
    const was = before.board[index];
    const now = after.board[index];
    if ((was === 8 || was === 9) && now >= 1 && now <= 7) revealed += 1;
  }
  return revealed;
}

function deeperValue(state: GameState, depth: number): number {
  if (depth <= 0 || state.gameOver) return boardUtility(state);
  const evaluation = fastEvaluateMoves(state, { maxDepth: depth, maxWork: TREE_SEARCH_MAX_WORK, timeLimitMs: 60_000 });
  if (evaluation.bestColumn === null) return boardUtility(state);
  const best = evaluation.columns.find((entry) => entry.column === evaluation.bestColumn);
  return best ? best.value : boardUtility(state);
}

/**
 * Build the one-ply tree below `root`. `leafDepth` 0 scores each outcome with
 * the leaf alone; 1 or 2 runs that many further plies of the browser solver
 * from each merged outcome (bounded work, deterministic) — for columns that
 * merge to at most DEEP_MERGED_CAP outcomes; heavier columns fall back to the
 * leaf and say so in `depthUsed`.
 */
export function buildGameTree(root: GameState, leafDepth = 0, maxOutcomes = 7): GameTree {
  const legal = new Set(legalColumns(root.board));
  const leaf = new FastLeaf(HEURISTIC_PROFILES.combined);
  const choices: TreeChoice[] = [];
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (!legal.has(column) || root.gameOver) {
      choices.push({ column, legal: false, value: Number.NEGATIVE_INFINITY, expectedScore: 0, outcomes: [], hiddenProbability: 0, mergedOutcomes: 0, streamedOutcomes: 0, unlisted: true, depthUsed: 0 });
      continue;
    }
    // 1. The exact expectation with the leaf, streamed (never materialised).
    let leafValue = 0;
    let expectedScore = 0;
    let streamed = 0;
    fastForEachMoveOutcome(root, column, (board, _nextDisc, _level, _movesRemaining, _gameOver, scoreDelta, probability) => {
      streamed += 1;
      expectedScore += probability * scoreDelta;
      leafValue += probability * (scoreDelta + leaf.evaluate(board));
    });
    if (streamed > LIST_STREAM_CAP) {
      choices.push({ column, legal: true, value: leafValue, expectedScore, outcomes: [], hiddenProbability: 1, mergedOutcomes: 0, streamedOutcomes: streamed, unlisted: true, depthUsed: 0 });
      continue;
    }
    // 2. The merged distribution, for listing (and for deeper values).
    const merged = enumerateMoveOutcomes(root, column);
    const depthUsed = leafDepth > 0 && merged.length <= DEEP_MERGED_CAP ? leafDepth : 0;
    let value = leafValue;
    const scored: TreeOutcome[] = merged.map((outcome, index) => {
      const outcomeValue = depthUsed > 0 ? deeperValue(outcome.state, depthUsed) : boardUtility(outcome.state);
      return {
        id: `${column}:${index}`,
        state: outcome.state,
        probability: outcome.probability,
        scoreDelta: outcome.scoreDelta,
        leafValue: outcomeValue,
        branchValue: outcome.scoreDelta + outcomeValue,
        revealedCells: countRevealed(root, outcome.state),
      };
    });
    if (depthUsed > 0) {
      value = scored.reduce((sum, outcome) => sum + outcome.probability * outcome.branchValue, 0);
    }
    scored.sort((a, b) => b.probability - a.probability || b.branchValue - a.branchValue);
    const shown = scored.slice(0, maxOutcomes);
    const hiddenProbability = scored.slice(maxOutcomes).reduce((sum, outcome) => sum + outcome.probability, 0);
    choices.push({ column, legal: true, value, expectedScore, outcomes: shown, hiddenProbability, mergedOutcomes: scored.length, streamedOutcomes: streamed, unlisted: false, depthUsed });
  }
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of COLUMN_ORDER) {
    const choice = choices[column];
    if (!choice.legal) continue;
    if (choice.value > bestValue) {
      bestValue = choice.value;
      bestColumn = column;
    }
  }
  return { root, choices, bestColumn, leafDepth };
}

export interface RealizedTransition {
  frames: readonly MoveAnimationFrame[];
  /** The engine state after the move, with the outcome's visible next disc. */
  state: GameState;
  /** True when the engine's random realization reproduced the chosen outcome exactly. */
  matched: boolean;
  attempts: number;
}

/**
 * Produce the engine's own animation frames for the transition root → outcome.
 * The engine draws reveals and the next disc from its random source, so the
 * realization is found by trying seeded sources until the resulting board and
 * terminal flag equal the outcome's (a few tries for one revealed cover, up to
 * `maxAttempts` for several). When no try matches, the first realization's
 * frames are returned with `matched: false` so the caller can say so.
 */
export function realizeTransition(root: GameState, column: number, outcome: TreeOutcome, maxAttempts = 512): RealizedTransition {
  const target = serializeBoard(outcome.state.board);
  let first: RealizedTransition | null = null;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    const result = playMove(root, column, seededRandom(0x7ee5_0000 + attempt), { captureAnimation: true });
    if (!result) break;
    const state: GameState = { ...result.state, nextDisc: outcome.state.nextDisc };
    const candidate: RealizedTransition = { frames: result.animation, state, matched: false, attempts: attempt + 1 };
    if (serializeBoard(result.state.board) === target && result.state.gameOver === outcome.state.gameOver) {
      return { ...candidate, matched: true };
    }
    first ??= candidate;
  }
  return first ?? { frames: [], state: { ...outcome.state }, matched: false, attempts: 0 };
}

export function formatProbability(probability: number): string {
  for (const denominator of [7, 49, 343, 2401]) {
    const numerator = probability * denominator;
    if (Math.abs(numerator - Math.round(numerator)) < 1e-9 && Math.round(numerator) >= 1) {
      return `${Math.round(numerator)}/${denominator}`;
    }
  }
  return `${(probability * 100).toFixed(1)}%`;
}
