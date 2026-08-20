import { writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  LEARNED_POLICY_FORMAT,
  LEARNED_POLICY_ACCUMULATOR_SIZE,
  LEARNED_POLICY_HIDDEN_SIZE,
  LEARNED_POLICY_TOKEN_COUNT,
  LEARNED_POLICY_VERSION,
  extractLearnedPolicyTokens,
  type SerializedLearnedPolicyWeights,
} from "../../../src/core/typescript/learned-evaluator.ts";
import { planOracleMove } from "../perfect-information-oracle/main.ts";

/**
 * Trains a public-state student by imitating a privileged teacher.
 *
 * The teacher deliberately sees one training game's exact future tape. The
 * student never receives a seed, tape, move index, score, or level: it sees
 * only canonical absolute-position tokens, the five-move phase, column
 * heights, and the current disc. Aggregating labels across independent games
 * is what can turn privileged demonstrations into a fair policy.
 */

const TRAINING_SEED_START = 0x3d70_0000;
const DAGGER_SEED_START = 0x3d71_0000;
const PROBE_SEED_START = 0x4d70_0000;
const REVEAL_DOMAIN = 0x5245_564c;
const REVEAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const INITIALIZATION_SEED = 0xd707_4f52;
const SHUFFLE_DOMAIN = 0x5348_5546;
const CENTER_FIRST = [3, 2, 4, 1, 5, 0, 6] as const;
const ACCUMULATOR_SIZE = 32;
const HIDDEN_SIZE = 16;
const TOKEN_COUNT = LEARNED_POLICY_TOKEN_COUNT;
const LABEL_SMOOTHING = 0.05;
const GRADIENT_CLIP = 5;
const ADAM_BETA_ONE = 0.9;
const ADAM_BETA_TWO = 0.999;
const ADAM_EPSILON = 1e-8;
const MAX_EXAMPLES = 50_000;

interface Options {
  oracleGames: number;
  daggerGames: number;
  probeGames: number;
  maxMoves: number;
  oracleDepth: number;
  oracleBeam: number;
  initialEpochs: number;
  daggerEpochs: number;
  learningRate: number;
  daggerWeight: number;
  output?: string;
}

interface Example {
  tokens: Uint16Array;
  legal: Uint8Array;
  label: number;
  weight: number;
}

interface Model {
  embedding: Float32Array;
  accumulatorBias: Float32Array;
  hiddenWeights: Float32Array;
  hiddenBias: Float32Array;
  outputWeights: Float32Array;
  outputBias: Float32Array;
}

interface Moments {
  first: Float32Array;
  second: Float32Array;
}

interface Optimizer {
  step: number;
  betaOnePower: number;
  betaTwoPower: number;
  embedding: Moments;
  accumulatorBias: Moments;
  hiddenWeights: Moments;
  hiddenBias: Moments;
  outputWeights: Moments;
  outputBias: Moments;
}

interface ForwardPass {
  accumulatorBefore: Float32Array;
  accumulator: Float32Array;
  hiddenBefore: Float32Array;
  hidden: Float32Array;
  logits: Float32Array;
}

interface EpisodeResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  generatedStates: number;
  labels: number;
  elapsedMs: number;
}

interface Summary {
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censored: number;
}

interface TrainingReport {
  loss: number;
  accuracy: number;
  epochs: number;
  updates: number;
}

class Random {
  private state: number;

  constructor(seed: number) {
    this.state = seed >>> 0;
  }

  bits() {
    this.state = (this.state + 0x6d2b_79f5) >>> 0;
    let value = this.state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return (value ^ (value >>> 14)) >>> 0;
  }

  unit() {
    return this.bits() / 4_294_967_296;
  }
}

