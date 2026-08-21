export const BOARD_SIZE = 7;
export const EMPTY = 0 as const;
export const SOLID = 8 as const;
export const CRACKED = 9 as const;

export const MOVES_PER_LEVEL = 5;
/** Hardcore/Blitz awards 17,000 points at its five-drop level boundary. */
export const LEVEL_BONUS = 17_000;
export const CLEAR_BONUS = 70_000;

export type DiscValue = 1 | 2 | 3 | 4 | 5 | 6 | 7;
export type CoveredCell = typeof SOLID | typeof CRACKED;
export type Cell = typeof EMPTY | DiscValue | CoveredCell;
export type Board = readonly Cell[];
export type RandomSource = () => number;

export interface GameState {
  board: Board;
  nextDisc: DiscValue;
  score: number;
  level: number;
  movesRemaining: number;
  movesPlayed: number;
  gameOver: boolean;
}

export interface ChainWave {
  depth: number;
  cleared: number;
  revealed: number;
  points: number;
}

export type MoveAnimationKind =
  | "drop"
  | "burst"
  | "impact"
  | "settle"
  | "rise";

/**
 * A presentation-only snapshot of a move. The engine still resolves every
 * matching disc in a wave simultaneously; burst snapshots reveal that result
 * one disc at a time without changing the underlying rules.
 */
export interface MoveAnimationFrame {
  kind: MoveAnimationKind;
  board: Board;
  indexes: readonly number[];
  chainDepth?: number;
}

export interface MoveResult {
  state: GameState;
  scoreDelta: number;
  waves: readonly ChainWave[];
  animation: readonly MoveAnimationFrame[];
  clearedBoard: boolean;
  levelAdvanced: boolean;
  /** Updated latent board when `PlayMoveOptions.latent` was supplied. */
  latentValues?: readonly (DiscValue | null)[];
}

/**
 * Predetermined hidden values carried by covered cells, aligned with board
 * indexes. Numbered and empty cells always hold `null`. This is the
 * deterministic "latent board" used by scripted benchmark rounds: a gray disc
 * has one fixed value that takes its place when the disc is revealed, instead
 * of drawing the reveal from a random source at that moment.
 */
export type LatentValues = (DiscValue | null)[];

export interface LatentBoardOptions {
  /** Hidden values aligned with board indexes. Copied on entry; never mutated. */
  values: readonly (DiscValue | null)[];
  /** Supplies the seven hidden values for each newly risen covered row, in column order. */
  nextCoveredRow: () => readonly DiscValue[];
}

export interface PlayMoveOptions {
  /** Interactive moves capture presentation snapshots; headless callers may skip them. */
  captureAnimation?: boolean;
  /**
   * Optional latent board. When present, reveals consume the predetermined
   * value of the revealed cell and rises draw their new covered row from
   * `nextCoveredRow`; the move's random source is then used only for the next
   * visible disc. The updated values are returned as `MoveResult.latentValues`.
   */
  latent?: LatentBoardOptions;
}

export interface CascadeOutcome {
  board: Board;
  score: number;
  probability: number;
  waves: readonly ChainWave[];
}

export interface MoveOutcome {
  state: GameState;
  scoreDelta: number;
  probability: number;
}

export type CascadeOutcomeVisitor = (outcome: CascadeOutcome) => void;
export type MoveOutcomeVisitor = (outcome: MoveOutcome) => void;

/** Used by the solver to stop a large reveal tree at its time boundary. */
export class SearchAbortedError extends Error {
  constructor() {
    super("Drop7 search stopped");
    this.name = "SearchAbortedError";
  }
}

const DIRECTIONS = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
] as const;

function indexOf(row: number, column: number) {
  return row * BOARD_SIZE + column;
}

function isInside(row: number, column: number) {
  return (
    row >= 0 &&
    row < BOARD_SIZE &&
    column >= 0 &&
    column < BOARD_SIZE
  );
}

export function isNumbered(cell: Cell): cell is DiscValue {
  return cell >= 1 && cell <= 7;
}

