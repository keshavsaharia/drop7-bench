import { createHash } from "node:crypto";
import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  createInitialLatentValues,
  legalColumns,
  placeDisc,
  playMove,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "../core/typescript/engine.ts";
import type { BenchPolicy } from "./policies.ts";
import type { ScriptedRound } from "./rounds.ts";

/** One replayable move in a benchmark game. */
export interface BenchFrame {
  /** 1-based move number. */
  move: number;
  /** The disc that was placed. */
  disc: DiscValue;
  /** The column the policy chose (after the legality fallback). */
  column: number;
  scoreDelta: number;
  score: number;
  /** Serialized board immediately after the disc lands, before resolution. */
  placedBoard: string;
  /** Serialized 49-character board after the move resolves. */
  board: string;
  /** The visible next disc after the move; null when the game is over. */
  nextDisc: DiscValue | null;
  movesRemaining: number;
  /** Chain waves in this move. */
  chainDepth: number;
  cleared: number;
  revealed: number;
  levelAdvanced: boolean;
  /** Optional presentation snapshots when a replay artifact is requested. */
  animation?: BenchAnimationFrame[];
}

export interface BenchAnimationFrame {
  kind: "drop" | "burst" | "impact" | "settle" | "rise";
  board: string;
  indexes: number[];
  chainDepth?: number;
}

export interface BenchRunOptions {
  captureAnimation?: boolean;
}

export interface BenchGameResult {
  policyId: string;
  roundId: string;
  score: number;
  moves: number;
  censored: boolean;
  maxChain: number;
  discsCleared: number;
  coveredRevealed: number;
  /** Policy choices that were not legal columns; a legal fallback was played. */
  illegalMoves: number;
  elapsedMs: number;
  /** Deterministic digest of the whole trajectory. */
  checksum: string;
  frames: BenchFrame[];
}

const CONSTANT_RANDOM = () => 0.5;

/**
 * Plays one policy through one scripted round. All randomness is predetermined:
 * visible discs come from the round's tape (indexed by move number) and gray
 * reveals consume the round's latent rows, so the engine's random source is
 * never consulted for gameplay events.
 */
export function playScriptedGame(
  policy: BenchPolicy,
  round: ScriptedRound,
  options: BenchRunOptions = {},
): BenchGameResult {
  const startedAt = performance.now();
  let rowCursor = 1;
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: round.discs[0],
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let latent: readonly (DiscValue | null)[] = createInitialLatentValues(
    round.latentRows[0],
  );
  const frames: BenchFrame[] = [];
  let maxChain = 0;
  let discsCleared = 0;
  let coveredRevealed = 0;
  let illegalMoves = 0;

  while (!state.gameOver && state.movesPlayed < round.maximumMoves) {
    const legal = legalColumns(state.board);
    let column = policy.chooseColumn(state);
    if (column === null || !legal.includes(column)) {
      illegalMoves += 1;
      column = legal[0];
    }
    const disc = state.nextDisc;
    const placedBoard = placeDisc(state.board, column, disc);
    if (!placedBoard) {
      throw new Error(`Legal column ${column} could not place its disc`);
    }
    const move = playMove(state, column, CONSTANT_RANDOM, {
      captureAnimation: options.captureAnimation ?? false,
      latent: {
        values: latent as LatentValues,
        nextCoveredRow: () => {
          if (rowCursor >= round.latentRows.length) {
            throw new Error(
              `Round ${round.id} ran out of latent rows at move ${state.movesPlayed}`,
            );
          }
          return round.latentRows[rowCursor++];
        },
      },
    });
    if (!move) {
      throw new Error(`Legal column ${column} was rejected by the engine`);
    }

    state = move.state;
    if (!state.gameOver) {
      state = { ...state, nextDisc: round.discs[state.movesPlayed] };
    }
    latent = move.latentValues ?? latent;

    maxChain = Math.max(maxChain, move.waves.length);
    let cleared = 0;
    let revealed = 0;
    for (const wave of move.waves) {
      cleared += wave.cleared;
      revealed += wave.revealed;
    }
    discsCleared += cleared;
    coveredRevealed += revealed;
    frames.push({
      move: state.movesPlayed,
      disc,
      column,
      scoreDelta: move.scoreDelta,
      score: state.score,
      placedBoard: placedBoard.join(""),
      board: state.board.join(""),
      nextDisc: state.gameOver ? null : state.nextDisc,
      movesRemaining: state.movesRemaining,
      chainDepth: move.waves.length,
      cleared,
      revealed,
      levelAdvanced: move.levelAdvanced,
      ...(options.captureAnimation
        ? {
            animation: move.animation.map((animationFrame) => ({
              kind: animationFrame.kind,
              board: animationFrame.board.join(""),
              indexes: [...animationFrame.indexes],
              ...(animationFrame.chainDepth === undefined
                ? {}
                : { chainDepth: animationFrame.chainDepth }),
            })),
          }
        : {}),
    });
  }

  const checksum = createHash("sha256")
    .update(
      frames
        .map((frame) => `${frame.column}:${frame.scoreDelta}:${frame.board}`)
        .join("|"),
    )
    .digest("hex")
    .slice(0, 16);

  return {
    policyId: policy.id,
    roundId: round.id,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    maxChain,
    discsCleared,
    coveredRevealed,
    illegalMoves,
    elapsedMs: Math.max(0, performance.now() - startedAt),
    checksum,
    frames,
  };
}