function initialState(seed: number): GameState {
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

function actualRevealSeed(seed: number, movesPlayed: number) {
  return mix32(
    seed ^
      Math.imul((movesPlayed + 1) >>> 0, REVEAL_MOVE_MULTIPLIER) ^
      REVEAL_DOMAIN,
  );
}

function advanceActual(state: GameState, column: number, seed: number) {
  const move = playMove(
    state,
    column,
    seededRandom(actualRevealSeed(seed, state.movesPlayed)),
    { captureAnimation: false },
  );
  if (!move) throw new Error(`Policy selected illegal column ${column}`);
  return move.state.gameOver
    ? move.state
    : {
        ...move.state,
        nextDisc: headlessDisc(seed, move.state.movesPlayed),
      };
}

function exampleFromState(
  state: GameState,
  teacherColumn: number,
  weight: number,
): Example {
  const { tokenIds, mirrored } = extractLearnedPolicyTokens(state);
  const legal = new Uint8Array(BOARD_SIZE);
  for (const column of legalColumns(state.board)) {
    legal[mirrored ? BOARD_SIZE - 1 - column : column] = 1;
  }
  const label = mirrored
    ? BOARD_SIZE - 1 - teacherColumn
    : teacherColumn;
  if (legal[label] !== 1) throw new Error("Oracle label was not legal");
  return { tokens: tokenIds, legal, label, weight };
}

function collectOracleEpisode(
  seed: number,
  options: Options,
  examples: Example[],
): EpisodeResult {
  const started = performance.now();
  let state = initialState(seed);
  let generatedStates = 0;
  let labels = 0;
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const plan = planOracleMove(
      state,
      seed,
      options.oracleDepth,
      options.oracleBeam,
    );
    if (plan.column === null) {
      throw new Error("Privileged oracle found no move in a live game");
    }
    generatedStates += plan.generatedStates;
    if (examples.length >= MAX_EXAMPLES) {
      throw new Error(`Dataset exceeded ${MAX_EXAMPLES} examples`);
    }
    examples.push(exampleFromState(state, plan.column, 1));
    labels += 1;
    state = advanceActual(state, plan.column, seed);
  }
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    generatedStates,
    labels,
    elapsedMs: performance.now() - started,
  };
}

function collectDaggerEpisode(
  seed: number,
  options: Options,
  model: Model,
  examples: Example[],
): EpisodeResult {
  const started = performance.now();
  let state = initialState(seed);
  let generatedStates = 0;
  let labels = 0;
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    // Query the privileged oracle at the state visited by the seed-blind
    // student, then still execute the student's action (standard DAgger).
    const plan = planOracleMove(
      state,
      seed,
      options.oracleDepth,
      options.oracleBeam,
    );
    if (plan.column === null) {
      throw new Error("DAgger oracle found no move in a live game");
    }
    generatedStates += plan.generatedStates;
    if (examples.length >= MAX_EXAMPLES) {
      throw new Error(`Dataset exceeded ${MAX_EXAMPLES} examples`);
    }
    examples.push(
      exampleFromState(state, plan.column, options.daggerWeight),
    );
    labels += 1;
    const studentColumn = chooseStudentColumn(state, model);
    if (studentColumn === null) {
      throw new Error("Student found no move in a live DAgger state");
    }
    state = advanceActual(state, studentColumn, seed);
  }
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    generatedStates,
    labels,
    elapsedMs: performance.now() - started,
  };
}

function evaluateStudentEpisode(
  seed: number,
  maxMoves: number,
  model: Model,
): EpisodeResult {
  const started = performance.now();
  let state = initialState(seed);
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = chooseStudentColumn(state, model);
    if (column === null) throw new Error("Student found no legal move");
    state = advanceActual(state, column, seed);
  }
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    generatedStates: 0,
    labels: 0,
    elapsedMs: performance.now() - started,
  };
}

function createModel(): Model {
  const random = new Random(INITIALIZATION_SEED);
  const vector = (length: number, scale = 0.025) =>
    Float32Array.from(
      { length },
      () => Math.fround((random.unit() * 2 - 1) * scale),
    );
  const model: Model = {
    embedding: vector(TOKEN_COUNT * ACCUMULATOR_SIZE),
    accumulatorBias: new Float32Array(ACCUMULATOR_SIZE).fill(0.1),
    hiddenWeights: vector(HIDDEN_SIZE * ACCUMULATOR_SIZE),
    hiddenBias: new Float32Array(HIDDEN_SIZE).fill(0.1),
    outputWeights: vector(BOARD_SIZE * HIDDEN_SIZE),
    outputBias: new Float32Array(BOARD_SIZE),
  };
  return model;
}

function moments(length: number): Moments {
  return {
    first: new Float32Array(length),
    second: new Float32Array(length),
  };
}

function createOptimizer(model: Model): Optimizer {
  return {
    step: 0,
    betaOnePower: 1,
    betaTwoPower: 1,
    embedding: moments(model.embedding.length),
    accumulatorBias: moments(model.accumulatorBias.length),
    hiddenWeights: moments(model.hiddenWeights.length),
    hiddenBias: moments(model.hiddenBias.length),
    outputWeights: moments(model.outputWeights.length),
    outputBias: moments(model.outputBias.length),
  };
}