export function emptyBoard(): Board {
  return Array<Cell>(BOARD_SIZE * BOARD_SIZE).fill(EMPTY);
}

export function boardFromRows(rows: readonly (readonly Cell[])[]): Board {
  if (
    rows.length !== BOARD_SIZE ||
    rows.some((row) => row.length !== BOARD_SIZE)
  ) {
    throw new Error("A Drop7 board must be a 7 x 7 matrix");
  }
  return rows.flat();
}

export function boardToRows(board: Board): Cell[][] {
  assertBoard(board);
  return Array.from({ length: BOARD_SIZE }, (_, row) =>
    board.slice(row * BOARD_SIZE, (row + 1) * BOARD_SIZE),
  );
}

export function createInitialBoard(): Board {
  const board = emptyBoard().slice();
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    board[indexOf(BOARD_SIZE - 1, column)] = SOLID;
  }
  return board;
}

export function randomDisc(random: RandomSource): DiscValue {
  const sample = Math.max(0, Math.min(0.999999999999, random()));
  return (Math.floor(sample * BOARD_SIZE) + 1) as DiscValue;
}

/** Small deterministic generator for tests, replays, and hydration-safe games. */
export function seededRandom(seed: number): RandomSource {
  let state = seed >>> 0;
  return () => {
    state += 0x6d2b79f5;
    let value = state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4_294_967_296;
  };
}

export function createGame(random: RandomSource = Math.random): GameState {
  return {
    board: createInitialBoard(),
    nextDisc: randomDisc(random),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
}

export function legalColumns(board: Board): number[] {
  assertBoard(board);
  const columns: number[] = [];
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (board[indexOf(0, column)] === EMPTY) columns.push(column);
  }
  return columns;
}

export function placeDisc(
  board: Board,
  column: number,
  disc: DiscValue,
): Board | null {
  assertBoard(board);
  if (
    !Number.isInteger(column) ||
    column < 0 ||
    column >= BOARD_SIZE ||
    board[indexOf(0, column)] !== EMPTY
  ) {
    return null;
  }

  const next = board.slice();
  for (let row = BOARD_SIZE - 1; row >= 0; row -= 1) {
    const index = indexOf(row, column);
    if (next[index] === EMPTY) {
      next[index] = disc;
      return next;
    }
  }
  return null;
}

export function applyGravity(
  board: Board,
  latent?: LatentValues | null,
): Board {
  assertBoard(board);
  const next = emptyBoard().slice();
  const nextLatent = latent
    ? Array<DiscValue | null>(BOARD_SIZE * BOARD_SIZE).fill(null)
    : null;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let destinationRow = BOARD_SIZE - 1;
    for (let row = BOARD_SIZE - 1; row >= 0; row -= 1) {
      const cell = board[indexOf(row, column)];
      if (cell === EMPTY) continue;
      next[indexOf(destinationRow, column)] = cell;
      if (latent && nextLatent) {
        nextLatent[indexOf(destinationRow, column)] = latent[indexOf(row, column)];
      }
      destinationRow -= 1;
    }
  }
  if (latent && nextLatent) {
    for (let index = 0; index < latent.length; index += 1) {
      latent[index] = nextLatent[index];
    }
  }
  return next;
}

export function contiguousLineLength(
  board: Board,
  row: number,
  column: number,
  axis: "row" | "column",
): number {
  assertBoard(board);
  if (!isInside(row, column) || board[indexOf(row, column)] === EMPTY) return 0;

  const [rowStep, columnStep] = axis === "row" ? [0, 1] : [1, 0];
  let count = 1;
  for (const direction of [-1, 1]) {
    let nextRow = row + rowStep * direction;
    let nextColumn = column + columnStep * direction;
    while (
      isInside(nextRow, nextColumn) &&
      board[indexOf(nextRow, nextColumn)] !== EMPTY
    ) {
      count += 1;
      nextRow += rowStep * direction;
      nextColumn += columnStep * direction;
    }
  }
  return count;
}

