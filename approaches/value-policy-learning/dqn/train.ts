import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  contiguousLineLength,
  createInitialBoard,
  placeDisc,
  playMove,
  seededRandom,
  type Board,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { extractHeuristicFeatures } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  initialFairPolicyWeights,
  runFairPolicyGame,
} from "../../fair-expectimax/fair-policy/tune.ts";

/**
 * Standalone, seed-blind Double-DQN laboratory for Drop7.
 *
 * Environment seeds are used only after chooseAction() returns. The network
 * sees the public board, public phase, upcoming disc, and deterministic
 * candidate probes derived from that observable position. It never receives a
 * game seed, move RNG state, or future disc/reveal value.
 */

const FORMAT = "drop7-observable-double-dqn" as const;
const VERSION = 1 as const;
const TRAINING_SEED_START = 0x2d70_0000;
const VALIDATION_SEED_START = 0x7d70_0000;
const RESERVED_FINAL_SEED_START = 0xd700_0000;
const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const PROBE_REVEAL_DOMAIN = 0x4451_5256;
const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;

const DEFAULT_TRAINING_GAMES = 512;
const DEFAULT_TRAINING_STEPS = 40_000;
const DEFAULT_VALIDATION_GAMES = 64;
const DEFAULT_CURVE_GAMES = 16;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_REPLAY_CAPACITY = 24_000;
const DEFAULT_WARMUP = 1_500;
const DEFAULT_BATCH_SIZE = 24;
const DEFAULT_TRAIN_EVERY = 4;
const DEFAULT_TARGET_EVERY = 500;
const DEFAULT_EVALUATE_EVERY = 8_000;
const DEFAULT_HIDDEN_ONE = 32;
const DEFAULT_HIDDEN_TWO = 16;
const DEFAULT_LEARNING_RATE = 0.0007;
const DEFAULT_GAMMA = 0.99;
const DEFAULT_EPSILON_START = 0.9;
const DEFAULT_EPSILON_END = 0.04;
const DEFAULT_EPSILON_FRACTION = 0.8;
const DEFAULT_POLICY_SAMPLES = 2;
const DEFAULT_TRAINER_SEED = 0xd0b1_e202;
const DEFAULT_POLICY_SEED = 0xd0b1_d707;
const DEFAULT_OUTPUT = "drop7-dqn.json";

const BOARD_CELLS = BOARD_SIZE * BOARD_SIZE;
export const DQN_FEATURE_SIZE = 170;
const FEATURE_SIZE = DQN_FEATURE_SIZE;
const TERMINAL_PENALTY = 12;
const CHECKPOINT_MINIMUM_MOVE_DELTA = 2;
const CHECKPOINT_MINIMUM_SCORE_FRACTION = 0.05;

interface Arguments {
  trainingGames: number;
  trainingSteps: number;
  validationGames: number;
  curveGames: number;
  maxMoves: number;
  replayCapacity: number;
  warmup: number;
  batchSize: number;
  trainEvery: number;
  targetEvery: number;
  evaluateEvery: number;
  hiddenOne: number;
  hiddenTwo: number;
  learningRate: number;
  gamma: number;
  epsilonStart: number;
  epsilonEnd: number;
  epsilonFraction: number;
  policySamples: number;
  trainerSeed: number;
  policySeed: number;
  outputPath: string;
  selfTest: boolean;
}

export interface CompactState {
  board: Board;
  nextDisc: 1 | 2 | 3 | 4 | 5 | 6 | 7;
  level: number;
  movesRemaining: number;
  movesPlayed: number;
  gameOver: boolean;
}

interface Experience {
  state: CompactState;
  action: number;
  reward: number;
  nextState: CompactState;
  done: boolean;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  clears: number;
  maxChain: number;
  reward: number;
}

interface Summary {
  games: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanClears: number;
  meanMaxChain: number;
  meanReward: number;
  results: readonly GameResult[];
}

interface CurvePoint {
  step: number;
  episodes: number;
  epsilon: number;
  meanTdLoss: number;
  replaySize: number;
  probe: Omit<Summary, "results">;
}

interface PairedValidation {
  baseline: Omit<Summary, "results" | "meanReward">;
  candidate: Omit<Summary, "results" | "meanReward">;
  pairedMeanScoreDelta: number;
  pairedMedianScoreDelta: number;
  pairedMeanMoveDelta: number;
  wins: number;
  ties: number;
  losses: number;
}

export interface NetworkSnapshot {
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

export interface DqnArtifact {
  format: typeof FORMAT;
  version: typeof VERSION;
  algorithm: "double-dqn";
  observableOnly: true;
  trainingSeedStart: number;
  validationSeedStart: number;
  reservedFinalSeedStart: number;
  options: Omit<Arguments, "outputPath" | "selfTest">;
  reward: {
    legalMove: number;
    terminalPenalty: number;
    scoreScale: number;
    revealScale: number;
    clearScale: number;
    chainScale: number;
  };
  network: NetworkSnapshot;
  curves: readonly CurvePoint[];
  validation: PairedValidation;
}

interface ForwardPass {
  hiddenOne: Float64Array;
  hiddenTwo: Float64Array;
  value: number;
}

export interface TrainingSample {
  input: Float64Array;
  target: number;
}

export interface QValueNetwork {
  value(input: Float64Array): number;
}

export interface CompiledDqnPolicy {
  readonly policySamples: number;
  readonly policySeed: number;
  readonly cacheEntries: number;
  chooseMove(state: Readonly<GameState>): number | null;
  evaluateActions(
    state: Readonly<GameState>,
  ): readonly { column: number; value: number }[];
  evaluateState(state: Readonly<GameState>): number;
  clearCache(): void;
}

export interface CompileDqnPolicyOptions {
  /** Bounded observable-state action cache; defaults to 8,192 entries. */
  cacheEntries?: number;
}

export class DenseQNetwork implements QValueNetwork {
  readonly inputSize: number;
  readonly hiddenOne: number;
  readonly hiddenTwo: number;
  readonly weightsOne: Float64Array;
  readonly biasesOne: Float64Array;
  readonly weightsTwo: Float64Array;
  readonly biasesTwo: Float64Array;
  readonly weightsThree: Float64Array;
  biasThree = 0;

  private readonly firstMoment: Float64Array[];
  private readonly secondMoment: Float64Array[];
  private firstMomentBiasThree = 0;
  private secondMomentBiasThree = 0;
  private adamStep = 0;