function forward(tokens: Uint16Array, model: Model): ForwardPass {
  const accumulatorBefore = new Float32Array(model.accumulatorBias);
  for (const token of tokens) {
    const offset = token * ACCUMULATOR_SIZE;
    for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
      accumulatorBefore[unit] = Math.fround(
        accumulatorBefore[unit] + model.embedding[offset + unit],
      );
    }
  }
  const accumulator = new Float32Array(ACCUMULATOR_SIZE);
  for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
    accumulator[unit] = Math.max(0, accumulatorBefore[unit]);
  }
  const hiddenBefore = new Float32Array(HIDDEN_SIZE);
  const hidden = new Float32Array(HIDDEN_SIZE);
  for (let hiddenUnit = 0; hiddenUnit < HIDDEN_SIZE; hiddenUnit += 1) {
    let value = model.hiddenBias[hiddenUnit];
    const offset = hiddenUnit * ACCUMULATOR_SIZE;
    for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
      value += accumulator[unit] * model.hiddenWeights[offset + unit];
    }
    hiddenBefore[hiddenUnit] = value;
    hidden[hiddenUnit] = Math.max(0, value);
  }
  const logits = new Float32Array(BOARD_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let value = model.outputBias[column];
    const offset = column * HIDDEN_SIZE;
    for (let unit = 0; unit < HIDDEN_SIZE; unit += 1) {
      value += hidden[unit] * model.outputWeights[offset + unit];
    }
    logits[column] = value;
  }
  return { accumulatorBefore, accumulator, hiddenBefore, hidden, logits };
}

function bestCanonicalColumn(logits: Float32Array, legal: Uint8Array) {
  let best = -1;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of CENTER_FIRST) {
    if (legal[column] !== 1) continue;
    if (logits[column] > bestValue) {
      best = column;
      bestValue = logits[column];
    }
  }
  return best;
}

function chooseStudentColumn(state: GameState, model: Model) {
  const { tokenIds, mirrored } = extractLearnedPolicyTokens(state);
  const legal = new Uint8Array(BOARD_SIZE);
  for (const physical of legalColumns(state.board)) {
    legal[mirrored ? BOARD_SIZE - 1 - physical : physical] = 1;
  }
  const canonical = bestCanonicalColumn(forward(tokenIds, model).logits, legal);
  if (canonical < 0) return null;
  return mirrored ? BOARD_SIZE - 1 - canonical : canonical;
}

function softmaxGradient(
  example: Example,
  logits: Float32Array,
) {
  let maximum = Number.NEGATIVE_INFINITY;
  let legalCount = 0;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (example.legal[column] !== 1) continue;
    maximum = Math.max(maximum, logits[column]);
    legalCount += 1;
  }
  const exponentials = new Float64Array(BOARD_SIZE);
  let total = 0;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (example.legal[column] !== 1) continue;
    exponentials[column] = Math.exp(logits[column] - maximum);
    total += exponentials[column];
  }
  const gradient = new Float32Array(BOARD_SIZE);
  let loss = 0;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (example.legal[column] !== 1) continue;
    const probability = exponentials[column] / total;
    const target =
      LABEL_SMOOTHING / legalCount +
      (column === example.label ? 1 - LABEL_SMOOTHING : 0);
    loss -= target * Math.log(Math.max(Number.MIN_VALUE, probability));
    gradient[column] = Math.fround(
      (probability - target) * example.weight,
    );
  }
  return { gradient, loss: loss * example.weight };
}

function clipped(value: number) {
  return Math.max(-GRADIENT_CLIP, Math.min(GRADIENT_CLIP, value));
}

function adamIndex(
  weights: Float32Array,
  state: Moments,
  index: number,
  gradient: number,
  learningRate: number,
  correctionOne: number,
  correctionTwo: number,
) {
  const first =
    ADAM_BETA_ONE * state.first[index] + (1 - ADAM_BETA_ONE) * gradient;
  const second =
    ADAM_BETA_TWO * state.second[index] +
    (1 - ADAM_BETA_TWO) * gradient * gradient;
  state.first[index] = Math.fround(first);
  state.second[index] = Math.fround(second);
  weights[index] = Math.fround(
    weights[index] -
      (learningRate * (first / correctionOne)) /
        (Math.sqrt(second / correctionTwo) + ADAM_EPSILON),
  );
}

