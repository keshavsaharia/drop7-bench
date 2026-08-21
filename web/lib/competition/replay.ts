import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  createInitialLatentValues,
  legalColumns,
  playMove,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "../../../src/core/typescript/engine.ts";
import type { ScriptedRound } from "../../../src/bench/rounds.ts";
import type { ReplayFrame } from "../leaderboard.ts";

export type ReplayFailure =
  | "empty"
  | "too-many-moves"
  | "invalid-column"
  | "illegal-column"
  | "trailing-moves"
  | "incomplete"
  | "engine-rejected";

export interface CompetitionReplayResult {
  valid: boolean;
  failure: ReplayFailure | null;
  score: number;
  moves: number;
  censored: boolean;
  frames: ReplayFrame[];
}

const CONSTANT_RANDOM = () => 0.5;

/** Replays exactly the submitted choices; invalid choices never receive a fallback. */
export function replayCompetitionColumns(
  round: ScriptedRound,
  columns: readonly number[],
): CompetitionReplayResult {
  if (columns.length === 0) return failure("empty");
  if (columns.length > round.maximumMoves) return failure("too-many-moves");

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
  const frames: ReplayFrame[] = [];

  for (const column of columns) {
    if (state.gameOver || state.movesPlayed >= round.maximumMoves) {
      return result(false, "trailing-moves", state, frames, round);
    }
    if (!Number.isInteger(column) || column < 0 || column > 6) {
      return result(false, "invalid-column", state, frames, round);
    }
    if (!legalColumns(state.board).includes(column)) {
      return result(false, "illegal-column", state, frames, round);
    }

    const disc = state.nextDisc;
    const move = playMove(state, column, CONSTANT_RANDOM, {
      captureAnimation: false,
      latent: {
        values: latent as LatentValues,
        nextCoveredRow: () => round.latentRows[rowCursor++],
      },
    });
    if (!move) return result(false, "engine-rejected", state, frames, round);

    state = move.state;
    if (!state.gameOver && state.movesPlayed < round.maximumMoves) {
      state = { ...state, nextDisc: round.discs[state.movesPlayed] };
    }
    latent = move.latentValues ?? latent;

    let cleared = 0;
    let revealed = 0;
    for (const wave of move.waves) {
      cleared += wave.cleared;
      revealed += wave.revealed;
    }
    frames.push({
      move: state.movesPlayed,
      disc,
      column,
      scoreDelta: move.scoreDelta,
      score: state.score,
      board: state.board.join(""),
      nextDisc: state.gameOver ? null : state.nextDisc,
      movesRemaining: state.movesRemaining,
      chainDepth: move.waves.length,
      cleared,
      revealed,
      levelAdvanced: move.levelAdvanced,
    });
  }

  const complete = state.gameOver || state.movesPlayed >= round.maximumMoves;
  return result(complete, complete ? null : "incomplete", state, frames, round);
}

function failure(reason: ReplayFailure): CompetitionReplayResult {
  return {
    valid: false,
    failure: reason,
    score: 0,
    moves: 0,
    censored: false,
    frames: [],
  };
}

function result(
  valid: boolean,
  failureReason: ReplayFailure | null,
  state: GameState,
  frames: ReplayFrame[],
  round: ScriptedRound,
): CompetitionReplayResult {
  return {
    valid,
    failure: failureReason,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver && state.movesPlayed >= round.maximumMoves,
    frames,
  };
}
