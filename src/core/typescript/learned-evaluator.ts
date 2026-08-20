import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  type Board,
  type GameState,
} from "./engine.ts";
import { evaluateHeuristic } from "./heuristic.ts";

/**
 * Compact NNUE-like evaluator shape. Every position activates exactly one
 * token per square, one move-clock token, and one height token per column.
 */
export const LEARNED_EVALUATOR_FORMAT = "drop7-nnue" as const;
export const LEARNED_EVALUATOR_VERSION = 1 as const;
export const LEARNED_EVALUATOR_CELL_KINDS = CRACKED + 1;
export const LEARNED_EVALUATOR_ACCUMULATOR_SIZE = 16;
export const LEARNED_EVALUATOR_HIDDEN_SIZE = 8;

export const POSITION_CELL_TOKEN_OFFSET = 0;
export const POSITION_CELL_TOKEN_COUNT =
  BOARD_SIZE * BOARD_SIZE * LEARNED_EVALUATOR_CELL_KINDS;
export const MOVES_REMAINING_TOKEN_OFFSET =
  POSITION_CELL_TOKEN_OFFSET + POSITION_CELL_TOKEN_COUNT;
export const MOVES_REMAINING_TOKEN_COUNT = MOVES_PER_LEVEL + 1;
export const COLUMN_HEIGHT_TOKEN_OFFSET =
  MOVES_REMAINING_TOKEN_OFFSET + MOVES_REMAINING_TOKEN_COUNT;
export const COLUMN_HEIGHT_TOKEN_COUNT =
  BOARD_SIZE * (BOARD_SIZE + 1);
export const LEARNED_EVALUATOR_TOKEN_COUNT =
  COLUMN_HEIGHT_TOKEN_OFFSET + COLUMN_HEIGHT_TOKEN_COUNT;
export const LEARNED_EVALUATOR_ACTIVE_TOKEN_COUNT =
  BOARD_SIZE * BOARD_SIZE + 1 + BOARD_SIZE;

export const LEARNED_POLICY_FORMAT = "drop7-policy-nnue" as const;
export const LEARNED_POLICY_VERSION = 1 as const;
export const LEARNED_POLICY_ACCUMULATOR_SIZE = 64;
export const LEARNED_POLICY_HIDDEN_SIZE = 32;
export const NEXT_DISC_TOKEN_OFFSET = LEARNED_EVALUATOR_TOKEN_COUNT;
export const NEXT_DISC_TOKEN_COUNT = BOARD_SIZE;
export const LEARNED_POLICY_TOKEN_COUNT =
  NEXT_DISC_TOKEN_OFFSET + NEXT_DISC_TOKEN_COUNT;
export const LEARNED_POLICY_ACTIVE_TOKEN_COUNT =
  LEARNED_EVALUATOR_ACTIVE_TOKEN_COUNT + 1;

const EMBEDDING_WEIGHT_COUNT =
  LEARNED_EVALUATOR_TOKEN_COUNT * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
const HIDDEN_WEIGHT_COUNT =
  LEARNED_EVALUATOR_HIDDEN_SIZE * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
const POLICY_COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;

export type LearnedEvaluatorPosition = Pick<
  GameState,
  "board" | "movesRemaining"
>;

/** JSON-safe artifact shape used by training tools and worker messages. */
export interface SerializedLearnedEvaluatorWeights {
  readonly format: typeof LEARNED_EVALUATOR_FORMAT;
  readonly version: typeof LEARNED_EVALUATOR_VERSION;
  /**
   * Optional fixed baseline. Residual models start from a known-safe policy
   * and spend their small network capacity only on corrections to it.
   */
  readonly baseline?: "combined";
  /** Optional safety rails applied to the learned residual before the baseline. */
  readonly residualMinimum?: number;
  readonly residualMaximum?: number;
  /** Row-major [token][accumulator unit]. */
  readonly embedding: readonly number[];
  readonly accumulatorBias: readonly number[];
  /** Row-major [hidden unit][accumulator unit]. */
  readonly hiddenWeights: readonly number[];
  readonly hiddenBias: readonly number[];
  readonly outputWeights: readonly number[];
  readonly outputBias: number;
}

const COMPILED_WEIGHTS = Symbol("compiled Drop7 learned evaluator weights");
const COMPILED_POLICY_WEIGHTS = Symbol(
  "compiled Drop7 learned policy weights",
);