export function findPoppers(board: Board): number[] {
  assertBoard(board);
  const poppers: number[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = indexOf(row, column);
      const cell = board[index];
      if (!isNumbered(cell)) continue;
      if (
        contiguousLineLength(board, row, column, "row") === cell ||
        contiguousLineLength(board, row, column, "column") === cell
      ) {
        poppers.push(index);
      }
    }
  }
  return poppers;
}

export function scoreForWave(depth: number) {
  if (!Number.isInteger(depth) || depth < 1) {
    throw new Error("Chain depth must be a positive integer");
  }
  return Math.floor(7 * depth ** 2.5);
}

interface ClearedWave {
  board: Cell[];
  revealIndexes: number[];
}

/** Clear one simultaneous wave and apply every adjacent hit before gravity. */
function clearWave(board: Board, poppers: readonly number[]): ClearedWave {
  const next = board.slice();
  const popping = new Set(poppers);
  for (const index of poppers) next[index] = EMPTY;

  const revealIndexes: number[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = indexOf(row, column);
      const cell = board[index];
      if (cell !== SOLID && cell !== CRACKED) continue;

      let hits = 0;
      for (const [rowDelta, columnDelta] of DIRECTIONS) {
        const neighborRow = row + rowDelta;
        const neighborColumn = column + columnDelta;
        if (
          isInside(neighborRow, neighborColumn) &&
          popping.has(indexOf(neighborRow, neighborColumn))
        ) {
          hits += 1;
        }
      }
      if (hits === 0) continue;

      const hitsNeeded = cell === SOLID ? 2 : 1;
      if (hits >= hitsNeeded) {
        revealIndexes.push(index);
      } else {
        next[index] = CRACKED;
      }
    }
  }
  return { board: next, revealIndexes };
}

export function resolveCascade(
  board: Board,
  random: RandomSource = Math.random,
  startingDepth = 1,
): { board: Board; score: number; waves: readonly ChainWave[] } {
  const result = resolveCascadeWithAnimation(
    board,
    random,
    startingDepth,
    false,
  );
  return { board: result.board, score: result.score, waves: result.waves };
}

function resolveCascadeWithAnimation(
  board: Board,
  random: RandomSource,
  startingDepth: number,
  captureAnimation: boolean,
  latent?: LatentValues | null,
): {
  board: Board;
  score: number;
  waves: readonly ChainWave[];
  animation: readonly MoveAnimationFrame[];
} {
  assertBoard(board);
  let current: Board = board.slice();
  let score = 0;
  const waves: ChainWave[] = [];
  const animation: MoveAnimationFrame[] = [];

  for (let depth = startingDepth; ; depth += 1) {
    const poppers = findPoppers(current);
    if (poppers.length === 0) break;

    if (captureAnimation) {
      const burstingBoard = current.slice();
      for (const index of poppers) {
        animation.push({
          kind: "burst",
          board: burstingBoard.slice(),
          indexes: [index],
          chainDepth: depth,
        });
        burstingBoard[index] = EMPTY;
      }
    }

    const cleared = clearWave(current, poppers);
    for (const index of cleared.revealIndexes) {
      if (latent) {
        const value = latent[index];
        if (value === null || value === undefined || !isNumbered(value)) {
          throw new Error("A revealed covered cell has no valid latent value");
        }
        cleared.board[index] = value;
        latent[index] = null;
      } else {
        cleared.board[index] = randomDisc(random);
      }
    }
    const points = poppers.length * scoreForWave(depth);
    score += points;
    waves.push({
      depth,
      cleared: poppers.length,
      revealed: cleared.revealIndexes.length,
      points,
    });

    if (captureAnimation) {
      const impactedIndexes = changedIndexes(current, cleared.board).filter(
        (index) => !poppers.includes(index),
      );
      animation.push({
        kind: "impact",
        board: cleared.board.slice(),
        indexes: impactedIndexes,
        chainDepth: depth,
      });
    }

    const settled = applyGravity(cleared.board, latent);
    if (captureAnimation && !boardsEqual(cleared.board, settled)) {
      animation.push({
        kind: "settle",
        board: settled,
        indexes: changedIndexes(cleared.board, settled),
        chainDepth: depth,
      });
    }
    current = settled;
  }

  return { board: current, score, waves, animation };
}