function trainExample(
  example: Example,
  model: Model,
  optimizer: Optimizer,
  learningRate: number,
) {
  const pass = forward(example.tokens, model);
  const { gradient: outputGradient, loss } = softmaxGradient(
    example,
    pass.logits,
  );
  const hiddenGradient = new Float32Array(HIDDEN_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const gradient = outputGradient[column];
    if (gradient === 0) continue;
    const offset = column * HIDDEN_SIZE;
    for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
      hiddenGradient[hidden] +=
        gradient * model.outputWeights[offset + hidden];
    }
  }
  for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
    hiddenGradient[hidden] =
      pass.hiddenBefore[hidden] > 0 ? clipped(hiddenGradient[hidden]) : 0;
  }
  const accumulatorGradient = new Float32Array(ACCUMULATOR_SIZE);
  for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
    const gradient = hiddenGradient[hidden];
    if (gradient === 0) continue;
    const offset = hidden * ACCUMULATOR_SIZE;
    for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
      accumulatorGradient[unit] +=
        gradient * model.hiddenWeights[offset + unit];
    }
  }
  for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
    accumulatorGradient[unit] =
      pass.accumulatorBefore[unit] > 0
        ? clipped(accumulatorGradient[unit])
        : 0;
  }

  optimizer.step += 1;
  optimizer.betaOnePower *= ADAM_BETA_ONE;
  optimizer.betaTwoPower *= ADAM_BETA_TWO;
  const correctionOne = 1 - optimizer.betaOnePower;
  const correctionTwo = 1 - optimizer.betaTwoPower;

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const gradient = outputGradient[column];
    if (gradient === 0) continue;
    adamIndex(
      model.outputBias,
      optimizer.outputBias,
      column,
      clipped(gradient),
      learningRate,
      correctionOne,
      correctionTwo,
    );
    const offset = column * HIDDEN_SIZE;
    for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
      adamIndex(
        model.outputWeights,
        optimizer.outputWeights,
        offset + hidden,
        clipped(gradient * pass.hidden[hidden]),
        learningRate,
        correctionOne,
        correctionTwo,
      );
    }
  }

  for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
    const gradient = hiddenGradient[hidden];
    adamIndex(
      model.hiddenBias,
      optimizer.hiddenBias,
      hidden,
      gradient,
      learningRate,
      correctionOne,
      correctionTwo,
    );
    const offset = hidden * ACCUMULATOR_SIZE;
    for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
      adamIndex(
        model.hiddenWeights,
        optimizer.hiddenWeights,
        offset + unit,
        clipped(gradient * pass.accumulator[unit]),
        learningRate,
        correctionOne,
        correctionTwo,
      );
    }
  }

  for (let unit = 0; unit < ACCUMULATOR_SIZE; unit += 1) {
    const gradient = accumulatorGradient[unit];
    adamIndex(
      model.accumulatorBias,
      optimizer.accumulatorBias,
      unit,
      gradient,
      learningRate,
      correctionOne,
      correctionTwo,
    );
    for (const token of example.tokens) {
      const index = token * ACCUMULATOR_SIZE + unit;
      adamIndex(
        model.embedding,
        optimizer.embedding,
        index,
        gradient,
        learningRate,
        correctionOne,
        correctionTwo,
      );
    }
  }
  return loss;
}

function datasetAccuracy(examples: readonly Example[], model: Model) {
  let correct = 0;
  let loss = 0;
  let totalWeight = 0;
  for (const example of examples) {
    const pass = forward(example.tokens, model);
    if (bestCanonicalColumn(pass.logits, example.legal) === example.label) {
      correct += example.weight;
    }
    const result = softmaxGradient(example, pass.logits);
    loss += result.loss;
    totalWeight += example.weight;
  }
  return { accuracy: correct / totalWeight, loss: loss / totalWeight };
}

