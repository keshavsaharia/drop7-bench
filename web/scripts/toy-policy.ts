/**
 * A deliberately trivial public-information policy, used only to produce
 * teaching data for the /learn/concepts pages.
 *
 * The rule is: play the legal column that scores the most points right now;
 * break ties by the lowest column (fewest discs already stacked there), then by
 * the lowest column index. It reads only the visible board, the visible next
 * disc and the rise clock, so it stays inside the information boundary — but it
 * is a *toy*, not a research policy, has no protocol, no preregistration and no
 * seed lease, and its games are never evidence about strategy strength. It
 * exists so that a page can show a real, engine-produced game instead of a
 * hand-drawn one.
 *
 * Games run in the engine's latent mode with a fixed disc tape and fixed hidden
 * covered-row values, so every board on the page is reproducible. Seeds come
 * from the scripted-round playground domain (0x5eed****), which overlaps no
 * research seed range.
 */
import {
  BOARD_SIZE,
  createInitialBoard,
  createInitialLatentValues,
  legalColumns,
  playMove,
  randomDisc,
  seededRandom,
  serializeBoard,
  type Board,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "../../src/core/typescript/engine.ts";

/** A scripted supply of hidden values for the covered rows that rise. */
export interface CoveredRowTape {
  /** Hidden values for the covered row that rises after `rises` rises so far. */
  rowFor: (rises: number) => readonly DiscValue[];
}

export function coveredRowTape(seed: number, rows = 1024): CoveredRowTape {
  const random = seededRandom(seed);
  const tape: DiscValue[][] = [];
  for (let i = 0; i < rows; i += 1) {
    tape.push(Array.from({ length: BOARD_SIZE }, () => randomDisc(random)));
  }
  return { rowFor: (rises: number) => tape[rises % tape.length] };
}

export function columnHeights(board: Board): number[] {
  const heights: number[] = [];
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let height = 0;
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== 0) height += 1;
    }
    heights.push(height);
  }
  return heights;
}

export function occupiedCount(board: Board): number {
  return board.reduce<number>((total, cell) => total + (cell === 0 ? 0 : 1), 0);
}

/** One trial application of a move that never touches the game's own randomness. */
function trial(
  state: GameState,
  column: number,
  latent: LatentValues,
  nextCoveredRow: () => readonly DiscValue[],
) {
  return playMove(state, column, () => 0.5, {
    captureAnimation: false,
    latent: { values: latent, nextCoveredRow },
  });
}

/**
 * The toy choice: most points now, then the lowest column, then the lowest
 * index. Returns null only when no legal column exists.
 */
export function toyChoice(
  state: GameState,
  latent: LatentValues,
  nextCoveredRow: () => readonly DiscValue[],
): number | null {
  const heights = columnHeights(state.board);
  let best: { column: number; points: number; height: number } | null = null;
  for (const column of legalColumns(state.board)) {
    const result = trial(state, column, latent, nextCoveredRow);
    if (!result) continue;
    const candidate = { column, points: result.scoreDelta, height: heights[column] };
    if (
      best === null ||
      candidate.points > best.points ||
      (candidate.points === best.points && candidate.height < best.height)
    ) {
      best = candidate;
    }
  }
  return best ? best.column : null;
}

export interface MoveRecord {
  /** 1-based move number. */
  move: number;
  /** The disc that was dropped and the column it went into. */
  disc: number;
  column: number;
  /** Points awarded by this move, and the running total afterwards. */
  points: number;
  score: number;
  /** Board after the move resolved (engine serialization, row-major from the top). */
  board: string;
  /** Board shape after the move. */
  maxHeight: number;
  occupied: number;
  /** Flow: numbered discs cleared and covered discs revealed by this move. */
  clears: number;
  reveals: number;
  /** Deepest cascade wave in this move; 0 when nothing cleared. */
  maxWaveDepth: number;
  /** True when this move ended a five-move cycle and the board rose. */
  rise: boolean;
  /** Moves left before the next rise, after this move. */
  movesUntilRise: number;
  gameOver: boolean;
}