export type LearnedPolicyPosition = Pick<
  GameState,
  "board" | "movesRemaining" | "nextDisc"
>;

export interface SerializedLearnedPolicyWeights {
  readonly format: typeof LEARNED_POLICY_FORMAT;
  readonly version: typeof LEARNED_POLICY_VERSION;
  readonly embedding: readonly number[];
  readonly accumulatorBias: readonly number[];
  readonly hiddenWeights: readonly number[];
  readonly hiddenBias: readonly number[];
  /** Row-major [canonical column][hidden unit]. */
  readonly outputWeights: readonly number[];
  readonly outputBias: readonly number[];
}

export interface CompiledLearnedPolicyWeights {
  readonly format: typeof LEARNED_POLICY_FORMAT;
  readonly version: typeof LEARNED_POLICY_VERSION;
  readonly embedding: Float32Array;
  readonly accumulatorBias: Float32Array;
  readonly hiddenWeights: Float32Array;
  readonly hiddenBias: Float32Array;
  readonly outputWeights: Float32Array;
  readonly outputBias: Float32Array;
  readonly [COMPILED_POLICY_WEIGHTS]: true;
}

export interface LearnedPolicyTokenization {
  readonly tokenIds: Uint16Array;
  readonly mirrored: boolean;
}

export interface LearnedPolicyEvaluation {
  /** Logits are mapped back to the caller's physical column orientation. */
  readonly logits: Float32Array;
  readonly bestColumn: number | null;
}

/** Float32 runtime representation. Compile once, then reuse for inference. */
export interface CompiledLearnedEvaluatorWeights {
  readonly format: typeof LEARNED_EVALUATOR_FORMAT;
  readonly version: typeof LEARNED_EVALUATOR_VERSION;
  readonly baseline?: "combined";
  readonly residualMinimum?: number;
  readonly residualMaximum?: number;
  readonly embedding: Float32Array;
  readonly accumulatorBias: Float32Array;
  readonly hiddenWeights: Float32Array;
  readonly hiddenBias: Float32Array;
  readonly outputWeights: Float32Array;
  readonly outputBias: number;
  readonly [COMPILED_WEIGHTS]: true;
}

export interface LearnedEvaluatorTokenization {
  /** IDs are ordered deterministically and index rows in the embedding table. */
  readonly tokenIds: Uint16Array;
  /** Whether the supplied board was reflected to reach its canonical form. */
  readonly mirrored: boolean;
}

/**
 * Convert a board to its horizontally canonical sparse feature set. Mirrored
 * positions produce byte-for-byte identical token arrays, which also makes
 * Float32 accumulation exactly mirror invariant rather than merely close.
 */
export function extractLearnedEvaluatorTokens(
  position: LearnedEvaluatorPosition,
): LearnedEvaluatorTokenization {
  assertPosition(position);

  const mirrored = mirroredRepresentationIsSmaller(position.board);
  const tokenIds = new Uint16Array(LEARNED_EVALUATOR_ACTIVE_TOKEN_COUNT);
  let cursor = 0;

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const sourceColumn = mirrored ? BOARD_SIZE - 1 - column : column;
      const canonicalIndex = row * BOARD_SIZE + column;
      const cell = position.board[row * BOARD_SIZE + sourceColumn];
      tokenIds[cursor] =
        POSITION_CELL_TOKEN_OFFSET +
        canonicalIndex * LEARNED_EVALUATOR_CELL_KINDS +
        cell;
      cursor += 1;
    }
  }

  tokenIds[cursor] =
    MOVES_REMAINING_TOKEN_OFFSET + position.movesRemaining;
  cursor += 1;

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const sourceColumn = mirrored ? BOARD_SIZE - 1 - column : column;
    const height = columnHeight(position.board, sourceColumn);
    tokenIds[cursor] =
      COLUMN_HEIGHT_TOKEN_OFFSET + column * (BOARD_SIZE + 1) + height;
    cursor += 1;
  }

  return { tokenIds, mirrored };
}