function train(
  examples: readonly Example[],
  model: Model,
  optimizer: Optimizer,
  epochs: number,
  learningRate: number,
  phase: string,
): TrainingReport {
  const order = Array.from({ length: examples.length }, (_, index) => index);
  let updates = 0;
  let final = { accuracy: 0, loss: Number.POSITIVE_INFINITY };
  for (let epoch = 0; epoch < epochs; epoch += 1) {
    const random = new Random(
      mix32(INITIALIZATION_SEED ^ SHUFFLE_DOMAIN ^ (epoch + 1) ^ updates),
    );
    for (let index = order.length - 1; index > 0; index -= 1) {
      const selected = Math.floor(random.unit() * (index + 1));
      [order[index], order[selected]] = [order[selected], order[index]];
    }
    let epochLoss = 0;
    for (const index of order) {
      epochLoss += trainExample(
        examples[index],
        model,
        optimizer,
        learningRate,
      );
      updates += 1;
    }
    if ((epoch + 1) % 5 === 0 || epoch + 1 === epochs) {
      final = datasetAccuracy(examples, model);
      process.stderr.write(
        `${phase} epoch ${epoch + 1}/${epochs} · online loss ${(epochLoss / examples.length).toFixed(4)} · corpus CE ${final.loss.toFixed(4)} · agreement ${(final.accuracy * 100).toFixed(1)}%\n`,
      );
    }
  }
  return { loss: final.loss, accuracy: final.accuracy, epochs, updates };
}

function summarize(results: readonly EpisodeResult[]): Summary {
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  return {
    meanScore: mean(scores),
    medianScore:
      scores.length % 2 === 0
        ? (scores[scores.length / 2 - 1] + scores[scores.length / 2]) / 2
        : scores[Math.floor(scores.length / 2)],
    minimumScore: scores[0],
    maximumScore: scores[scores.length - 1],
    meanMoves: mean(results.map((result) => result.moves)),
    censored: results.filter((result) => result.censored).length,
  };
}

function printEpisode(prefix: string, result: EpisodeResult) {
  process.stderr.write(
    `${prefix} seed 0x${result.seed.toString(16).padStart(8, "0")} · ${result.score.toLocaleString("en-US")} · ${result.moves} moves · ${result.labels} labels · ${result.generatedStates.toLocaleString("en-US")} generated · ${(result.elapsedMs / 1_000).toFixed(3)}s\n`,
  );
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function exportModel(model: Model): SerializedLearnedPolicyWeights {
  // Pad the 32x16 training network into the runtime 64x32 artifact
  // shape. Active weights occupy the leading blocks; inference remains exact.
  const productionAccumulator = LEARNED_POLICY_ACCUMULATOR_SIZE;
  const productionHidden = LEARNED_POLICY_HIDDEN_SIZE;
  const embedding = new Float32Array(TOKEN_COUNT * productionAccumulator);
  for (let token = 0; token < TOKEN_COUNT; token += 1) {
    embedding.set(
      model.embedding.subarray(
        token * ACCUMULATOR_SIZE,
        (token + 1) * ACCUMULATOR_SIZE,
      ),
      token * productionAccumulator,
    );
  }
  const accumulatorBias = new Float32Array(productionAccumulator);
  accumulatorBias.set(model.accumulatorBias);
  const hiddenWeights = new Float32Array(productionHidden * productionAccumulator);
  for (let hidden = 0; hidden < HIDDEN_SIZE; hidden += 1) {
    hiddenWeights.set(
      model.hiddenWeights.subarray(
        hidden * ACCUMULATOR_SIZE,
        (hidden + 1) * ACCUMULATOR_SIZE,
      ),
      hidden * productionAccumulator,
    );
  }
  const hiddenBias = new Float32Array(productionHidden);
  hiddenBias.set(model.hiddenBias);
  const outputWeights = new Float32Array(BOARD_SIZE * productionHidden);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    outputWeights.set(
      model.outputWeights.subarray(
        column * HIDDEN_SIZE,
        (column + 1) * HIDDEN_SIZE,
      ),
      column * productionHidden,
    );
  }
  return {
    format: LEARNED_POLICY_FORMAT,
    version: LEARNED_POLICY_VERSION,
    embedding: [...embedding],
    accumulatorBias: [...accumulatorBias],
    hiddenWeights: [...hiddenWeights],
    hiddenBias: [...hiddenBias],
    outputWeights: [...outputWeights],
    outputBias: [...model.outputBias],
  };
}

