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

/**
 * One line of the crash-recovery journal: the played column plus enough of the
 * post-move state to verify a deterministic replay reproduced it exactly.
 */
export interface BenchCheckpointEntry {
  /** 1-based move number, matching BenchFrame.move. */
  move: number;
  column: number;
  /** Serialized 49-character board after the move resolves. */
  board: string;
  score: number;
  /** The policy's original choice was illegal and a fallback was played. */
  illegal: boolean;
}

export interface BenchRunOptions {
  captureAnimation?: boolean;
  /**
   * Recorded moves to replay verbatim before consulting the policy again.
   * Each entry is applied through the engine and verified against its
   * recorded board and score; any divergence (changed policy, engine, or
   * round) throws a "checkpoint mismatch" error instead of continuing a
   * different game.
   */
  resume?: readonly BenchCheckpointEntry[];
  /**
   * Called after every completed move with the frame and its journal entry;
   * `resumed` marks moves that came from the resume journal rather than a
   * fresh policy decision.
   */
  onFrame?: (
    frame: BenchFrame,
    entry: BenchCheckpointEntry,
    resumed: boolean,
  ) => void;
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

  const resume = options.resume ?? [];
  while (!state.gameOver && state.movesPlayed < round.maximumMoves) {
    const legal = legalColumns(state.board);
    const recorded = resume[state.movesPlayed];
    let column: number;
    let illegal = false;
    if (recorded) {
      if (recorded.move !== state.movesPlayed + 1 || !legal.includes(recorded.column)) {
        throw new Error(
          `checkpoint mismatch at move ${state.movesPlayed + 1}: recorded column ${recorded.column} is not a legal continuation of this round`,
        );
      }
      column = recorded.column;
      illegal = recorded.illegal;
      if (illegal) illegalMoves += 1;
    } else {
      const chosen = policy.chooseColumn(state);
      if (chosen === null || !legal.includes(chosen)) {
        illegal = true;
        illegalMoves += 1;
        column = legal[0];
      } else {
        column = chosen;
      }
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
    const frame: BenchFrame = {
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
    };
    frames.push(frame);
    const entry: BenchCheckpointEntry = {
      move: frame.move,
      column: frame.column,
      board: frame.board,
      score: frame.score,
      illegal,
    };
    if (recorded && (recorded.board !== entry.board || recorded.score !== entry.score)) {
      throw new Error(
        `checkpoint mismatch at move ${frame.move}: the deterministic replay diverged from the recorded board or score`,
      );
    }
    options.onFrame?.(frame, entry, recorded !== undefined);
  }

  if (resume.length > frames.length) {
    throw new Error(
      `checkpoint mismatch: the journal records ${resume.length} moves but the game ended after ${frames.length}`,
    );
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
