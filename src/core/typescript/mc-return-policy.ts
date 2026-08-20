import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  contiguousLineLength,
  placeDisc,
  playMove,
  type Board,
  type GameState,
} from "./engine.ts";
import { extractHeuristicFeatures } from "./heuristic.ts";

export const MC_RETURN_FEATURE_SIZE = 170;
export const MC_RETURN_FORMAT = "drop7-monte-carlo-return" as const;
export const MC_RETURN_VERSION = 1 as const;

export interface McReturnNetworkSnapshot {
  inputSize: number;
  hiddenOne: number;
  hiddenTwo: number;
  weightsOne: number[];
  biasesOne: number[];
  weightsTwo: number[];
  biasesTwo: number[];
  weightsThree: number[];
  biasThree: number;
}

export interface McReturnTrainingSample {
  input: ArrayLike<number>;
  target: number;
}

export interface McReturnPolicyOptions {
  samples?: number;
  policySeed?: number;
}

export interface McReturnArtifact {
  format: typeof MC_RETURN_FORMAT;
  version: typeof MC_RETURN_VERSION;
  algorithm: "undiscounted-monte-carlo-return";
  observableOnly: true;
  options: {
    samples: number;
    policySeed: number;
  };
  network: McReturnNetworkSnapshot;
}

export interface McReturnValueNetwork {
  value(input: ArrayLike<number>): number;
}

const BOARD_CELLS = BOARD_SIZE * BOARD_SIZE;
const PROBE_REVEAL_DOMAIN = 0x4451_5256;
const DEFAULT_POLICY_SEED = 0x6d63_7274;
const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;

/**
 * The exact observable action encoding used by the reference compact DQN.
 * Keeping this compatible lets the Monte-Carlo laboratory warm-start from a
 * DQN checkpoint while replacing TD targets with complete-episode returns.
 */
export function encodeMcReturnAction(
  state: Readonly<GameState>,
  column: number,
  samples = 2,
  policySeed = DEFAULT_POLICY_SEED,
) {
  validateInferenceState(state);
  if (!Number.isSafeInteger(column) || column < 0 || column >= BOARD_SIZE) {
    throw new Error("column must be an integer from 0 to 6");
  }
  if (state.board[column] !== EMPTY) {
    throw new Error(`column ${column} is full`);
  }
  if (!Number.isSafeInteger(samples) || samples < 1 || samples > 16) {
    throw new Error("samples must be an integer from 1 to 16");
  }
  validateUint32(policySeed, "policySeed");
  const observable = canonicalObservable(state);
  const canonicalColumn = observable.mirrored
    ? BOARD_SIZE - 1 - column
    : column;
  const board = observable.mirrored ? mirrorBoard(state.board) : state.board;
  const values: number[] = [];

  for (const cell of board) {
    values.push(cell >= 1 && cell <= 7 ? cell / 7 : 0);
  }
  for (const cell of board) {
    values.push(cell === SOLID ? 1 : cell === CRACKED ? 0.5 : 0);
  }
  for (let disc = 1; disc <= BOARD_SIZE; disc += 1) {
    values.push(state.nextDisc === disc ? 1 : 0);
  }
  values.push(state.movesRemaining / MOVES_PER_LEVEL);
  values.push(Math.min(state.level, 100) / 100);
  values.push(Math.min(state.movesPlayed, 500) / 500);
  appendHeuristic(values, state);
  const heights = columnHeights(board);
  for (const height of heights) values.push(height / BOARD_SIZE);

  for (let candidate = 0; candidate < BOARD_SIZE; candidate += 1) {
    values.push(candidate === canonicalColumn ? 1 : 0);
  }
  const actualHeights = columnHeights(state.board);
  const landingHeight = actualHeights[column] + 1;
  const leftHeight = column === 0
    ? landingHeight
    : actualHeights[column - 1];
  const rightHeight = column === BOARD_SIZE - 1
    ? landingHeight
    : actualHeights[column + 1];
  const canonicalLeftHeight = observable.mirrored ? rightHeight : leftHeight;
  const canonicalRightHeight = observable.mirrored ? leftHeight : rightHeight;
  let coveredInColumn = 0;
  let lowInColumn = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const cell = state.board[row * BOARD_SIZE + column];
    if (cell === SOLID || cell === CRACKED) coveredInColumn += 1;
    if (cell === 1 || cell === 2) lowInColumn += 1;
  }
  const placed = placeDisc(state.board, column, state.nextDisc);
  const landingRow = BOARD_SIZE - landingHeight;
  const horizontalLength = placed
    ? contiguousLineLength(placed, landingRow, column, "row")
    : 0;
  values.push(landingHeight / BOARD_SIZE);
  values.push((landingHeight / BOARD_SIZE) ** 2);
  values.push(Math.abs(canonicalColumn - 3) / 3);
  values.push((landingHeight - canonicalLeftHeight) / BOARD_SIZE);
  values.push((landingHeight - canonicalRightHeight) / BOARD_SIZE);
  values.push(coveredInColumn / BOARD_SIZE);
  values.push(lowInColumn / BOARD_SIZE);
  values.push((state.nextDisc - landingHeight) / BOARD_SIZE);
  values.push((state.nextDisc - horizontalLength) / BOARD_SIZE);
  values.push(
    ...expectedCandidateFeatures(
      state,
      column,
      samples,
      policySeed,
      observable.hash,
    ),
  );

  if (values.length !== MC_RETURN_FEATURE_SIZE) {
    throw new Error(
      `MC-return feature drift: expected ${MC_RETURN_FEATURE_SIZE}, got ${values.length}`,
    );
  }
  return Float64Array.from(values);
}