export async function runExperiment(options: Options) {
  validateOptions(options);
  const examples: Example[] = [];
  const teacherResults: EpisodeResult[] = [];
  const teacherStarted = performance.now();
  for (let game = 0; game < options.oracleGames; game += 1) {
    const result = collectOracleEpisode(
      (TRAINING_SEED_START + game) >>> 0,
      options,
      examples,
    );
    teacherResults.push(result);
    printEpisode("oracle", result);
  }
  const teacherSeconds = (performance.now() - teacherStarted) / 1_000;
  const teacherGenerated = teacherResults.reduce(
    (sum, result) => sum + result.generatedStates,
    0,
  );
  const teacherSummary = summarize(teacherResults);
  process.stderr.write(
    `teacher corpus · ${examples.length} labels · mean ${Math.round(teacherSummary.meanScore).toLocaleString("en-US")} · ${teacherGenerated.toLocaleString("en-US")} generated · ${(teacherGenerated / teacherSeconds).toFixed(0)} states/s\n`,
  );
  if (teacherSummary.meanScore < 1_000_000) {
    throw new Error("Privileged oracle failed the 1M teacher gate");
  }

  const model = createModel();
  const optimizer = createOptimizer(model);
  const initialTraining = train(
    examples,
    model,
    optimizer,
    options.initialEpochs,
    options.learningRate,
    "imitation",
  );

  const daggerResults: EpisodeResult[] = [];
  for (let game = 0; game < options.daggerGames; game += 1) {
    const result = collectDaggerEpisode(
      (DAGGER_SEED_START + game) >>> 0,
      options,
      model,
      examples,
    );
    daggerResults.push(result);
    printEpisode("DAgger", result);
  }
  const daggerTraining = train(
    examples,
    model,
    optimizer,
    options.daggerEpochs,
    options.learningRate * 0.5,
    "DAgger",
  );

  // Lock the model before reading any probe seed below this line.
  const probeResults: EpisodeResult[] = [];
  for (let game = 0; game < options.probeGames; game += 1) {
    const result = evaluateStudentEpisode(
      (PROBE_SEED_START + game) >>> 0,
      options.maxMoves,
      model,
    );
    probeResults.push(result);
    printEpisode("probe", result);
  }
  const probeSummary = summarize(probeResults);
  const report = {
    format: "drop7-privileged-oracle-dagger",
    trainingSeeds: {
      oracleStart: `0x${TRAINING_SEED_START.toString(16)}`,
      oracleGames: options.oracleGames,
      daggerStart: `0x${DAGGER_SEED_START.toString(16)}`,
      daggerGames: options.daggerGames,
    },
    probeSeeds: {
      start: `0x${PROBE_SEED_START.toString(16)}`,
      games: options.probeGames,
      touchedOnlyAfterFreeze: true,
    },
    oracle: {
      depth: options.oracleDepth,
      beam: options.oracleBeam,
      summary: teacherSummary,
      scores: teacherResults.map((result) => result.score),
      labels: teacherResults.reduce((sum, result) => sum + result.labels, 0),
      generatedStates: teacherGenerated,
      seconds: teacherSeconds,
      statesPerSecond: teacherGenerated / teacherSeconds,
    },
    initialTraining,
    dagger: {
      summary: summarize(daggerResults),
      scores: daggerResults.map((result) => result.score),
      labels: daggerResults.reduce((sum, result) => sum + result.labels, 0),
      generatedStates: daggerResults.reduce(
        (sum, result) => sum + result.generatedStates,
        0,
      ),
      training: daggerTraining,
    },
    corpusExamples: examples.length,
    modelParameters:
      model.embedding.length +
      model.accumulatorBias.length +
      model.hiddenWeights.length +
      model.hiddenBias.length +
      model.outputWeights.length +
      model.outputBias.length,
    maxRssBytes: process.resourceUsage().maxRSS * 1024,
    probe: {
      summary: probeSummary,
      scores: probeResults.map((result) => result.score),
      moves: probeResults.map((result) => result.moves),
      qualified: probeSummary.meanScore >= 300_000,
    },
    heldoutRangesUntouched: ["0x5d700000+", "0x7d700000+", "0xd7000000+"],
  };
  if (options.output) {
    await writeFile(
      options.output,
      `${JSON.stringify({ report, model: exportModel(model) })}\n`,
      "utf8",
    );
  }
  process.stdout.write(`${JSON.stringify(report)}\n`);
  return report;
}