/**
 * Resolve every equally likely value hidden under a newly opened gray disc.
 * Equivalent end states are merged, which keeps common searches compact.
 */
export function enumerateCascadeOutcomes(
  board: Board,
  shouldStop: () => boolean = () => false,
  startingDepth = 1,
): CascadeOutcome[] {
  const outcomes = new Map<string, CascadeOutcome>();

  forEachCascadeOutcome(
    board,
    (outcome) => {
      const key = `${serializeBoard(outcome.board)}:${outcome.score}`;
      const previous = outcomes.get(key);
      if (previous) {
        previous.probability += outcome.probability;
      } else {
        outcomes.set(key, { ...outcome });
      }
    },
    shouldStop,
    startingDepth,
  );

  return [...outcomes.values()];
}

/**
 * Visit chance outcomes as they are produced instead of retaining the entire
 * reveal tree. Search uses this path so pathological cascades stay bounded by
 * the recursion depth rather than the number of terminal outcomes.
 */
export function forEachCascadeOutcome(
  board: Board,
  visitOutcome: CascadeOutcomeVisitor,
  shouldStop: () => boolean = () => false,
  startingDepth = 1,
) {
  assertBoard(board);

  const visit = (
    current: Board,
    depth: number,
    score: number,
    probability: number,
    waves: readonly ChainWave[],
  ) => {
    if (shouldStop()) throw new SearchAbortedError();
    const poppers = findPoppers(current);
    if (poppers.length === 0) {
      visitOutcome({ board: current, score, probability, waves });
      return;
    }

    const cleared = clearWave(current, poppers);
    const points = poppers.length * scoreForWave(depth);
    const wave: ChainWave = {
      depth,
      cleared: poppers.length,
      revealed: cleared.revealIndexes.length,
      points,
    };
    const nextWaves = [...waves, wave];

    const assignReveals = (revealIndex: number, branchProbability: number) => {
      if (shouldStop()) throw new SearchAbortedError();
      if (revealIndex === cleared.revealIndexes.length) {
        visit(
          applyGravity(cleared.board),
          depth + 1,
          score + points,
          branchProbability,
          nextWaves,
        );
        return;
      }

      const boardIndex = cleared.revealIndexes[revealIndex];
      for (let value = 1; value <= BOARD_SIZE; value += 1) {
        cleared.board[boardIndex] = value as DiscValue;
        assignReveals(revealIndex + 1, branchProbability / BOARD_SIZE);
      }
    };

    assignReveals(0, probability);
  };

  visit(board, startingDepth, 0, 1, []);
}

export function raiseCoveredRow(
  board: Board,
  latent?: LatentValues | null,
  nextCoveredRow?: () => readonly DiscValue[],
): Board | null {
  assertBoard(board);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (board[indexOf(0, column)] !== EMPTY) return null;
  }

  const raised = emptyBoard().slice();
  for (let row = 0; row < BOARD_SIZE - 1; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      raised[indexOf(row, column)] = board[indexOf(row + 1, column)];
    }
  }
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    raised[indexOf(BOARD_SIZE - 1, column)] = SOLID;
  }
  if (latent) {
    if (!nextCoveredRow) {
      throw new Error("A latent board rise needs a covered-row source");
    }
    const shifted = Array<DiscValue | null>(BOARD_SIZE * BOARD_SIZE).fill(null);
    for (let row = 0; row < BOARD_SIZE - 1; row += 1) {
      for (let column = 0; column < BOARD_SIZE; column += 1) {
        shifted[indexOf(row, column)] = latent[indexOf(row + 1, column)];
      }
    }
    const freshRow = nextCoveredRow();
    if (
      freshRow.length !== BOARD_SIZE ||
      freshRow.some((value) => !isNumbered(value))
    ) {
      throw new Error("A covered row needs exactly seven latent disc values");
    }
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      shifted[indexOf(BOARD_SIZE - 1, column)] = freshRow[column];
    }
    for (let index = 0; index < latent.length; index += 1) {
      latent[index] = shifted[index];
    }
  }
  return raised;
}