/** Validate a deserialized artifact before allocating its Float32 model. */
export function validateSerializedLearnedEvaluatorWeights(
  value: unknown,
): asserts value is SerializedLearnedEvaluatorWeights {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new TypeError("Learned evaluator weights must be an object");
  }

  const candidate = value as Record<string, unknown>;
  if (candidate.format !== LEARNED_EVALUATOR_FORMAT) {
    throw new TypeError(
      `Learned evaluator format must be ${LEARNED_EVALUATOR_FORMAT}`,
    );
  }
  if (candidate.version !== LEARNED_EVALUATOR_VERSION) {
    throw new TypeError(
      `Learned evaluator version must be ${LEARNED_EVALUATOR_VERSION}`,
    );
  }
  if (
    candidate.baseline !== undefined &&
    candidate.baseline !== "combined"
  ) {
    throw new TypeError(
      "Learned evaluator baseline must be combined when present",
    );
  }
  if (candidate.residualMinimum !== undefined) {
    assertFloat(candidate.residualMinimum, "residualMinimum");
  }
  if (candidate.residualMaximum !== undefined) {
    assertFloat(candidate.residualMaximum, "residualMaximum");
  }
  if (
    typeof candidate.residualMinimum === "number" &&
    typeof candidate.residualMaximum === "number" &&
    candidate.residualMinimum > candidate.residualMaximum
  ) {
    throw new RangeError(
      "Learned evaluator residualMinimum cannot exceed residualMaximum",
    );
  }

  assertFloatArray(candidate.embedding, EMBEDDING_WEIGHT_COUNT, "embedding");
  assertFloatArray(
    candidate.accumulatorBias,
    LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
    "accumulatorBias",
  );
  assertFloatArray(
    candidate.hiddenWeights,
    HIDDEN_WEIGHT_COUNT,
    "hiddenWeights",
  );
  assertFloatArray(
    candidate.hiddenBias,
    LEARNED_EVALUATOR_HIDDEN_SIZE,
    "hiddenBias",
  );
  assertFloatArray(
    candidate.outputWeights,
    LEARNED_EVALUATOR_HIDDEN_SIZE,
    "outputWeights",
  );
  assertFloat(candidate.outputBias, "outputBias");
}

/** Copy a validated JSON artifact into compact Float32 inference buffers. */
export function compileLearnedEvaluatorWeights(
  value: unknown,
): CompiledLearnedEvaluatorWeights {
  validateSerializedLearnedEvaluatorWeights(value);

  return Object.freeze({
    format: value.format,
    version: value.version,
    ...(value.baseline === undefined ? {} : { baseline: value.baseline }),
    ...(value.residualMinimum === undefined
      ? {}
      : { residualMinimum: Math.fround(value.residualMinimum) }),
    ...(value.residualMaximum === undefined
      ? {}
      : { residualMaximum: Math.fround(value.residualMaximum) }),
    embedding: new Float32Array(value.embedding),
    accumulatorBias: new Float32Array(value.accumulatorBias),
    hiddenWeights: new Float32Array(value.hiddenWeights),
    hiddenBias: new Float32Array(value.hiddenBias),
    outputWeights: new Float32Array(value.outputWeights),
    outputBias: Math.fround(value.outputBias),
    [COMPILED_WEIGHTS]: true as const,
  });
}

/**
 * Deterministic Float32 inference. Math.fround fixes every multiply/add
 * boundary so browsers and the headless Node runner follow the same path.
 */