  constructor(
    inputSize: number,
    hiddenOne: number,
    hiddenTwo: number,
    random: () => number,
  ) {
    this.inputSize = inputSize;
    this.hiddenOne = hiddenOne;
    this.hiddenTwo = hiddenTwo;
    this.weightsOne = new Float64Array(inputSize * hiddenOne);
    this.biasesOne = new Float64Array(hiddenOne);
    this.weightsTwo = new Float64Array(hiddenOne * hiddenTwo);
    this.biasesTwo = new Float64Array(hiddenTwo);
    this.weightsThree = new Float64Array(hiddenTwo);
    initializeWeights(this.weightsOne, inputSize, random);
    initializeWeights(this.weightsTwo, hiddenOne, random);
    initializeWeights(this.weightsThree, hiddenTwo, random);
    this.firstMoment = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
    this.secondMoment = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
  }

  value(input: Float64Array) {
    return this.forward(input).value;
  }

  forward(input: Float64Array): ForwardPass {
    if (input.length !== this.inputSize) {
      throw new Error(`Expected ${this.inputSize} Q inputs, got ${input.length}`);
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
      for (let inputIndex = 0; inputIndex < this.hiddenOne; inputIndex += 1) {
        sum += this.weightsTwo[offset + inputIndex] * hiddenOne[inputIndex];
      }
      hiddenTwo[output] = Math.max(0, sum);
    }

    let value = this.biasThree;
    for (let index = 0; index < this.hiddenTwo; index += 1) {
      value += this.weightsThree[index] * hiddenTwo[index];
    }
    return { hiddenOne, hiddenTwo, value };
  }

  trainBatch(samples: readonly TrainingSample[], learningRate: number) {
    const gradients = this.parameterArrays().map(
      (parameter) => new Float64Array(parameter.length),
    );
    let gradientBiasThree = 0;
    let loss = 0;

    for (const sample of samples) {
      const pass = this.forward(sample.input);
      const error = pass.value - sample.target;
      loss += Math.abs(error) <= 1 ? 0.5 * error * error : Math.abs(error) - 0.5;
      const outputGradient = Math.max(-1, Math.min(1, error));
      gradientBiasThree += outputGradient;

      const [gradientOne, gradientBiasOne, gradientTwo, gradientBiasTwo, gradientThree] =
        gradients;
      const hiddenTwoGradient = new Float64Array(this.hiddenTwo);
      for (let index = 0; index < this.hiddenTwo; index += 1) {
        gradientThree[index] += outputGradient * pass.hiddenTwo[index];
        hiddenTwoGradient[index] =
          pass.hiddenTwo[index] > 0
            ? outputGradient * this.weightsThree[index]
            : 0;
      }

      const hiddenOneGradient = new Float64Array(this.hiddenOne);
      for (let output = 0; output < this.hiddenTwo; output += 1) {
        const gradient = hiddenTwoGradient[output];
        gradientBiasTwo[output] += gradient;
        const offset = output * this.hiddenOne;
        for (let inputIndex = 0; inputIndex < this.hiddenOne; inputIndex += 1) {
          gradientTwo[offset + inputIndex] +=
            gradient * pass.hiddenOne[inputIndex];
          hiddenOneGradient[inputIndex] +=
            gradient * this.weightsTwo[offset + inputIndex];
        }
      }

      for (let output = 0; output < this.hiddenOne; output += 1) {
        const gradient =
          pass.hiddenOne[output] > 0 ? hiddenOneGradient[output] : 0;
        gradientBiasOne[output] += gradient;
        const offset = output * this.inputSize;
        for (let inputIndex = 0; inputIndex < this.inputSize; inputIndex += 1) {
          gradientOne[offset + inputIndex] += gradient * sample.input[inputIndex];
        }
      }
    }

    const scale = 1 / samples.length;
    let normSquared = (gradientBiasThree * scale) ** 2;
    for (const gradient of gradients) {
      for (let index = 0; index < gradient.length; index += 1) {
        gradient[index] *= scale;
        normSquared += gradient[index] ** 2;
      }
    }
    gradientBiasThree *= scale;
    const clipScale = Math.min(1, 5 / Math.max(1e-12, Math.sqrt(normSquared)));

    this.adamStep += 1;
    const biasCorrectionOne = 1 - 0.9 ** this.adamStep;
    const biasCorrectionTwo = 1 - 0.999 ** this.adamStep;
    const parameters = this.parameterArrays();
    for (let parameterIndex = 0; parameterIndex < parameters.length; parameterIndex += 1) {
      const parameter = parameters[parameterIndex];
      const gradient = gradients[parameterIndex];
      const firstMoment = this.firstMoment[parameterIndex];
      const secondMoment = this.secondMoment[parameterIndex];
      for (let index = 0; index < parameter.length; index += 1) {
        const clipped = gradient[index] * clipScale;
        firstMoment[index] = 0.9 * firstMoment[index] + 0.1 * clipped;
        secondMoment[index] =
          0.999 * secondMoment[index] + 0.001 * clipped * clipped;
        const adjusted =
          (firstMoment[index] / biasCorrectionOne) /
          (Math.sqrt(secondMoment[index] / biasCorrectionTwo) + 1e-8);
        parameter[index] -= learningRate * adjusted;
      }
    }

    const clippedBias = gradientBiasThree * clipScale;
    this.firstMomentBiasThree =
      0.9 * this.firstMomentBiasThree + 0.1 * clippedBias;
    this.secondMomentBiasThree =
      0.999 * this.secondMomentBiasThree + 0.001 * clippedBias * clippedBias;
    this.biasThree -=
      learningRate *
      (this.firstMomentBiasThree / biasCorrectionOne) /
      (Math.sqrt(this.secondMomentBiasThree / biasCorrectionTwo) + 1e-8);
    return loss / samples.length;
  }

  copyFrom(source: DenseQNetwork) {
    if (
      this.inputSize !== source.inputSize ||
      this.hiddenOne !== source.hiddenOne ||
      this.hiddenTwo !== source.hiddenTwo
    ) {
      throw new Error("Cannot copy incompatible Q networks");
    }
    for (const [target, origin] of zip(this.parameterArrays(), source.parameterArrays())) {
      target.set(origin);
    }
    this.biasThree = source.biasThree;
  }