export function chooseMcReturnMove(
  state: Readonly<GameState>,
  network: McReturnValueNetwork,
  options: McReturnPolicyOptions & {
    epsilon?: number;
    random?: () => number;
  } = {},
) {
  if (state.gameOver) return null;
  validateInferenceState(state);
  const samples = options.samples ?? 2;
  const policySeed = options.policySeed ?? DEFAULT_POLICY_SEED;
  const epsilon = options.epsilon ?? 0;
  if (!Number.isFinite(epsilon) || epsilon < 0 || epsilon > 1) {
    throw new Error("epsilon must be from 0 to 1");
  }
  const observable = canonicalObservable(state);
  const legal = columnOrder(observable.mirrored).filter(
    (column) => state.board[column] === EMPTY,
  );
  if (legal.length === 0) return null;
  if (epsilon > 0) {
    if (!options.random) throw new Error("epsilon exploration needs random");
    if (options.random() < epsilon) {
      return legal[Math.floor(options.random() * legal.length)];
    }
  }
  let bestColumn = legal[0];
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of legal) {
    const value = network.value(
      encodeMcReturnAction(state, column, samples, policySeed),
    );
    if (!Number.isFinite(value)) {
      throw new Error("MC-return network produced a non-finite value");
    }
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

export class TrainableMcReturnNetwork implements McReturnValueNetwork {
  readonly inputSize: number;
  readonly hiddenOne: number;
  readonly hiddenTwo: number;
  readonly weightsOne: Float64Array;
  readonly biasesOne: Float64Array;
  readonly weightsTwo: Float64Array;
  readonly biasesTwo: Float64Array;
  readonly weightsThree: Float64Array;
  biasThree: number;

  private readonly firstMoment: Float64Array[];
  private readonly secondMoment: Float64Array[];
  private firstMomentBiasThree = 0;
  private secondMomentBiasThree = 0;
  private adamStep = 0;

  constructor(snapshot: McReturnNetworkSnapshot) {
    validateNetworkSnapshot(snapshot);
    this.inputSize = snapshot.inputSize;
    this.hiddenOne = snapshot.hiddenOne;
    this.hiddenTwo = snapshot.hiddenTwo;
    this.weightsOne = Float64Array.from(snapshot.weightsOne);
    this.biasesOne = Float64Array.from(snapshot.biasesOne);
    this.weightsTwo = Float64Array.from(snapshot.weightsTwo);
    this.biasesTwo = Float64Array.from(snapshot.biasesTwo);
    this.weightsThree = Float64Array.from(snapshot.weightsThree);
    this.biasThree = snapshot.biasThree;
    this.firstMoment = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
    this.secondMoment = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
  }

  value(input: ArrayLike<number>) {
    return this.forward(input).value;
  }

  trainBatch(
    samples: readonly McReturnTrainingSample[],
    learningRate: number,
  ) {
    if (samples.length === 0) throw new Error("training batch is empty");
    if (!Number.isFinite(learningRate) || learningRate <= 0) {
      throw new Error("learningRate must be positive and finite");
    }
    const gradients = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
    let gradientBiasThree = 0;
    let loss = 0;

    for (const sample of samples) {
      if (!Number.isFinite(sample.target)) {
        throw new Error("MC-return target must be finite");
      }
      const pass = this.forward(sample.input);
      const error = pass.value - sample.target;
      loss += Math.abs(error) <= 1
        ? 0.5 * error * error
        : Math.abs(error) - 0.5;
      const outputGradient = Math.max(-1, Math.min(1, error));
      gradientBiasThree += outputGradient;
      const [
        gradientOne,
        gradientBiasOne,
        gradientTwo,
        gradientBiasTwo,
        gradientThree,
      ] = gradients;
      const hiddenTwoGradient = new Float64Array(this.hiddenTwo);
      for (let index = 0; index < this.hiddenTwo; index += 1) {
        gradientThree[index] += outputGradient * pass.hiddenTwo[index];
        hiddenTwoGradient[index] = pass.hiddenTwo[index] > 0
          ? outputGradient * this.weightsThree[index]
          : 0;
      }
      const hiddenOneGradient = new Float64Array(this.hiddenOne);
      for (let output = 0; output < this.hiddenTwo; output += 1) {
        const gradient = hiddenTwoGradient[output];
        gradientBiasTwo[output] += gradient;
        const offset = output * this.hiddenOne;
        for (let input = 0; input < this.hiddenOne; input += 1) {
          gradientTwo[offset + input] +=
            gradient * pass.hiddenOne[input];
          hiddenOneGradient[input] +=
            gradient * this.weightsTwo[offset + input];
        }
      }
      for (let output = 0; output < this.hiddenOne; output += 1) {
        const gradient = pass.hiddenOne[output] > 0
          ? hiddenOneGradient[output]
          : 0;
        gradientBiasOne[output] += gradient;
        const offset = output * this.inputSize;
        for (let input = 0; input < this.inputSize; input += 1) {
          gradientOne[offset + input] += gradient * sample.input[input];
        }
      }
    }

    const batchScale = 1 / samples.length;
    let normSquared = (gradientBiasThree * batchScale) ** 2;
    for (const gradient of gradients) {
      for (let index = 0; index < gradient.length; index += 1) {
        gradient[index] *= batchScale;
        normSquared += gradient[index] ** 2;
      }
    }
    gradientBiasThree *= batchScale;
    const clipScale = Math.min(
      1,
      5 / Math.max(1e-12, Math.sqrt(normSquared)),
    );
    this.adamStep += 1;
    const correctionOne = 1 - 0.9 ** this.adamStep;
    const correctionTwo = 1 - 0.999 ** this.adamStep;
    const parameters = this.parameterArrays();
    for (let parameterIndex = 0;
      parameterIndex < parameters.length;
      parameterIndex += 1) {
      const parameter = parameters[parameterIndex];
      const gradient = gradients[parameterIndex];
      const firstMoment = this.firstMoment[parameterIndex];
      const secondMoment = this.secondMoment[parameterIndex];
      for (let index = 0; index < parameter.length; index += 1) {
        const clipped = gradient[index] * clipScale;
        firstMoment[index] = 0.9 * firstMoment[index] + 0.1 * clipped;
        secondMoment[index] =
          0.999 * secondMoment[index] + 0.001 * clipped * clipped;
        parameter[index] -=
          learningRate *
          (firstMoment[index] / correctionOne) /
          (Math.sqrt(secondMoment[index] / correctionTwo) + 1e-8);
      }
    }
    const clippedBias = gradientBiasThree * clipScale;
    this.firstMomentBiasThree =
      0.9 * this.firstMomentBiasThree + 0.1 * clippedBias;
    this.secondMomentBiasThree =
      0.999 * this.secondMomentBiasThree + 0.001 * clippedBias * clippedBias;
    this.biasThree -=
      learningRate *
      (this.firstMomentBiasThree / correctionOne) /
      (Math.sqrt(this.secondMomentBiasThree / correctionTwo) + 1e-8);
    return loss / samples.length;
  }

  snapshot(): McReturnNetworkSnapshot {
    return {
      inputSize: this.inputSize,
      hiddenOne: this.hiddenOne,
      hiddenTwo: this.hiddenTwo,
      weightsOne: [...this.weightsOne],
      biasesOne: [...this.biasesOne],
      weightsTwo: [...this.weightsTwo],
      biasesTwo: [...this.biasesTwo],
      weightsThree: [...this.weightsThree],
      biasThree: this.biasThree,
    };
  }

  byteLength() {
    return (
      this.parameterArrays().reduce(
        (total, array) => total + array.byteLength,
        0,
      ) +
      this.firstMoment.reduce(
        (total, array) => total + array.byteLength,
        0,
      ) +
      this.secondMoment.reduce(
        (total, array) => total + array.byteLength,
        0,
      ) +
      3 * Float64Array.BYTES_PER_ELEMENT
    );
  }

  private forward(input: ArrayLike<number>) {
    if (input.length !== this.inputSize) {
      throw new Error(`expected ${this.inputSize} Q inputs, got ${input.length}`);
    }
    const hiddenOne = new Float64Array(this.hiddenOne);
    for (let output = 0; output < this.hiddenOne; output += 1) {
      let sum = this.biasesOne[output];
      const offset = output * this.inputSize;
      for (let inputIndex = 0; inputIndex < this.inputSize; inputIndex += 1) {
        sum += this.weightsOne[offset + inputIndex] * input[inputIndex];
      }
      hiddenOne[output] = Math.max(0, sum);
    }
    const hiddenTwo = new Float64Array(this.hiddenTwo);
    for (let output = 0; output < this.hiddenTwo; output += 1) {
      let sum = this.biasesTwo[output];
      const offset = output * this.hiddenOne;
      for (let input = 0; input < this.hiddenOne; input += 1) {
        sum += this.weightsTwo[offset + input] * hiddenOne[input];
      }
      hiddenTwo[output] = Math.max(0, sum);
    }
    let value = this.biasThree;
    for (let index = 0; index < this.hiddenTwo; index += 1) {
      value += this.weightsThree[index] * hiddenTwo[index];
    }
    return { hiddenOne, hiddenTwo, value };
  }

  private parameterArrays() {
    return [
      this.weightsOne,
      this.biasesOne,
      this.weightsTwo,
      this.biasesTwo,
      this.weightsThree,
    ];
  }
}

export class CompiledMcReturnPolicy {
  private readonly network: TrainableMcReturnNetwork;
  readonly samples: number;
  readonly policySeed: number;

  constructor(artifact: McReturnArtifact) {
    if (
      artifact.format !== MC_RETURN_FORMAT ||
      artifact.version !== MC_RETURN_VERSION ||
      artifact.algorithm !== "undiscounted-monte-carlo-return" ||
      artifact.observableOnly !== true
    ) {
      throw new Error("unsupported MC-return artifact");
    }
    if (!Number.isSafeInteger(artifact.options.samples) ||
      artifact.options.samples < 1 || artifact.options.samples > 16) {
      throw new Error("artifact samples must be from 1 to 16");
    }
    validateUint32(artifact.options.policySeed, "artifact policySeed");
    this.network = new TrainableMcReturnNetwork(artifact.network);
    this.samples = artifact.options.samples;
    this.policySeed = artifact.options.policySeed;
  }

  chooseMove(state: Readonly<GameState>) {
    return chooseMcReturnMove(state, this.network, {
      samples: this.samples,
      policySeed: this.policySeed,
    });
  }

  evaluateActions(state: Readonly<GameState>) {
    if (state.gameOver) return [];
    const observable = canonicalObservable(state);
    return columnOrder(observable.mirrored)
      .filter((column) => state.board[column] === EMPTY)
      .map((column) => ({
        column,
        value: this.network.value(
          encodeMcReturnAction(
            state,
            column,
            this.samples,
            this.policySeed,
          ),
        ),
      }));
  }
}

function expectedCandidateFeatures(
  state: Readonly<GameState>,
  column: number,
  samples: number,
  policySeed: number,
  observableHash: number,
) {
  const result = Array<number>(25).fill(0);
  for (let sample = 0; sample < samples; sample += 1) {
    const reveal = stratifiedProbe(
      observableHash,
      policySeed,
      sample,
      samples,
    );
    const move = playMove(state, column, () => reveal, {
      captureAnimation: false,
    });
    if (!move) continue;
    let cleared = 0;
    let revealed = 0;
    for (const wave of move.waves) {
      cleared += wave.cleared;
      revealed += wave.revealed;
    }
    const scale = 1 / samples;
    result[0] += Math.min(move.scoreDelta, 100_000) / 100_000 * scale;
    result[1] += cleared / BOARD_CELLS * scale;
    result[2] += revealed / 14 * scale;
    result[3] += Math.min(move.waves.length, 10) / 10 * scale;
    result[4] += (move.state.gameOver ? 1 : 0) * scale;
    result[5] += (move.clearedBoard ? 1 : 0) * scale;
    result[6] += (move.levelAdvanced ? 1 : 0) * scale;
    const heuristic: number[] = [];
    appendHeuristic(heuristic, move.state);
    for (let index = 0; index < heuristic.length; index += 1) {
      result[7 + index] += heuristic[index] * scale;
    }
    const heights = columnHeights(move.state.board);
    result[21] += Math.max(...heights) / BOARD_SIZE * scale;
    result[22] += mean(heights) / BOARD_SIZE * scale;
    result[23] +=
      roughness(heights) / (BOARD_SIZE * (BOARD_SIZE - 1)) * scale;
    result[24] +=
      heights.filter((height) => height < BOARD_SIZE).length /
      BOARD_SIZE * scale;
  }
  return result;
}

function appendHeuristic(target: number[], state: Readonly<GameState>) {
  const feature = extractHeuristicFeatures(state);
  target.push(
    feature.openColumns / 7,
    feature.heightLoad / 1_400,
    feature.solidCells / 49,
    feature.crackedCells / 49,
    feature.numberedCells / 49,
    feature.highLowNumbers / 20,
    feature.legacyNearMatches / 49,
    feature.directPotential / 49,
    feature.latentChainPotential / 49,
    feature.crackedExposure / 49,
    feature.solidExposure / 49,
    feature.adjacentOnes / 42,
    feature.tripleTwos / 14,
    feature.deadLowNumbers / 20,
  );
}

function canonicalObservable(state: Readonly<GameState>) {
  const mirrored = mirroredBoardIsSmaller(state.board);
  let hash = 0x811c_9dc5;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const sourceColumn = mirrored ? BOARD_SIZE - 1 - column : column;
      hash ^= state.board[row * BOARD_SIZE + sourceColumn] + 1;
      hash = Math.imul(hash, 0x0100_0193);
    }
  }
  hash ^= state.nextDisc;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.movesRemaining;
  return { hash: mix32(hash), mirrored };
}