export function evaluateLearnedPosition(
  position: LearnedEvaluatorPosition,
  weights: CompiledLearnedEvaluatorWeights,
): number {
  if (weights?.[COMPILED_WEIGHTS] !== true) {
    throw new TypeError(
      "Learned evaluator weights must be created by compileLearnedEvaluatorWeights",
    );
  }

  const { tokenIds } = extractLearnedEvaluatorTokens(position);
  const accumulator = new Float32Array(weights.accumulatorBias);

  for (const tokenId of tokenIds) {
    const embeddingOffset =
      tokenId * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let unit = 0;
      unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      unit += 1
    ) {
      accumulator[unit] = Math.fround(
        accumulator[unit] + weights.embedding[embeddingOffset + unit],
      );
    }
  }

  for (
    let unit = 0;
    unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    unit += 1
  ) {
    accumulator[unit] = relu(accumulator[unit]);
  }

  const hidden = new Float32Array(LEARNED_EVALUATOR_HIDDEN_SIZE);
  for (
    let hiddenUnit = 0;
    hiddenUnit < LEARNED_EVALUATOR_HIDDEN_SIZE;
    hiddenUnit += 1
  ) {
    let sum = weights.hiddenBias[hiddenUnit];
    const weightOffset =
      hiddenUnit * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let accumulatorUnit = 0;
      accumulatorUnit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      accumulatorUnit += 1
    ) {
      sum = floatMultiplyAdd(
        sum,
        accumulator[accumulatorUnit],
        weights.hiddenWeights[weightOffset + accumulatorUnit],
      );
    }
    hidden[hiddenUnit] = relu(sum);
  }

  let output = weights.outputBias;
  for (
    let hiddenUnit = 0;
    hiddenUnit < LEARNED_EVALUATOR_HIDDEN_SIZE;
    hiddenUnit += 1
  ) {
    output = floatMultiplyAdd(
      output,
      hidden[hiddenUnit],
      weights.outputWeights[hiddenUnit],
    );
  }
  output = Math.max(
    weights.residualMinimum ?? Number.NEGATIVE_INFINITY,
    Math.min(
      weights.residualMaximum ?? Number.POSITIVE_INFINITY,
      output,
    ),
  );
  if (weights.baseline === "combined") {
    output += evaluateHeuristic(
      {
        board: position.board,
        movesRemaining: position.movesRemaining,
        nextDisc: 1,
        score: 0,
        level: 1,
        movesPlayed: 0,
        gameOver: false,
      },
      "combined",
    );
  }
  return output;
}

export function extractLearnedPolicyTokens(
  position: LearnedPolicyPosition,
): LearnedPolicyTokenization {
  assertPolicyPosition(position);
  const base = extractLearnedEvaluatorTokens(position);
  const tokenIds = new Uint16Array(LEARNED_POLICY_ACTIVE_TOKEN_COUNT);
  tokenIds.set(base.tokenIds);
  tokenIds[tokenIds.length - 1] =
    NEXT_DISC_TOKEN_OFFSET + position.nextDisc - 1;
  return { tokenIds, mirrored: base.mirrored };
}

export function validateSerializedLearnedPolicyWeights(
  value: unknown,
): asserts value is SerializedLearnedPolicyWeights {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new TypeError("Learned policy weights must be an object");
  }
  const candidate = value as Record<string, unknown>;
  if (candidate.format !== LEARNED_POLICY_FORMAT) {
    throw new TypeError(`Learned policy format must be ${LEARNED_POLICY_FORMAT}`);
  }
  if (candidate.version !== LEARNED_POLICY_VERSION) {
    throw new TypeError(`Learned policy version must be ${LEARNED_POLICY_VERSION}`);
  }
  assertFloatArray(
    candidate.embedding,
    LEARNED_POLICY_TOKEN_COUNT * LEARNED_POLICY_ACCUMULATOR_SIZE,
    "policy embedding",
  );
  assertFloatArray(
    candidate.accumulatorBias,
    LEARNED_POLICY_ACCUMULATOR_SIZE,
    "policy accumulatorBias",
  );
  assertFloatArray(
    candidate.hiddenWeights,
    LEARNED_POLICY_HIDDEN_SIZE * LEARNED_POLICY_ACCUMULATOR_SIZE,
    "policy hiddenWeights",
  );
  assertFloatArray(
    candidate.hiddenBias,
    LEARNED_POLICY_HIDDEN_SIZE,
    "policy hiddenBias",
  );
  assertFloatArray(
    candidate.outputWeights,
    BOARD_SIZE * LEARNED_POLICY_HIDDEN_SIZE,
    "policy outputWeights",
  );
  assertFloatArray(
    candidate.outputBias,
    BOARD_SIZE,
    "policy outputBias",
  );
}

export function compileLearnedPolicyWeights(
  value: unknown,
): CompiledLearnedPolicyWeights {
  validateSerializedLearnedPolicyWeights(value);
  return Object.freeze({
    format: value.format,
    version: value.version,
    embedding: new Float32Array(value.embedding),
    accumulatorBias: new Float32Array(value.accumulatorBias),
    hiddenWeights: new Float32Array(value.hiddenWeights),
    hiddenBias: new Float32Array(value.hiddenBias),
    outputWeights: new Float32Array(value.outputWeights),
    outputBias: new Float32Array(value.outputBias),
    [COMPILED_POLICY_WEIGHTS]: true as const,
  });
}