/** Hidden values for the starting position: one covered row along the bottom. */
export function createInitialLatentValues(
  bottomRow: readonly DiscValue[],
): LatentValues {
  if (
    bottomRow.length !== BOARD_SIZE ||
    bottomRow.some((value) => !isNumbered(value))
  ) {
    throw new Error("The initial covered row needs exactly seven latent disc values");
  }
  const values = Array<DiscValue | null>(BOARD_SIZE * BOARD_SIZE).fill(null);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    values[indexOf(BOARD_SIZE - 1, column)] = bottomRow[column];
  }
  return values;
}

export function playMove(
  state: GameState,
  column: number,
  random: RandomSource = Math.random,
  options: PlayMoveOptions = {},
): MoveResult | null {
  if (state.gameOver) return null;
  const placed = placeDisc(state.board, column, state.nextDisc);
  if (!placed) return null;

  const captureAnimation = options.captureAnimation ?? true;
  const latent = options.latent ? options.latent.values.slice() : null;
  if (latent && latent.length !== BOARD_SIZE * BOARD_SIZE) {
    throw new Error("A latent board must contain exactly 49 cells");
  }
  const animation: MoveAnimationFrame[] = [];
  const droppedIndex = changedIndexes(state.board, placed)[0];
  if (latent) latent[droppedIndex] = null;
  if (captureAnimation) {
    animation.push({ kind: "drop", board: placed, indexes: [droppedIndex] });
  }
  const firstCascade = resolveCascadeWithAnimation(
    placed,
    random,
    1,
    captureAnimation,
    latent,
  );
  animation.push(...firstCascade.animation);
  let board = firstCascade.board;
  let scoreDelta = firstCascade.score;
  let clearedBoard = isBoardEmpty(board);
  let levelAdvanced = false;
  let gameOver = false;
  let level = state.level;
  let movesRemaining = state.movesRemaining - 1;
  const waves = [...firstCascade.waves];

  if (clearedBoard) scoreDelta += CLEAR_BONUS;

  if (movesRemaining === 0) {
    const raised = raiseCoveredRow(board, latent, options.latent?.nextCoveredRow);
    if (!raised) {
      gameOver = true;
    } else {
      levelAdvanced = true;
      level += 1;
      movesRemaining = MOVES_PER_LEVEL;
      scoreDelta += LEVEL_BONUS;
      if (captureAnimation) {
        animation.push({
          kind: "rise",
          board: raised,
          indexes: occupiedIndexes(raised),
        });
      }
      const levelCascade = resolveCascadeWithAnimation(
        raised,
        random,
        firstCascade.waves.length + 1,
        captureAnimation,
        latent,
      );
      board = levelCascade.board;
      scoreDelta += levelCascade.score;
      waves.push(...levelCascade.waves);
      animation.push(...levelCascade.animation);
      if (isBoardEmpty(board)) {
        scoreDelta += CLEAR_BONUS;
        clearedBoard = true;
      }
    }
  }

  if (!gameOver && legalColumns(board).length === 0) gameOver = true;

  return {
    state: {
      board,
      nextDisc: gameOver ? state.nextDisc : randomDisc(random),
      score: state.score + scoreDelta,
      level,
      movesRemaining,
      movesPlayed: state.movesPlayed + 1,
      gameOver,
    },
    scoreDelta,
    waves,
    animation,
    clearedBoard,
    levelAdvanced,
    ...(latent ? { latentValues: latent } : {}),
  };
}

/** Exact chance outcomes for one move, including reveals and the next disc. */
export function enumerateMoveOutcomes(
  state: GameState,
  column: number,
  shouldStop: () => boolean = () => false,
): MoveOutcome[] {
  const results: MoveOutcome[] = [];
  forEachMoveOutcome(
    state,
    column,
    (outcome) => results.push(outcome),
    shouldStop,
  );
  return mergeMoveOutcomes(results);
}