function validateOptions(options: Options) {
  for (const [key, value] of Object.entries(options)) {
    if (key === "output") continue;
    if (!Number.isFinite(value)) throw new Error(`${key} must be finite`);
  }
  for (const key of [
    "oracleGames",
    "daggerGames",
    "probeGames",
    "maxMoves",
    "oracleDepth",
    "oracleBeam",
    "initialEpochs",
    "daggerEpochs",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  if (options.oracleGames > 64 || options.daggerGames > 64 ||
      options.probeGames > 64 || options.maxMoves > 2_000 ||
      options.oracleDepth > 12 || options.oracleBeam > 2_048) {
    throw new Error("Experiment exceeds its bounded work limits");
  }
  if (options.learningRate <= 0 || options.daggerWeight <= 0) {
    throw new Error("Learning rate and DAgger weight must be positive");
  }
}

function integerArgument(arguments_: readonly string[], flag: string, fallback: number) {
  const index = arguments_.indexOf(flag);
  if (index < 0) return fallback;
  const value = Number(arguments_[index + 1]);
  if (!Number.isSafeInteger(value)) throw new Error(`${flag} must be an integer`);
  return value;
}

function numberArgument(arguments_: readonly string[], flag: string, fallback: number) {
  const index = arguments_.indexOf(flag);
  if (index < 0) return fallback;
  const value = Number(arguments_[index + 1]);
  if (!Number.isFinite(value)) throw new Error(`${flag} must be finite`);
  return value;
}

function stringArgument(arguments_: readonly string[], flag: string) {
  const index = arguments_.indexOf(flag);
  if (index < 0) return undefined;
  const value = arguments_[index + 1];
  if (!value) throw new Error(`${flag} requires a value`);
  return value;
}

function parseOptions(arguments_: readonly string[]): Options {
  return {
    oracleGames: integerArgument(arguments_, "--oracle-games", 8),
    daggerGames: integerArgument(arguments_, "--dagger-games", 8),
    probeGames: integerArgument(arguments_, "--probe-games", 16),
    maxMoves: integerArgument(arguments_, "--max-moves", 500),
    oracleDepth: integerArgument(arguments_, "--oracle-depth", 4),
    oracleBeam: integerArgument(arguments_, "--oracle-beam", 128),
    initialEpochs: integerArgument(arguments_, "--initial-epochs", 15),
    daggerEpochs: integerArgument(arguments_, "--dagger-epochs", 10),
    learningRate: numberArgument(arguments_, "--learning-rate", 0.001),
    daggerWeight: numberArgument(arguments_, "--dagger-weight", 3),
    output: stringArgument(arguments_, "--output"),
  };
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

function runSelfTest() {
  const model = createModel();
  const optimizer = createOptimizer(model);
  const first = initialState(TRAINING_SEED_START);
  const second = initialState(TRAINING_SEED_START + 1);
  const examples = [
    exampleFromState(first, 2, 1),
    exampleFromState(second, 4, 1),
  ];
  const before = datasetAccuracy(examples, model);
  const trained = train(examples, model, optimizer, 30, 0.002, "self-test");
  if (!(trained.loss < before.loss) || trained.accuracy < 0.5) {
    throw new Error("Student optimization self-test failed");
  }
  const state = initialState(TRAINING_SEED_START);
  const selected = chooseStudentColumn(state, model);
  const altered = { ...state, score: 999_999, level: 77, movesPlayed: 381 };
  if (chooseStudentColumn(altered, model) !== selected) {
    throw new Error("Student seed-blind self-test failed");
  }
  const mirrored = {
    ...state,
    board: Array.from({ length: BOARD_SIZE * BOARD_SIZE }, (_, index) => {
      const row = Math.floor(index / BOARD_SIZE);
      const column = index % BOARD_SIZE;
      return state.board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
    }),
  } satisfies GameState;
  const mirrorSelected = chooseStudentColumn(mirrored, model);
  if (selected !== null && mirrorSelected !== BOARD_SIZE - 1 - selected) {
    // Symmetric positions can only be strictly equivariant at the center.
    if (state.board.some((cell, index) => cell !== mirrored.board[index])) {
      throw new Error("Student mirror self-test failed");
    }
  }
  process.stdout.write(
    `SELF_TEST {"optimization":true,"seedBlind":true,"boundedExamples":${MAX_EXAMPLES},"parameters":${model.embedding.length + model.accumulatorBias.length + model.hiddenWeights.length + model.hiddenBias.length + model.outputWeights.length + model.outputBias.length}}\n`,
  );
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  if (process.argv.includes("--self-test")) runSelfTest();
  else await runExperiment(parseOptions(process.argv.slice(2)));
}