export function evaluateLearnedPolicy(
  position: LearnedPolicyPosition,
  weights: CompiledLearnedPolicyWeights,
): LearnedPolicyEvaluation {
  if (weights?.[COMPILED_POLICY_WEIGHTS] !== true) {
    throw new TypeError(
      "Learned policy weights must be created by compileLearnedPolicyWeights",
    );
  }
  const { tokenIds, mirrored } = extractLearnedPolicyTokens(position);
  const accumulator = new Float32Array(weights.accumulatorBias);
  for (const tokenId of tokenIds) {
    const offset = tokenId * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < LEARNED_POLICY_ACCUMULATOR_SIZE; unit += 1) {
      accumulator[unit] = Math.fround(
        accumulator[unit] + weights.embedding[offset + unit],
      );
    }
  }
  for (let unit = 0; unit < accumulator.length; unit += 1) {
    accumulator[unit] = relu(accumulator[unit]);
  }

  const hidden = new Float32Array(LEARNED_POLICY_HIDDEN_SIZE);
  for (let hiddenUnit = 0; hiddenUnit < hidden.length; hiddenUnit += 1) {
    let sum = weights.hiddenBias[hiddenUnit];
    const offset = hiddenUnit * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < accumulator.length; unit += 1) {
      sum = floatMultiplyAdd(
        sum,
        accumulator[unit],
        weights.hiddenWeights[offset + unit],
      );
    }
    hidden[hiddenUnit] = relu(sum);
  }

  const canonicalLogits = new Float32Array(BOARD_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let sum = weights.outputBias[column];
    const offset = column * LEARNED_POLICY_HIDDEN_SIZE;
    for (let hiddenUnit = 0; hiddenUnit < hidden.length; hiddenUnit += 1) {
      sum = floatMultiplyAdd(
        sum,
        hidden[hiddenUnit],
        weights.outputWeights[offset + hiddenUnit],
      );
    }
    canonicalLogits[column] = sum;
  }

  const logits = new Float32Array(BOARD_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const canonicalColumn = mirrored ? BOARD_SIZE - 1 - column : column;
    logits[column] = canonicalLogits[canonicalColumn];
  }
  let bestColumn: number | null = null;
  let bestLogit = Number.NEGATIVE_INFINITY;
  for (const canonicalColumn of POLICY_COLUMN_ORDER) {
    const column = mirrored
      ? BOARD_SIZE - 1 - canonicalColumn
      : canonicalColumn;
    if (position.board[column] !== EMPTY) continue;
    const logit = canonicalLogits[canonicalColumn];
    if (logit > bestLogit) {
      bestLogit = logit;
      bestColumn = column;
    }
  }
  return { logits, bestColumn };
}

export function createZeroLearnedPolicyWeights(): SerializedLearnedPolicyWeights {
  return {
    format: LEARNED_POLICY_FORMAT,
    version: LEARNED_POLICY_VERSION,
    embedding: Array<number>(
      LEARNED_POLICY_TOKEN_COUNT * LEARNED_POLICY_ACCUMULATOR_SIZE,
    ).fill(0),
    accumulatorBias: Array<number>(LEARNED_POLICY_ACCUMULATOR_SIZE).fill(0),
    hiddenWeights: Array<number>(
      LEARNED_POLICY_HIDDEN_SIZE * LEARNED_POLICY_ACCUMULATOR_SIZE,
    ).fill(0),
    hiddenBias: Array<number>(LEARNED_POLICY_HIDDEN_SIZE).fill(0),
    outputWeights: Array<number>(
      BOARD_SIZE * LEARNED_POLICY_HIDDEN_SIZE,
    ).fill(0),
    outputBias: Array<number>(BOARD_SIZE).fill(0),
  };
}

/** A convenient valid artifact for smoke tests and incremental integration. */
export function createZeroLearnedEvaluatorWeights(): SerializedLearnedEvaluatorWeights {
  return {
    format: LEARNED_EVALUATOR_FORMAT,
    version: LEARNED_EVALUATOR_VERSION,
    embedding: Array<number>(EMBEDDING_WEIGHT_COUNT).fill(0),
    accumulatorBias: Array<number>(
      LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
    ).fill(0),
    hiddenWeights: Array<number>(HIDDEN_WEIGHT_COUNT).fill(0),
    hiddenBias: Array<number>(LEARNED_EVALUATOR_HIDDEN_SIZE).fill(0),
    outputWeights: Array<number>(LEARNED_EVALUATOR_HIDDEN_SIZE).fill(0),
    outputBias: 0,
  };
}