export interface GameRecord {
  seedHex: string;
  latentSeedHex: string;
  moves: number;
  score: number;
  rises: number;
  clears: number;
  reveals: number;
  clearsPerMove: number;
  revealsPerMove: number;
  pointsPerMove: number;
  levelPoints: number;
  /** Points from cascade waves alone (score minus rise and board-clear bonuses). */
  chainPoints: number;
  boardClears: number;
  finalBoard: string;
  /** Why the game ended: the board could not rise, or no column was open. */
  ending: "no-room-to-rise" | "no-legal-column" | "move-cap";
  history: MoveRecord[];
}

/** Plays one complete game with the toy policy in latent mode. */
export function playToyGame(options: {
  seed: number;
  latentSeed: number;
  maxMoves?: number;
  /** Optional starting board; defaults to the engine's opening position. */
  board?: Board;
  /** Keep the per-move history (off for bulk rollouts). */
  history?: boolean;
}): GameRecord {
  const maxMoves = options.maxMoves ?? 400;
  const keepHistory = options.history ?? true;
  const random = seededRandom(options.seed);
  const tape = coveredRowTape(options.latentSeed);
  const board = options.board ?? createInitialBoard();
  let latent: LatentValues = createInitialLatentValues(tape.rowFor(0));
  if (options.board) {
    // A harvested board carries covered cells anywhere; give each one a value.
    const values: LatentValues = new Array(BOARD_SIZE * BOARD_SIZE).fill(null);
    const row = tape.rowFor(0);
    for (let index = 0; index < board.length; index += 1) {
      const cell = board[index];
      if (cell === 8 || cell === 9) values[index] = row[index % BOARD_SIZE];
    }
    latent = values;
  }

  let state: GameState = {
    board,
    nextDisc: randomDisc(random),
    score: 0,
    level: 1,
    movesRemaining: 5,
    movesPlayed: 0,
    gameOver: false,
  };

  let rises = 0;
  let clears = 0;
  let reveals = 0;
  let chainPoints = 0;
  let boardClears = 0;
  const history: MoveRecord[] = [];
  let ending: GameRecord["ending"] = "move-cap";

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const nextCoveredRow = () => tape.rowFor(rises + 1);
    const column = toyChoice(state, latent, nextCoveredRow);
    if (column === null) {
      ending = "no-legal-column";
      break;
    }
    const disc = state.nextDisc;
    const willRise = state.movesRemaining === 1;
    const result = playMove(state, column, random, {
      captureAnimation: false,
      latent: { values: latent, nextCoveredRow },
    });
    if (!result) {
      ending = "no-legal-column";
      break;
    }
    const moveClears = result.waves.reduce((total, wave) => total + wave.cleared, 0);
    const moveReveals = result.waves.reduce((total, wave) => total + wave.revealed, 0);
    const maxWaveDepth = result.waves.reduce((deepest, wave) => Math.max(deepest, wave.depth), 0);
    clears += moveClears;
    reveals += moveReveals;
    if (result.clearedBoard) boardClears += 1;
    chainPoints +=
      result.scoreDelta -
      (result.levelAdvanced ? 17_000 : 0) -
      (result.clearedBoard ? 70_000 : 0);
    if (result.levelAdvanced) rises += 1;
    latent = (result.latentValues ?? latent).slice();
    state = result.state;
    if (keepHistory) {
      history.push({
        move: state.movesPlayed,
        disc,
        column,
        points: result.scoreDelta,
        score: state.score,
        board: serializeBoard(state.board),
        maxHeight: Math.max(...columnHeights(state.board)),
        occupied: occupiedCount(state.board),
        clears: moveClears,
        reveals: moveReveals,
        maxWaveDepth,
        rise: result.levelAdvanced,
        movesUntilRise: state.movesRemaining,
        gameOver: state.gameOver,
      });
    }
    if (state.gameOver) {
      ending = willRise && !result.levelAdvanced ? "no-room-to-rise" : "no-legal-column";
      break;
    }
  }

  const moves = state.movesPlayed;
  return {
    seedHex: `0x${options.seed.toString(16)}`,
    latentSeedHex: `0x${options.latentSeed.toString(16)}`,
    moves,
    score: state.score,
    rises,
    clears,
    reveals,
    clearsPerMove: moves > 0 ? clears / moves : 0,
    revealsPerMove: moves > 0 ? reveals / moves : 0,
    pointsPerMove: moves > 0 ? state.score / moves : 0,
    levelPoints: rises * 17_000,
    chainPoints,
    boardClears,
    finalBoard: serializeBoard(state.board),
    ending,
    history,
  };
}