  snapshot(): NetworkSnapshot {
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

  restore(snapshot: NetworkSnapshot) {
    if (
      snapshot.inputSize !== this.inputSize ||
      snapshot.hiddenOne !== this.hiddenOne ||
      snapshot.hiddenTwo !== this.hiddenTwo
    ) {
      throw new Error("Checkpoint network dimensions do not match");
    }
    this.weightsOne.set(snapshot.weightsOne);
    this.biasesOne.set(snapshot.biasesOne);
    this.weightsTwo.set(snapshot.weightsTwo);
    this.biasesTwo.set(snapshot.biasesTwo);
    this.weightsThree.set(snapshot.weightsThree);
    this.biasThree = snapshot.biasThree;
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

/** Inference-only network: no optimizer tensors and no per-forward allocations. */
class InferenceQNetwork implements QValueNetwork {
  private readonly snapshot: NetworkSnapshot;
  private readonly weightsOne: Float64Array;
  private readonly biasesOne: Float64Array;
  private readonly weightsTwo: Float64Array;
  private readonly biasesTwo: Float64Array;
  private readonly weightsThree: Float64Array;
  private readonly hiddenOne: Float64Array;
  private readonly hiddenTwo: Float64Array;

  constructor(snapshot: NetworkSnapshot) {
    this.snapshot = snapshot;
    this.weightsOne = Float64Array.from(snapshot.weightsOne);
    this.biasesOne = Float64Array.from(snapshot.biasesOne);
    this.weightsTwo = Float64Array.from(snapshot.weightsTwo);
    this.biasesTwo = Float64Array.from(snapshot.biasesTwo);
    this.weightsThree = Float64Array.from(snapshot.weightsThree);
    this.hiddenOne = new Float64Array(snapshot.hiddenOne);
    this.hiddenTwo = new Float64Array(snapshot.hiddenTwo);
  }

  value(input: Float64Array) {
    if (input.length !== this.snapshot.inputSize) {
      throw new Error(
        `Expected ${this.snapshot.inputSize} Q inputs, got ${input.length}`,
      );
    }
    for (let output = 0; output < this.snapshot.hiddenOne; output += 1) {
      let sum = this.biasesOne[output];
      const offset = output * this.snapshot.inputSize;
      for (let index = 0; index < this.snapshot.inputSize; index += 1) {
        sum += this.weightsOne[offset + index] * input[index];
      }
      this.hiddenOne[output] = Math.max(0, sum);
    }
    for (let output = 0; output < this.snapshot.hiddenTwo; output += 1) {
      let sum = this.biasesTwo[output];
      const offset = output * this.snapshot.hiddenOne;
      for (let index = 0; index < this.snapshot.hiddenOne; index += 1) {
        sum += this.weightsTwo[offset + index] * this.hiddenOne[index];
      }
      this.hiddenTwo[output] = Math.max(0, sum);
    }
    let value = this.snapshot.biasThree;
    for (let index = 0; index < this.snapshot.hiddenTwo; index += 1) {
      value += this.weightsThree[index] * this.hiddenTwo[index];
    }
    return value;
  }
}

/** Compile an untrusted JSON value into a bounded, observable-only policy. */
export function compileDqnCheckpoint(
  input: unknown,
  options: CompileDqnPolicyOptions = {},
): CompiledDqnPolicy {
  const artifact = recordValue(input, "DQN checkpoint");
  if (artifact.format !== FORMAT || artifact.version !== VERSION) {
    throw new Error(
      `Unsupported DQN checkpoint; expected ${FORMAT} version ${VERSION}`,
    );
  }
  if (artifact.algorithm !== "double-dqn" || artifact.observableOnly !== true) {
    throw new Error("DQN checkpoint must be an observable-only Double-DQN");
  }
  const artifactOptions = recordValue(artifact.options, "DQN options");
  const policySamples = boundedCheckpointInteger(
    artifactOptions.policySamples,
    "options.policySamples",
    1,
    32,
  );
  const policySeed = boundedCheckpointInteger(
    artifactOptions.policySeed,
    "options.policySeed",
    0,
    0xffff_ffff,
  ) >>> 0;
  const snapshot = parseNetworkSnapshot(artifact.network);
  const network = new InferenceQNetwork(snapshot);
  const cacheEntries = boundedCheckpointInteger(
    options.cacheEntries ?? 8_192,
    "cacheEntries",
    0,
    100_000,
  );
  const cache = new Map<string, number | null>();

  return Object.freeze({
    policySamples,
    policySeed,
    cacheEntries,
    chooseMove(state: Readonly<GameState>) {
      assertInferenceState(state);
      const key = observableStateKey(state);
      if (cache.has(key)) {
        const cached = cache.get(key)!;
        cache.delete(key);
        cache.set(key, cached);
        return cached;
      }
      const column = chooseAction(
        { ...state },
        network,
        policySamples,
        policySeed,
      );
      if (cacheEntries > 0) {
        cache.set(key, column);
        if (cache.size > cacheEntries) cache.delete(cache.keys().next().value!);
      }
      return column;
    },
    evaluateActions(state: Readonly<GameState>) {
      assertInferenceState(state);
      if (state.gameOver) return [];
      const mutableState: GameState = { ...state };
      const observable = canonicalObservable(mutableState);
      return columnOrder(observable.mirrored)
        .filter((column) => state.board[column] === EMPTY)
        .map((column) => ({
          column,
          value: network.value(
            actionInput(mutableState, column, policySamples, policySeed),
          ),
        }));
    },
    evaluateState(state: Readonly<GameState>) {
      const actions = this.evaluateActions(state);
      let value = Number.NEGATIVE_INFINITY;
      for (const action of actions) value = Math.max(value, action.value);
      return value;
    },
    clearCache() {
      cache.clear();
    },
  });
}

export async function loadDqnCheckpoint(
  path: string,
  options: CompileDqnPolicyOptions = {},
) {
  const source = await readFile(path, "utf8");
  let parsed: unknown;
  try {
    parsed = JSON.parse(source) as unknown;
  } catch (error) {
    throw new Error(`DQN checkpoint is not valid JSON: ${String(error)}`);
  }
  return compileDqnCheckpoint(parsed, options);
}

function parseNetworkSnapshot(input: unknown): NetworkSnapshot {
  const network = recordValue(input, "DQN network");
  const inputSize = boundedCheckpointInteger(
    network.inputSize,
    "network.inputSize",
    FEATURE_SIZE,
    FEATURE_SIZE,
  );
  const hiddenOne = boundedCheckpointInteger(
    network.hiddenOne,
    "network.hiddenOne",
    1,
    1_024,
  );
  const hiddenTwo = boundedCheckpointInteger(
    network.hiddenTwo,
    "network.hiddenTwo",
    1,
    1_024,
  );
  return {
    inputSize,
    hiddenOne,
    hiddenTwo,
    weightsOne: finiteCheckpointArray(
      network.weightsOne,
      "network.weightsOne",
      inputSize * hiddenOne,
    ),
    biasesOne: finiteCheckpointArray(
      network.biasesOne,
      "network.biasesOne",
      hiddenOne,
    ),
    weightsTwo: finiteCheckpointArray(
      network.weightsTwo,
      "network.weightsTwo",
      hiddenOne * hiddenTwo,
    ),
    biasesTwo: finiteCheckpointArray(
      network.biasesTwo,
      "network.biasesTwo",
      hiddenTwo,
    ),
    weightsThree: finiteCheckpointArray(
      network.weightsThree,
      "network.weightsThree",
      hiddenTwo,
    ),
    biasThree: finiteCheckpointNumber(network.biasThree, "network.biasThree"),
  };
}

function recordValue(input: unknown, label: string): Record<string, unknown> {
  if (typeof input !== "object" || input === null || Array.isArray(input)) {
    throw new Error(`${label} must be an object`);
  }
  return input as Record<string, unknown>;
}

function boundedCheckpointInteger(
  input: unknown,
  label: string,
  minimum: number,
  maximum: number,
) {
  if (!Number.isSafeInteger(input) || (input as number) < minimum || (input as number) > maximum) {
    throw new Error(`${label} must be an integer from ${minimum} to ${maximum}`);
  }
  return input as number;
}

function finiteCheckpointArray(
  input: unknown,
  label: string,
  length: number,
) {
  if (!Array.isArray(input) || input.length !== length) {
    throw new Error(`${label} must contain exactly ${length} values`);
  }
  return input.map((value, index) =>
    finiteCheckpointNumber(value, `${label}[${index}]`),
  );
}

function finiteCheckpointNumber(input: unknown, label: string) {
  if (
    typeof input !== "number" ||
    !Number.isFinite(input) ||
    Math.abs(input) > 1_000_000
  ) {
    throw new Error(`${label} must be a finite, bounded number`);
  }
  return input;
}

function assertInferenceState(state: Readonly<GameState>) {
  if (
    state.board.length !== BOARD_CELLS ||
    state.board.some((cell) => !Number.isInteger(cell) || cell < EMPTY || cell > CRACKED)
  ) {
    throw new Error("DQN inference requires a valid 7 x 7 board");
  }
  if (!Number.isInteger(state.nextDisc) || state.nextDisc < 1 || state.nextDisc > 7) {
    throw new Error("DQN inference requires a visible next disc from 1 to 7");
  }
  for (const [label, value] of [
    ["level", state.level],
    ["movesRemaining", state.movesRemaining],
    ["movesPlayed", state.movesPlayed],
  ] as const) {
    if (!Number.isSafeInteger(value) || value < 0 || value > 65_535) {
      throw new Error(`DQN inference ${label} is invalid`);
    }
  }
}

function observableStateKey(state: Readonly<GameState>) {
  return `${state.board.join("")}:${state.nextDisc}:${state.level}:${state.movesRemaining}:${state.movesPlayed}:${state.gameOver ? 1 : 0}`;
}

/** Fixed-size replay: two boards cost 98 bytes per transition. */
class CompactReplayBuffer {
  readonly capacity: number;
  size = 0;
  private cursor = 0;
  private readonly boards: Uint8Array;
  private readonly nextBoards: Uint8Array;
  private readonly metadata: Uint16Array;
  private readonly nextMetadata: Uint16Array;
  private readonly actions: Uint8Array;
  private readonly rewards: Float32Array;
  private readonly dones: Uint8Array;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.boards = new Uint8Array(capacity * BOARD_CELLS);
    this.nextBoards = new Uint8Array(capacity * BOARD_CELLS);
    this.metadata = new Uint16Array(capacity * 4);
    this.nextMetadata = new Uint16Array(capacity * 4);
    this.actions = new Uint8Array(capacity);
    this.rewards = new Float32Array(capacity);
    this.dones = new Uint8Array(capacity);
  }

  add(experience: Experience) {
    const index = this.cursor;
    this.writeState(this.boards, this.metadata, index, experience.state);
    this.writeState(
      this.nextBoards,
      this.nextMetadata,
      index,
      experience.nextState,
    );
    this.actions[index] = experience.action;
    this.rewards[index] = experience.reward;
    this.dones[index] = experience.done ? 1 : 0;
    this.cursor = (this.cursor + 1) % this.capacity;
    this.size = Math.min(this.capacity, this.size + 1);
  }

  sample(random: () => number): Experience {
    if (this.size === 0) throw new Error("Cannot sample an empty replay buffer");
    const index = Math.floor(random() * this.size);
    return {
      state: this.readState(this.boards, this.metadata, index),
      action: this.actions[index],
      reward: this.rewards[index],
      nextState: this.readState(this.nextBoards, this.nextMetadata, index),
      done: this.dones[index] === 1,
    };
  }

  byteLength() {
    return (
      this.boards.byteLength +
      this.nextBoards.byteLength +
      this.metadata.byteLength +
      this.nextMetadata.byteLength +
      this.actions.byteLength +
      this.rewards.byteLength +
      this.dones.byteLength
    );
  }

  private writeState(
    boards: Uint8Array,
    metadata: Uint16Array,
    index: number,
    state: CompactState,
  ) {
    boards.set(state.board, index * BOARD_CELLS);
    const offset = index * 4;
    metadata[offset] = state.nextDisc;
    metadata[offset + 1] = state.level;
    metadata[offset + 2] = state.movesRemaining;
    metadata[offset + 3] = state.movesPlayed;
  }

  private readState(
    boards: Uint8Array,
    metadata: Uint16Array,
    index: number,
  ): CompactState {
    const boardOffset = index * BOARD_CELLS;
    const metadataOffset = index * 4;
    return {
      board: Array.from(
        boards.subarray(boardOffset, boardOffset + BOARD_CELLS),
        (cell) => cell as Board[number],
      ),
      nextDisc: metadata[metadataOffset] as CompactState["nextDisc"],
      level: metadata[metadataOffset + 1],
      movesRemaining: metadata[metadataOffset + 2],
      movesPlayed: metadata[metadataOffset + 3],
      gameOver: false,
    };
  }
}

export async function trainDoubleDqn(options: Arguments) {
  const random = seededRandom(options.trainerSeed);
  const online = new DenseQNetwork(
    FEATURE_SIZE,
    options.hiddenOne,
    options.hiddenTwo,
    random,
  );
  const target = new DenseQNetwork(
    FEATURE_SIZE,
    options.hiddenOne,
    options.hiddenTwo,
    () => 0.5,
  );
  target.copyFrom(online);
  const replay = new CompactReplayBuffer(options.replayCapacity);
  const trainingSeeds = consecutiveSeeds(TRAINING_SEED_START, options.trainingGames);
  const probeSeeds = consecutiveSeeds(
    TRAINING_SEED_START + options.trainingGames,
    options.curveGames,
  );
  const validationSeeds = consecutiveSeeds(
    VALIDATION_SEED_START,
    options.validationGames,
  );

  process.stdout.write(
    `observable Double-DQN · train ${formatSeedRange(trainingSeeds)} · probe ${formatSeedRange(probeSeeds)} · validation ${formatSeedRange(validationSeeds)} · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  process.stdout.write(
    `network ${FEATURE_SIZE}→${options.hiddenOne}→${options.hiddenTwo}→1 · replay ${formatBytes(replay.byteLength())} fixed · ${options.policySamples} deterministic candidate probes\n`,
  );

  const curves: CurvePoint[] = [];
  let bestSnapshot = online.snapshot();
  let bestProbe: Summary | undefined;
  let environmentSteps = 0;
  let episodes = 0;
  let updates = 0;
  let lossSum = 0;
  let lossCount = 0;
  let nextEvaluation = options.evaluateEvery;
  const shuffledSeeds = [...trainingSeeds];
  shuffle(shuffledSeeds, random);

  while (environmentSteps < options.trainingSteps) {
    if (episodes > 0 && episodes % shuffledSeeds.length === 0) {
      shuffle(shuffledSeeds, random);
    }
    const gameSeed = shuffledSeeds[episodes % shuffledSeeds.length];
    let state = initialDqnState(gameSeed);

    while (!state.gameOver && state.movesPlayed < options.maxMoves) {
      const epsilon = annealedEpsilon(environmentSteps, options);
      const action = chooseAction(
        state,
        online,
        options.policySamples,
        options.policySeed,
        random,
        epsilon,
      );
      if (action === null) throw new Error("DQN found no move in a live state");
      const move = playActualDqnMove(state, action, gameSeed);
      if (!move) throw new Error(`DQN chose illegal column ${action}`);
      const capped = move.state.movesPlayed >= options.maxMoves;
      const reward = shapedReward(move, move.state.gameOver);
      replay.add({
        state: compactDqnState(state),
        action,
        reward,
        nextState: compactDqnState(move.state),
        done: move.state.gameOver || capped,
      });
      state = move.state;
      environmentSteps += 1;

      if (
        replay.size >= Math.max(options.warmup, options.batchSize) &&
        environmentSteps % options.trainEvery === 0
      ) {
        const samples: TrainingSample[] = [];
        for (let batch = 0; batch < options.batchSize; batch += 1) {
          const experience = replay.sample(random);
          let targetValue = experience.reward;
          if (!experience.done) {
            const nextState = expandDqnState(experience.nextState);
            const nextAction = chooseAction(
              nextState,
              online,
              options.policySamples,
              options.policySeed,
            );
            if (nextAction !== null) {
              const targetInput = actionInput(
                nextState,
                nextAction,
                options.policySamples,
                options.policySeed,
              );
              // Double-DQN: online network selects, lagged target evaluates.
              targetValue += options.gamma * target.value(targetInput);
            }
          }
          samples.push({
            input: actionInput(
              expandDqnState(experience.state),
              experience.action,
              options.policySamples,
              options.policySeed,
            ),
            target: targetValue,
          });
        }
        lossSum += online.trainBatch(samples, options.learningRate);
        lossCount += 1;
        updates += 1;
        if (updates % options.targetEvery === 0) target.copyFrom(online);
      }

      if (environmentSteps >= nextEvaluation || environmentSteps >= options.trainingSteps) {
        const probe = evaluateNetwork(
          online,
          probeSeeds,
          options.policySamples,
          options.policySeed,
          options.maxMoves,
        );
        const point: CurvePoint = {
          step: environmentSteps,
          episodes,
          epsilon,
          meanTdLoss: lossCount === 0 ? 0 : lossSum / lossCount,
          replaySize: replay.size,
          probe: omitResults(probe),
        };
        curves.push(point);
        process.stdout.write(
          `step ${formatInteger(environmentSteps)} · ε ${epsilon.toFixed(3)} · loss ${point.meanTdLoss.toFixed(4)} · probe ${formatSummary(probe)}\n`,
        );
        if (!bestProbe || compareSummary(probe, bestProbe) > 0) {
          bestProbe = probe;
          bestSnapshot = online.snapshot();
        }
        lossSum = 0;
        lossCount = 0;
        while (nextEvaluation <= environmentSteps) {
          nextEvaluation += options.evaluateEvery;
        }
      }
      if (state.gameOver || environmentSteps >= options.trainingSteps) break;
    }
    episodes += 1;
  }

  online.restore(bestSnapshot);
  const validation = pairedValidation(online, validationSeeds, options);
  process.stdout.write(`validation baseline · ${formatSummaryLike(validation.baseline)}\n`);
  process.stdout.write(`validation DQN      · ${formatSummaryLike(validation.candidate)}\n`);
  process.stdout.write(
    `paired delta ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
  );

  const materiallyBetter = isMaterialImprovement(validation);
  if (materiallyBetter) {
    const artifact: DqnArtifact = {
      format: FORMAT,
      version: VERSION,
      algorithm: "double-dqn",
      observableOnly: true,
      trainingSeedStart: TRAINING_SEED_START,
      validationSeedStart: VALIDATION_SEED_START,
      reservedFinalSeedStart: RESERVED_FINAL_SEED_START,
      options: serializableOptions(options),
      reward: {
        legalMove: 1,
        terminalPenalty: TERMINAL_PENALTY,
        scoreScale: 0.15 / 100_000,
        revealScale: 0.025,
        clearScale: 0.01,
        chainScale: 0.015,
      },
      network: bestSnapshot,
      curves,
      validation,
    };
    await writeArtifact(options.outputPath, artifact);
    process.stdout.write(`checkpoint ${resolve(options.outputPath)}\n`);
  } else {
    process.stdout.write(
      `checkpoint withheld: requires ≥${CHECKPOINT_MINIMUM_MOVE_DELTA.toFixed(1)} moves, ≥${Math.round(CHECKPOINT_MINIMUM_SCORE_FRACTION * 100)}% score, and more paired wins than losses\n`,
    );
  }
  return { curves, validation, materiallyBetter };
}

export function chooseAction(
  state: GameState,
  network: QValueNetwork,
  samples: number,
  policySeed: number,
  random?: () => number,
  epsilon = 0,
) {
  if (state.gameOver) return null;
  const observable = canonicalObservable(state);
  const legal = columnOrder(observable.mirrored).filter(
    (column) => state.board[column] === EMPTY,
  );
  if (legal.length === 0) return null;
  if (random && epsilon > 0 && random() < epsilon) {
    return legal[Math.floor(random() * legal.length)];
  }
  let bestColumn = legal[0];
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of legal) {
    const value = network.value(actionInput(state, column, samples, policySeed));
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

export function actionInput(
  state: GameState,
  column: number,
  samples: number,
  policySeed: number,
) {
  const observable = canonicalObservable(state);
  const canonicalColumn = observable.mirrored ? BOARD_SIZE - 1 - column : column;
  const board = observable.mirrored ? mirrorBoard(state.board) : state.board;
  const values: number[] = [];

  // Compact spatial state: numbered-value and cover channels.
  for (const cell of board) values.push(cell >= 1 && cell <= 7 ? cell / 7 : 0);
  for (const cell of board) {
    values.push(cell === SOLID ? 1 : cell === CRACKED ? 0.5 : 0);
  }
  for (let disc = 1; disc <= 7; disc += 1) {
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
  const leftHeight =
    column === 0 ? landingHeight : actualHeights[column - 1];
  const rightHeight =
    column === BOARD_SIZE - 1 ? landingHeight : actualHeights[column + 1];
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

  const outcome = expectedCandidateFeatures(
    state,
    column,
    samples,
    policySeed,
    observable.hash,
  );
  values.push(...outcome);
  if (values.length !== FEATURE_SIZE) {
    throw new Error(`Feature size drifted: expected ${FEATURE_SIZE}, got ${values.length}`);
  }
  return Float64Array.from(values);
}

function expectedCandidateFeatures(
  state: GameState,
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
    result[23] += roughness(heights) / (BOARD_SIZE * (BOARD_SIZE - 1)) * scale;
    result[24] += heights.filter((height) => height < BOARD_SIZE).length / BOARD_SIZE * scale;
  }
  return result;
}

function appendHeuristic(target: number[], state: GameState) {
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

function runNetworkGame(
  seed: number,
  network: DenseQNetwork,
  samples: number,
  policySeed: number,
  maxMoves: number,
): GameResult {
  let state = initialDqnState(seed);
  let clears = 0;
  let maxChain = 0;
  let reward = 0;
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = chooseAction(state, network, samples, policySeed);
    if (column === null) throw new Error("DQN found no legal move");
    const move = playActualDqnMove(state, column, seed);
    if (!move) throw new Error(`DQN chose illegal column ${column}`);
    reward += shapedReward(move, move.state.gameOver);
    clears += clearCount(move);
    maxChain = Math.max(maxChain, move.waves.length);
    state = move.state;
  }
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    clears,
    maxChain,
    reward,
  };
}

function evaluateNetwork(
  network: DenseQNetwork,
  seeds: readonly number[],
  samples: number,
  policySeed: number,
  maxMoves: number,
) {
  return summarize(
    seeds.map((seed) =>
      runNetworkGame(seed, network, samples, policySeed, maxMoves),
    ),
  );
}

function pairedValidation(
  network: DenseQNetwork,
  seeds: readonly number[],
  options: Arguments,
): PairedValidation {
  const baselineResults = seeds.map((seed): GameResult => {
    const result = runFairPolicyGame(
      seed,
      initialFairPolicyWeights(),
      { samples: 3, policySeed: options.policySeed },
      options.maxMoves,
    );
    return {
      seed,
      score: result.score,
      moves: result.moves,
      censored: result.censored,
      clears: result.clears,
      maxChain: result.maxChain,
      reward: 0,
    };
  });
  const candidateResults = seeds.map((seed) =>
    runNetworkGame(
      seed,
      network,
      options.policySamples,
      options.policySeed,
      options.maxMoves,
    ),
  );
  const baseline = summarize(baselineResults);
  const candidate = summarize(candidateResults);
  const scoreDeltas = candidateResults.map(
    (result, index) => result.score - baselineResults[index].score,
  );
  const moveDeltas = candidateResults.map(
    (result, index) => result.moves - baselineResults[index].moves,
  );
  return {
    baseline: omitRewardAndResults(baseline),
    candidate: omitRewardAndResults(candidate),
    pairedMeanScoreDelta: mean(scoreDeltas),
    pairedMedianScoreDelta: median(scoreDeltas),
    pairedMeanMoveDelta: mean(moveDeltas),
    wins: scoreDeltas.filter((delta) => delta > 0).length,
    ties: scoreDeltas.filter((delta) => delta === 0).length,
    losses: scoreDeltas.filter((delta) => delta < 0).length,
  };
}

export function shapedReward(move: MoveResult, terminal: boolean) {
  let cleared = 0;
  let revealed = 0;
  for (const wave of move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  return (
    1 +
    Math.min(move.scoreDelta, 100_000) / 100_000 * 0.15 +
    revealed * 0.025 +
    cleared * 0.01 +
    Math.max(0, move.waves.length - 1) * 0.015 -
    (terminal ? TERMINAL_PENALTY : 0)
  );
}

export function initialDqnState(seed: number): GameState {
  return {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
}

export function playActualDqnMove(
  state: GameState,
  column: number,
  seed: number,
) {
  const revealSeed = mix32(
    seed ^
      Math.imul(state.movesPlayed + 1, ACTUAL_MOVE_MULTIPLIER) ^
      ACTUAL_REVEAL_DOMAIN,
  );
  const move = playMove(state, column, seededRandom(revealSeed), {
    captureAnimation: false,
  });
  if (!move) return null;
  const nextState = move.state.gameOver
    ? move.state
    : {
        ...move.state,
        nextDisc: headlessDisc(seed, move.state.movesPlayed),
      };
  return { ...move, state: nextState };
}

export function compactDqnState(state: GameState): CompactState {
  return {
    board: state.board,
    nextDisc: state.nextDisc,
    level: state.level,
    movesRemaining: state.movesRemaining,
    movesPlayed: state.movesPlayed,
    gameOver: state.gameOver,
  };
}

export function expandDqnState(state: CompactState): GameState {
  return { ...state, score: 0 };
}

function canonicalObservable(state: GameState) {
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
  const offset = mix32(observableHash ^ policySeed ^ PROBE_REVEAL_DOMAIN) % 7;
  const stratum = Math.floor(((sample + 0.5) * 7) / samples);
  const disc = ((offset + stratum) % 7) + 1;
  return (disc - 0.5) / 7;
}

function summarize(results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    games: results.length,
    meanScore: mean(scores),
    medianScore: median(scores),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censoredGames: results.filter((result) => result.censored).length,
    meanClears: mean(results.map((result) => result.clears)),
    meanMaxChain: mean(results.map((result) => result.maxChain)),
    meanReward: mean(results.map((result) => result.reward)),
    results,
  };
}

function compareSummary(first: Summary, second: Summary) {
  const moveDelta = first.meanMoves - second.meanMoves;
  if (Math.abs(moveDelta) > 0.25) return moveDelta;
  return first.meanScore - second.meanScore;
}

function isMaterialImprovement(validation: PairedValidation) {
  return (
    validation.pairedMeanMoveDelta >= CHECKPOINT_MINIMUM_MOVE_DELTA &&
    validation.candidate.meanScore >=
      validation.baseline.meanScore * (1 + CHECKPOINT_MINIMUM_SCORE_FRACTION) &&
    validation.wins > validation.losses
  );
}

function omitResults(summary: Summary): Omit<Summary, "results"> {
  const { results: _results, ...rest } = summary;
  void _results;
  return rest;
}

function omitRewardAndResults(
  summary: Summary,
): Omit<Summary, "results" | "meanReward"> {
  const { results: _results, meanReward: _reward, ...rest } = summary;
  void _results;
  void _reward;
  return rest;
}

function serializableOptions(
  options: Arguments,
): Omit<Arguments, "outputPath" | "selfTest"> {
  const { outputPath: _outputPath, selfTest: _selfTest, ...rest } = options;
  void _outputPath;
  void _selfTest;
  return rest;
}

function annealedEpsilon(step: number, options: Arguments) {
  const annealSteps = Math.max(1, options.trainingSteps * options.epsilonFraction);
  const fraction = Math.min(1, step / annealSteps);
  return options.epsilonStart +
    (options.epsilonEnd - options.epsilonStart) * fraction;
}

function initializeWeights(
  target: Float64Array,
  fanIn: number,
  random: () => number,
) {
  const scale = Math.sqrt(2 / fanIn);
  for (let index = 0; index < target.length; index += 1) {
    target[index] = gaussian(random) * scale;
  }
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
  let value = 0;
  for (let column = 1; column < heights.length; column += 1) {
    value += Math.abs(heights[column] - heights[column - 1]);
  }
  return value;
}

function clearCount(move: MoveResult) {
  return move.clearedBoard ? 1 : 0;
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
  const mirrored = board.slice();
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      mirrored[row * BOARD_SIZE + column] =
        board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
    }
  }
  return mirrored;
}

function columnOrder(mirrored: boolean) {
  return mirrored ? MIRRORED_COLUMN_ORDER : COLUMN_ORDER;
}

function mix32(input: number) {
  let value = input >>> 0;
  value ^= value >>> 16;
  value = Math.imul(value, 0x7feb_352d);
  value ^= value >>> 15;
  value = Math.imul(value, 0x846c_a68b);
  value ^= value >>> 16;
  return value >>> 0;
}

function gaussian(random: () => number) {
  const first = Math.max(Number.EPSILON, random());
  const second = random();
  return Math.sqrt(-2 * Math.log(first)) * Math.cos(2 * Math.PI * second);
}

function shuffle<T>(items: T[], random: () => number) {
  for (let index = items.length - 1; index > 0; index -= 1) {
    const other = Math.floor(random() * (index + 1));
    [items[index], items[other]] = [items[other], items[index]];
  }
}

function zip<T>(first: readonly T[], second: readonly T[]) {
  if (first.length !== second.length) throw new Error("Cannot zip unequal arrays");
  return first.map((item, index) => [item, second[index]] as const);
}

function consecutiveSeeds(start: number, count: number) {
  if (!Number.isSafeInteger(count) || count < 1 || count > 10_000) {
    throw new Error("Game count must be an integer between 1 and 10,000");
  }
  if (start + count >= RESERVED_FINAL_SEED_START) {
    throw new Error("Seed range overlaps reserved final evaluation seeds");
  }
  return Array.from({ length: count }, (_, index) => (start + index) >>> 0);
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function median(values: readonly number[]) {
  const sorted = [...values].sort(numberOrder);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function formatSummary(summary: Summary) {
  return `mean ${formatInteger(summary.meanScore)} · median ${formatInteger(summary.medianScore)} · moves ${summary.meanMoves.toFixed(1)} · capped ${summary.censoredGames}/${summary.games} · chain ${summary.meanMaxChain.toFixed(2)}`;
}

function formatSummaryLike(summary: PairedValidation["baseline"]) {
  return `mean ${formatInteger(summary.meanScore)} · median ${formatInteger(summary.medianScore)} · moves ${summary.meanMoves.toFixed(1)} · capped ${summary.censoredGames}/${summary.games} · chain ${summary.meanMaxChain.toFixed(2)}`;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function signedInteger(value: number) {
  return `${value >= 0 ? "+" : ""}${formatInteger(value)}`;
}

function signedNumber(value: number, digits: number) {
  return `${value >= 0 ? "+" : ""}${value.toFixed(digits)}`;
}

function formatSeed(value: number) {
  return `0x${value.toString(16).padStart(8, "0")}`;
}

function formatSeedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}

function formatBytes(bytes: number) {
  return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
}

async function writeArtifact(path: string, artifact: DqnArtifact) {
  const absolute = resolve(path);
  await mkdir(dirname(absolute), { recursive: true });
  const temporary = `${absolute}.tmp-${process.pid}`;
  await writeFile(temporary, `${JSON.stringify(artifact, null, 2)}\n`, "utf8");
  await rename(temporary, absolute);
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const options: Arguments = {
    trainingGames: DEFAULT_TRAINING_GAMES,
    trainingSteps: DEFAULT_TRAINING_STEPS,
    validationGames: DEFAULT_VALIDATION_GAMES,
    curveGames: DEFAULT_CURVE_GAMES,
    maxMoves: DEFAULT_MAX_MOVES,
    replayCapacity: DEFAULT_REPLAY_CAPACITY,
    warmup: DEFAULT_WARMUP,
    batchSize: DEFAULT_BATCH_SIZE,
    trainEvery: DEFAULT_TRAIN_EVERY,
    targetEvery: DEFAULT_TARGET_EVERY,
    evaluateEvery: DEFAULT_EVALUATE_EVERY,
    hiddenOne: DEFAULT_HIDDEN_ONE,
    hiddenTwo: DEFAULT_HIDDEN_TWO,
    learningRate: DEFAULT_LEARNING_RATE,
    gamma: DEFAULT_GAMMA,
    epsilonStart: DEFAULT_EPSILON_START,
    epsilonEnd: DEFAULT_EPSILON_END,
    epsilonFraction: DEFAULT_EPSILON_FRACTION,
    policySamples: DEFAULT_POLICY_SAMPLES,
    trainerSeed: DEFAULT_TRAINER_SEED,
    policySeed: DEFAULT_POLICY_SEED,
    outputPath: DEFAULT_OUTPUT,
    selfTest: false,
  };
  const numeric = new Map<string, keyof Arguments>([
    ["--training-games", "trainingGames"],
    ["--training-steps", "trainingSteps"],
    ["--validation-games", "validationGames"],
    ["--curve-games", "curveGames"],
    ["--max-moves", "maxMoves"],
    ["--replay-capacity", "replayCapacity"],
    ["--warmup", "warmup"],
    ["--batch-size", "batchSize"],
    ["--train-every", "trainEvery"],
    ["--target-every", "targetEvery"],
    ["--evaluate-every", "evaluateEvery"],
    ["--hidden-one", "hiddenOne"],
    ["--hidden-two", "hiddenTwo"],
    ["--learning-rate", "learningRate"],
    ["--gamma", "gamma"],
    ["--epsilon-start", "epsilonStart"],
    ["--epsilon-end", "epsilonEnd"],
    ["--epsilon-fraction", "epsilonFraction"],
    ["--samples", "policySamples"],
    ["--trainer-seed", "trainerSeed"],
    ["--policy-seed", "policySeed"],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--self-test") {
      options.selfTest = true;
      continue;
    }
    if (flag === "--output") {
      options.outputPath = requiredValue(arguments_, ++index, flag);
      continue;
    }
    const key = numeric.get(flag);
    if (!key) throw new Error(`Unknown argument: ${flag}`);
    const raw = requiredValue(arguments_, ++index, flag);
    (options as unknown as Record<string, number>)[key] = Number(raw);
  }
  validateArguments(options);
  return options;
}

function validateArguments(options: Arguments) {
  for (const key of [
    "trainingGames",
    "trainingSteps",
    "validationGames",
    "curveGames",
    "maxMoves",
    "replayCapacity",
    "warmup",
    "batchSize",
    "trainEvery",
    "targetEvery",
    "evaluateEvery",
    "hiddenOne",
    "hiddenTwo",
    "policySamples",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  for (const key of ["trainerSeed", "policySeed"] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 0 || options[key] > 0xffff_ffff) {
      throw new Error(`${key} must be a uint32 integer`);
    }
    options[key] >>>= 0;
  }
  if (!(options.learningRate > 0 && Number.isFinite(options.learningRate))) {
    throw new Error("learningRate must be positive and finite");
  }
  if (!(options.gamma >= 0 && options.gamma <= 1)) throw new Error("gamma must be in [0, 1]");
  if (!(options.epsilonStart >= 0 && options.epsilonStart <= 1)) throw new Error("epsilonStart must be in [0, 1]");
  if (!(options.epsilonEnd >= 0 && options.epsilonEnd <= 1)) throw new Error("epsilonEnd must be in [0, 1]");
  if (!(options.epsilonFraction > 0 && options.epsilonFraction <= 1)) throw new Error("epsilonFraction must be in (0, 1]");
  if (options.batchSize > options.replayCapacity) throw new Error("batchSize cannot exceed replayCapacity");
}

function requiredValue(arguments_: readonly string[], index: number, flag: string) {
  const value = arguments_[index];
  if (value === undefined) throw new Error(`${flag} needs a value`);
  return value;
}

export function runSelfTest() {
  const random = seededRandom(12345);
  const network = new DenseQNetwork(FEATURE_SIZE, 8, 4, random);
  const initial = initialDqnState(42);
  const asymmetricBoard = placeDisc(initial.board, 0, 4);
  if (!asymmetricBoard) throw new Error("Could not create self-test board");
  const state: GameState = { ...initial, board: asymmetricBoard };
  const input = actionInput(state, 1, 2, 99);
  if (input.length !== FEATURE_SIZE || [...input].some((value) => !Number.isFinite(value))) {
    throw new Error("Action features are invalid");
  }
  const mirroredState: GameState = { ...state, board: mirrorBoard(state.board) };
  const mirroredInput = actionInput(mirroredState, 5, 2, 99);
  if ([...input].some((value, index) => Math.abs(value - mirroredInput[index]) > 1e-12)) {
    throw new Error("Canonical features broke reflection symmetry");
  }
  const replay = new CompactReplayBuffer(4);
  replay.add({
    state: compactDqnState(state),
    action: 3,
    reward: 1,
    nextState: compactDqnState(state),
    done: false,
  });
  const sampled = replay.sample(() => 0);
  if (sampled.action !== 3 || sampled.state.board.length !== BOARD_CELLS) {
    throw new Error("Compact replay round trip failed");
  }
  const before = network.value(input);
  for (let iteration = 0; iteration < 100; iteration += 1) {
    network.trainBatch([{ input, target: 3 }], 0.003);
  }
  const after = network.value(input);
  if (Math.abs(after - 3) >= Math.abs(before - 3)) {
    throw new Error("Q network did not learn a scalar target");
  }
  const checkpoint = {
    format: FORMAT,
    version: VERSION,
    algorithm: "double-dqn",
    observableOnly: true,
    options: { policySamples: 2, policySeed: 99 },
    network: network.snapshot(),
  };
  const compiled = compileDqnCheckpoint(checkpoint, { cacheEntries: 2 });
  const compiledMove = compiled.chooseMove(state);
  if (compiledMove === null || state.board[compiledMove] !== EMPTY) {
    throw new Error("Compiled DQN chose an illegal move");
  }
  if (
    compiled.evaluateActions(state).length !== BOARD_SIZE ||
    !Number.isFinite(compiled.evaluateState(state))
  ) {
    throw new Error("Compiled DQN values are invalid");
  }
  assertThrows(
    () =>
      compileDqnCheckpoint({
        ...checkpoint,
        network: { ...checkpoint.network, weightsThree: [Number.NaN] },
      }),
    /weightsThree/,
  );
  process.stdout.write("drop7 DQN self-test passed\n");
}

function assertThrows(callback: () => void, pattern: RegExp) {
  let error: unknown;
  try {
    callback();
  } catch (caught) {
    error = caught;
  }
  if (!(error instanceof Error) || !pattern.test(error.message)) {
    throw new Error(`Expected error matching ${String(pattern)}`);
  }
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  await trainDoubleDqn(options);
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await runCli(process.argv.slice(2));
}