/** Deterministic fixture initializer; model training remains deliberately external. */
export function createRandomLearnedEvaluatorWeights(
  seed: number,
  scale = 0.05,
): SerializedLearnedEvaluatorWeights {
  if (!Number.isInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new RangeError("Learned evaluator seed must be a uint32 integer");
  }
  if (!Number.isFinite(scale) || scale < 0 || !Number.isFinite(Math.fround(scale))) {
    throw new RangeError(
      "Learned evaluator random scale must be a non-negative Float32",
    );
  }

  let randomState = seed >>> 0;
  const random = () => {
    randomState += 0x6d2b79f5;
    let value = randomState;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4_294_967_296;
  };
  const vector = (length: number) =>
    Array.from({ length }, () =>
      Math.fround((random() * 2 - 1) * scale),
    );

  return {
    format: LEARNED_EVALUATOR_FORMAT,
    version: LEARNED_EVALUATOR_VERSION,
    embedding: vector(EMBEDDING_WEIGHT_COUNT),
    accumulatorBias: vector(LEARNED_EVALUATOR_ACCUMULATOR_SIZE),
    hiddenWeights: vector(HIDDEN_WEIGHT_COUNT),
    hiddenBias: vector(LEARNED_EVALUATOR_HIDDEN_SIZE),
    outputWeights: vector(LEARNED_EVALUATOR_HIDDEN_SIZE),
    outputBias: vector(1)[0],
  };
}

function mirroredRepresentationIsSmaller(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const rowOffset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[rowOffset + column];
      const mirrored = board[rowOffset + BOARD_SIZE - 1 - column];
      if (mirrored < forward) return true;
      if (mirrored > forward) return false;
    }
  }
  return false;
}

function columnHeight(board: Board, column: number) {
  let height = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    if (board[row * BOARD_SIZE + column] !== EMPTY) height += 1;
  }
  return height;
}

function assertPosition(position: LearnedEvaluatorPosition) {
  if (typeof position !== "object" || position === null) {
    throw new TypeError("Learned evaluator position must be an object");
  }
  if (!Array.isArray(position.board) || position.board.length !== BOARD_SIZE ** 2) {
    throw new TypeError("Learned evaluator board must contain 49 cells");
  }
  for (let index = 0; index < position.board.length; index += 1) {
    const cell: unknown = position.board[index];
    if (!Number.isInteger(cell) || (cell as number) < EMPTY || (cell as number) > CRACKED) {
      throw new TypeError(
        `Learned evaluator board cell ${index} must be an integer from 0 through 9`,
      );
    }
  }
  if (
    !Number.isInteger(position.movesRemaining) ||
    position.movesRemaining < 0 ||
    position.movesRemaining > MOVES_PER_LEVEL
  ) {
    throw new RangeError(
      `Learned evaluator movesRemaining must be an integer from 0 through ${MOVES_PER_LEVEL}`,
    );
  }
}

function assertPolicyPosition(position: LearnedPolicyPosition) {
  assertPosition(position);
  if (
    !Number.isInteger(position.nextDisc) ||
    position.nextDisc < 1 ||
    position.nextDisc > BOARD_SIZE
  ) {
    throw new RangeError(
      `Learned policy nextDisc must be an integer from 1 through ${BOARD_SIZE}`,
    );
  }
}

function assertFloatArray(value: unknown, length: number, name: string) {
  if (!Array.isArray(value)) {
    throw new TypeError(`Learned evaluator ${name} must be a JSON array`);
  }
  if (value.length !== length) {
    throw new RangeError(
      `Learned evaluator ${name} must contain ${length} values`,
    );
  }
  for (let index = 0; index < value.length; index += 1) {
    assertFloat(value[index], `${name}[${index}]`);
  }
}

function assertFloat(value: unknown, name: string): asserts value is number {
  if (
    typeof value !== "number" ||
    !Number.isFinite(value) ||
    !Number.isFinite(Math.fround(value))
  ) {
    throw new TypeError(`Learned evaluator ${name} must be a finite Float32`);
  }
}

function relu(value: number) {
  return value > 0 ? value : 0;
}

function floatMultiplyAdd(sum: number, left: number, right: number) {
  return Math.fround(sum + Math.fround(left * right));
}