/** Stream exact move outcomes so the solver never materializes a chance tree. */
export function forEachMoveOutcome(
  state: GameState,
  column: number,
  visitOutcome: MoveOutcomeVisitor,
  shouldStop: () => boolean = () => false,
) {
  if (state.gameOver) return;
  const placed = placeDisc(state.board, column, state.nextDisc);
  if (!placed) return;

  forEachCascadeOutcome(placed, (first) => {
    if (shouldStop()) throw new SearchAbortedError();
    let reward = first.score + (isBoardEmpty(first.board) ? CLEAR_BONUS : 0);
    const remaining = state.movesRemaining - 1;

    if (remaining === 0) {
      const raised = raiseCoveredRow(first.board);
      if (!raised) {
        visitOutcome({
          state: {
            ...state,
            board: first.board,
            score: state.score + reward,
            movesRemaining: 0,
            movesPlayed: state.movesPlayed + 1,
            gameOver: true,
          },
          scoreDelta: reward,
          probability: first.probability,
        });
        return;
      }

      reward += LEVEL_BONUS;
      forEachCascadeOutcome(
        raised,
        (levelOutcome) => {
          let branchReward = reward + levelOutcome.score;
          if (isBoardEmpty(levelOutcome.board)) branchReward += CLEAR_BONUS;
          visitNextDiscOutcomes(
            visitOutcome,
            {
              ...state,
              board: levelOutcome.board,
              score: state.score + branchReward,
              level: state.level + 1,
              movesRemaining: MOVES_PER_LEVEL,
              movesPlayed: state.movesPlayed + 1,
              gameOver: legalColumns(levelOutcome.board).length === 0,
            },
            branchReward,
            first.probability * levelOutcome.probability,
          );
        },
        shouldStop,
        first.waves.length + 1,
      );
    } else {
      visitNextDiscOutcomes(
        visitOutcome,
        {
          ...state,
          board: first.board,
          score: state.score + reward,
          movesRemaining: remaining,
          movesPlayed: state.movesPlayed + 1,
          gameOver: legalColumns(first.board).length === 0,
        },
        reward,
        first.probability,
      );
    }
    },
    shouldStop,
  );
}

function visitNextDiscOutcomes(
  visitOutcome: MoveOutcomeVisitor,
  state: GameState,
  scoreDelta: number,
  probability: number,
) {
  if (state.gameOver) {
    visitOutcome({ state, scoreDelta, probability });
    return;
  }
  for (let value = 1; value <= BOARD_SIZE; value += 1) {
    visitOutcome({
      state: { ...state, nextDisc: value as DiscValue },
      scoreDelta,
      probability: probability / BOARD_SIZE,
    });
  }
}

function mergeMoveOutcomes(outcomes: readonly MoveOutcome[]): MoveOutcome[] {
  const merged = new Map<string, MoveOutcome>();
  for (const outcome of outcomes) {
    const { state } = outcome;
    const key = [
      serializeBoard(state.board),
      state.nextDisc,
      state.level,
      state.movesRemaining,
      state.gameOver ? 1 : 0,
      outcome.scoreDelta,
    ].join(":");
    const previous = merged.get(key);
    if (previous) {
      previous.probability += outcome.probability;
    } else {
      merged.set(key, { ...outcome });
    }
  }
  return [...merged.values()];
}

export function isBoardEmpty(board: Board) {
  return board.every((cell) => cell === EMPTY);
}

export function serializeBoard(board: Board) {
  return board.join("");
}

function boardsEqual(left: Board, right: Board) {
  return left.every((cell, index) => cell === right[index]);
}

function changedIndexes(left: Board, right: Board) {
  const indexes: number[] = [];
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) indexes.push(index);
  }
  return indexes;
}

function occupiedIndexes(board: Board) {
  const indexes: number[] = [];
  for (let index = 0; index < board.length; index += 1) {
    if (board[index] !== EMPTY) indexes.push(index);
  }
  return indexes;
}

function assertBoard(board: Board) {
  if (board.length !== BOARD_SIZE * BOARD_SIZE) {
    throw new Error("A Drop7 board must contain exactly 49 cells");
  }
}
