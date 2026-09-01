import {
  CLASSIC_RULESET,
  classicDropsForLevel,
  playClassicMove,
  type ClassicGameState,
} from "./classic-engine.ts";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  SOLID,
  createInitialBoard,
  createInitialLatentValues,
  playMove,
  type DiscValue,
  type DroppableDisc,
  type GameState,
  type LatentValues,
} from "./engine.ts";

export { CLASSIC_RULESET } from "./classic-engine.ts";

export const HARDCORE_RULESET = "drop7-hardcore-5-v1" as const;
export const RECORDED_GAME_FORMAT = "drop7-recorded-game-v2" as const;
export const MAX_RECORDED_MOVES = 20_000;

export type RecordedRuleset = typeof HARDCORE_RULESET | typeof CLASSIC_RULESET;

/** Complete public choices and private random configuration for one game. */
export interface RecordedGameTape {
  format: typeof RECORDED_GAME_FORMAT;
  ruleset: RecordedRuleset;
  columns: number[];
  discs: DroppableDisc[];
  /** Hidden value for a dropped gray disc; null for numbered drops. */
  dropLatentValues: (DiscValue | null)[];
  /** Initial covered row followed by each successfully risen covered row. */
  coveredRows: DiscValue[][];
}

export type RecordedGameFailure =
  | "invalid-format"
  | "illegal-column"
  | "trailing-move"
  | "incomplete-game"
  | "invalid-configuration";

export interface RecordedGameEvaluation {
  valid: boolean;
  complete: boolean;
  failure: RecordedGameFailure | null;
  score: number;
  level: number;
  moves: number;
  finalState: GameState | ClassicGameState | null;
  coveredRowsConsumed: number;
}

const CONSTANT_RANDOM = () => 0;

export function isRecordedGameTape(value: unknown): value is RecordedGameTape {
  if (!value || typeof value !== "object") return false;
  const tape = value as Partial<RecordedGameTape>;
  if (
    tape.format !== RECORDED_GAME_FORMAT ||
    (tape.ruleset !== HARDCORE_RULESET && tape.ruleset !== CLASSIC_RULESET) ||
    !Array.isArray(tape.columns) ||
    tape.columns.length < 1 ||
    tape.columns.length > MAX_RECORDED_MOVES ||
    !tape.columns.every(isColumn) ||
    !Array.isArray(tape.discs) ||
    tape.discs.length !== tape.columns.length ||
    !Array.isArray(tape.dropLatentValues) ||
    tape.dropLatentValues.length !== tape.discs.length ||
    !Array.isArray(tape.coveredRows) ||
    tape.coveredRows.length < 1 ||
    tape.coveredRows.length > MAX_RECORDED_MOVES + 1 ||
    !tape.coveredRows.every(isCoveredRow)
  ) {
    return false;
  }

  return tape.discs.every((disc, index) => {
    const hidden = tape.dropLatentValues![index];
    if (tape.ruleset === HARDCORE_RULESET) {
      return isDiscValue(disc) && hidden === null;
    }
    return isDroppableDisc(disc) &&
      (disc === SOLID ? isDiscValue(hidden) : hidden === null);
  });
}

/** Replays without any random draws; the client score is never trusted. */
export function evaluateRecordedGameTape(
  value: unknown,
): RecordedGameEvaluation {
  if (!isRecordedGameTape(value)) return failure("invalid-format");
  const tape = value;
  let rowCursor = 1;
  let latent: LatentValues = createInitialLatentValues(tape.coveredRows[0]);
  let state: GameState | ClassicGameState = {
    board: createInitialBoard(),
    nextDisc: tape.discs[0],
    score: 0,
    level: 1,
    movesRemaining:
      tape.ruleset === CLASSIC_RULESET
        ? classicDropsForLevel(1)
        : MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  } as GameState | ClassicGameState;

  for (let index = 0; index < tape.columns.length; index += 1) {
    if (state.gameOver) {
      return result(false, true, "trailing-move", state, rowCursor);
    }
    let move;
    try {
      const latentOptions = {
        values: latent,
        droppedValue: tape.dropLatentValues[index],
        nextCoveredRow: () => {
          const row = tape.coveredRows[rowCursor];
          if (!row) throw new Error("missing-covered-row");
          rowCursor += 1;
          return row;
        },
      };
      move = tape.ruleset === CLASSIC_RULESET
        ? playClassicMove(
            state as ClassicGameState,
            tape.columns[index],
            CONSTANT_RANDOM,
            { captureAnimation: false, latent: latentOptions },
          )
        : playMove(
            state as GameState,
            tape.columns[index],
            CONSTANT_RANDOM,
            { captureAnimation: false, latent: latentOptions },
          );
    } catch {
      return result(false, false, "invalid-configuration", state, rowCursor);
    }
    if (!move?.latentValues) {
      return result(false, false, "illegal-column", state, rowCursor);
    }
    latent = [...move.latentValues];
    state = move.state;
    if (!state.gameOver && index + 1 < tape.discs.length) {
      state = { ...state, nextDisc: tape.discs[index + 1] } as
        | GameState
        | ClassicGameState;
    }
  }

  if (!state.gameOver) {
    return result(false, false, "incomplete-game", state, rowCursor);
  }
  if (rowCursor !== tape.coveredRows.length) {
    return result(false, true, "invalid-configuration", state, rowCursor);
  }
  return result(true, true, null, state, rowCursor);
}

function result(
  valid: boolean,
  complete: boolean,
  reason: RecordedGameFailure | null,
  state: GameState | ClassicGameState,
  coveredRowsConsumed: number,
): RecordedGameEvaluation {
  return {
    valid,
    complete,
    failure: reason,
    score: state.score,
    level: state.level,
    moves: state.movesPlayed,
    finalState: state,
    coveredRowsConsumed,
  };
}

function failure(reason: RecordedGameFailure): RecordedGameEvaluation {
  return {
    valid: false,
    complete: false,
    failure: reason,
    score: 0,
    level: 0,
    moves: 0,
    finalState: null,
    coveredRowsConsumed: 0,
  };
}

function isColumn(value: unknown) {
  return Number.isInteger(value) && (value as number) >= 0 &&
    (value as number) < BOARD_SIZE;
}

function isDiscValue(value: unknown): value is DiscValue {
  return Number.isInteger(value) && (value as number) >= 1 &&
    (value as number) <= BOARD_SIZE;
}

function isDroppableDisc(value: unknown): value is DroppableDisc {
  return isDiscValue(value) || value === SOLID;
}

function isCoveredRow(value: unknown): value is DiscValue[] {
  return Array.isArray(value) && value.length === BOARD_SIZE &&
    value.every(isDiscValue);
}