function stratifiedProbe(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
) {
  const offset =
    mix32(observableHash ^ policySeed ^ PROBE_REVEAL_DOMAIN) % BOARD_SIZE;
  const stratum = Math.floor(((sample + 0.5) * BOARD_SIZE) / samples);
  const disc = ((offset + stratum) % BOARD_SIZE) + 1;
  return (disc - 0.5) / BOARD_SIZE;
}

function columnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

function roughness(heights: readonly number[]) {
  let result = 0;
  for (let column = 1; column < heights.length; column += 1) {
    result += Math.abs(heights[column] - heights[column - 1]);
  }
  return result;
}

function mirroredBoardIsSmaller(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (mirrored < forward) return true;
      if (mirrored > forward) return false;
    }
  }
  return false;
}

function mirrorBoard(board: Board): Board {
  const result = board.slice();
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      result[row * BOARD_SIZE + column] =
        board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
    }
  }
  return result;
}

function columnOrder(mirrored: boolean) {
  return mirrored ? MIRRORED_COLUMN_ORDER : COLUMN_ORDER;
}

function validateNetworkSnapshot(snapshot: McReturnNetworkSnapshot) {
  for (const [label, value] of [
    ["inputSize", snapshot.inputSize],
    ["hiddenOne", snapshot.hiddenOne],
    ["hiddenTwo", snapshot.hiddenTwo],
  ] as const) {
    if (!Number.isSafeInteger(value) || value < 1 || value > 4_096) {
      throw new Error(`${label} must be a bounded positive integer`);
    }
  }
  if (snapshot.inputSize !== MC_RETURN_FEATURE_SIZE) {
    throw new Error(`inputSize must be ${MC_RETURN_FEATURE_SIZE}`);
  }
  validateArray(
    snapshot.weightsOne,
    snapshot.inputSize * snapshot.hiddenOne,
    "weightsOne",
  );
  validateArray(snapshot.biasesOne, snapshot.hiddenOne, "biasesOne");
  validateArray(
    snapshot.weightsTwo,
    snapshot.hiddenOne * snapshot.hiddenTwo,
    "weightsTwo",
  );
  validateArray(snapshot.biasesTwo, snapshot.hiddenTwo, "biasesTwo");
  validateArray(snapshot.weightsThree, snapshot.hiddenTwo, "weightsThree");
  if (!Number.isFinite(snapshot.biasThree)) {
    throw new Error("biasThree must be finite");
  }
}

function validateArray(values: readonly number[], length: number, label: string) {
  if (
    values.length !== length ||
    values.some((value) => !Number.isFinite(value) || Math.abs(value) > 1e6)
  ) {
    throw new Error(`${label} must contain ${length} bounded finite values`);
  }
}

function validateInferenceState(state: Readonly<GameState>) {
  if (
    state.board.length !== BOARD_CELLS ||
    state.board.some(
      (cell) => !Number.isInteger(cell) || cell < EMPTY || cell > CRACKED,
    )
  ) {
    throw new Error("MC-return inference requires a valid 7 x 7 board");
  }
  if (
    !Number.isInteger(state.nextDisc) ||
    state.nextDisc < 1 ||
    state.nextDisc > BOARD_SIZE
  ) {
    throw new Error("MC-return inference requires a visible next disc");
  }
}

function validateUint32(value: number, label: string) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error(`${label} must be a uint32`);
  }
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}
