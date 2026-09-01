import {
  BOARD_SIZE,
  CLEAR_BONUS,
  EMPTY,
  SOLID,
  createInitialBoard,
  isBoardEmpty,
  legalColumns,
  placeDisc,
  raiseCoveredRow,
  resolveCascadeWithAnimation,
  type Board,
  type ChainWave,
  type DiscValue,
  type DroppableDisc,
  type LatentBoardOptions,
  type MoveAnimationFrame,
  type RandomSource,
} from "./engine.ts";

export const CLASSIC_FIRST_LEVEL_DROPS = 30;
export const CLASSIC_LEVEL_BONUS = 7_000;
export const CLASSIC_GRAY_DISC_PROBABILITY = 1 / 8;
export const CLASSIC_RULESET = "drop7-classic-decreasing-v1" as const;

export type ClassicDisc = DroppableDisc;

export interface ClassicGameState {
  board: Board;
  nextDisc: ClassicDisc;
  score: number;
  level: number;
  movesRemaining: number;
  movesPlayed: number;
  gameOver: boolean;
}

export interface ClassicMoveResult {
  state: ClassicGameState;
  scoreDelta: number;
  waves: readonly ChainWave[];
  animation: readonly MoveAnimationFrame[];
  clearedBoard: boolean;
  levelAdvanced: boolean;
  latentValues?: readonly (DiscValue | null)[];
}

export interface ClassicPlayMoveOptions {
  captureAnimation?: boolean;
  latent?: LatentBoardOptions;
}

export function classicDropsForLevel(level: number) {
  if (!Number.isInteger(level) || level < 1) {
    throw new Error("A Classic level must be a positive integer");
  }
  return Math.max(1, CLASSIC_FIRST_LEVEL_DROPS - (level - 1));
}

export function randomClassicDisc(
  random: RandomSource = Math.random,
): ClassicDisc {
  const sample = Math.max(0, Math.min(0.999999999999, random()));
  const outcome = Math.floor(sample * 8) + 1;
  return outcome === 8 ? SOLID : (outcome as DiscValue);
}

export function createClassicGame(
  random: RandomSource = Math.random,
): ClassicGameState {
  return {
    board: createInitialBoard(),
    nextDisc: randomClassicDisc(random),
    score: 0,
    level: 1,
    movesRemaining: classicDropsForLevel(1),
    movesPlayed: 0,
    gameOver: false,
  };
}

export function playClassicMove(
  state: ClassicGameState,
  column: number,
  random: RandomSource = Math.random,
  options: ClassicPlayMoveOptions = {},
): ClassicMoveResult | null {
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
  if (latent) {
    latent[droppedIndex] =
      state.nextDisc === SOLID ? (options.latent?.droppedValue ?? null) : null;
  }
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
    const raised = raiseCoveredRow(
      board,
      latent,
      options.latent?.nextCoveredRow,
    );
    if (!raised) {
      gameOver = true;
    } else {
      levelAdvanced = true;
      level += 1;
      movesRemaining = classicDropsForLevel(level);
      scoreDelta += CLASSIC_LEVEL_BONUS;
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
      nextDisc: gameOver ? state.nextDisc : randomClassicDisc(random),
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

function changedIndexes(left: Board, right: Board) {
  const indexes: number[] = [];
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) indexes.push(index);
  }
  return indexes;
}

function occupiedIndexes(board: Board) {
  const indexes: number[] = [];
  for (let index = 0; index < BOARD_SIZE * BOARD_SIZE; index += 1) {
    if (board[index] !== EMPTY) indexes.push(index);
  }
  return indexes;
}
