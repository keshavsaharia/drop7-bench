import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
  LEARNED_EVALUATOR_FORMAT,
  LEARNED_EVALUATOR_HIDDEN_SIZE,
  LEARNED_EVALUATOR_VERSION,
  LEARNED_POLICY_ACCUMULATOR_SIZE,
  LEARNED_POLICY_FORMAT,
  LEARNED_POLICY_HIDDEN_SIZE,
  LEARNED_POLICY_VERSION,
  compileLearnedEvaluatorWeights,
  compileLearnedPolicyWeights,
  createRandomLearnedEvaluatorWeights,
  createZeroLearnedPolicyWeights,
  evaluateLearnedPolicy,
  evaluateLearnedPosition,
  extractLearnedEvaluatorTokens,
  extractLearnedPolicyTokens,
  validateSerializedLearnedEvaluatorWeights,
  validateSerializedLearnedPolicyWeights,
  type CompiledLearnedPolicyWeights,
  type LearnedEvaluatorPosition,
  type LearnedPolicyPosition,
  type SerializedLearnedPolicyWeights,
  type SerializedLearnedEvaluatorWeights,
} from "../../../src/core/typescript/learned-evaluator.ts";
import { evaluateRolloutMoves } from "../../../src/core/typescript/rollout-solver.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import { planOracleMove } from "../../oracle-curriculum/perfect-information-oracle/main.ts";

/** These disjoint ranges stay fixed so checkpoints are directly comparable. */
export const TRAINING_SEED_START = 0x1d70_0000;
export const CALIBRATION_SEED_START = 0x5d70_0000;
export const VALIDATION_SEED_START = 0x7d70_0000;

const MAX_GAMES = 100_000;
const MAX_POLICY_SAMPLES = 32;
const TARGET_SCALE = 10_000;
const HUBER_DELTA = 10;
const GRADIENT_CLIP = 100;
const ACTION_LABEL_SMOOTHING = 0.1;
const ORACLE_RELU_BIAS_FLOOR = 0.05;
const ADAM_BETA_ONE = 0.9;
const ADAM_BETA_TWO = 0.999;
const ADAM_EPSILON = 1e-8;
const MODEL_INITIALIZATION_SEED = 0xd707_2026;
const MODEL_INITIALIZATION_SCALE = 0.025;
const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const POLICY_REVEAL_DOMAIN = 0x5052_4f42;
const POLICY_EXPLORATION_DOMAIN = 0x4558_504c;
const POLICY_CHOICE_DOMAIN = 0x4348_4f49;
const SHUFFLE_DOMAIN = 0x5348_5546;
const FITTED_REVEAL_DOMAIN = 0x4652_564c;
const FITTED_DISC_DOMAIN = 0x4644_4953;
const FITTED_POLICY_DOMAIN = 0x4650_4f4c;
const MOVE_MULTIPLIER = 0x85eb_ca6b;
const SAMPLE_MULTIPLIER = 0x9e37_79b9;
const COLUMN_MULTIPLIER = 0xc2b2_ae35;
const FITTED_TARGET_LIMIT = 250_000;
const MAX_FITTED_HORIZON = 80;
const MAX_STATES_PER_GAME = 100;
const VALIDATION_ROLLOUTS = 4;
const VALIDATION_ROLLOUT_HORIZON = 12;

const CENTER_FIRST_COLUMNS = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_CENTER_FIRST_COLUMNS = [3, 4, 2, 5, 1, 6, 0] as const;

export interface TrainingArguments {
  games: number;
  policySamples: number;
  warmupGames: number;
  rampGames: number;
  learningRate: number;
  terminalUtility: number;
  maxMoves: number;
  epsilon: number;
  validationGames: number;
  calibrationGames: number;
  validateEvery: number;
  teacher: "shape" | "oracle" | "fitted" | "contrastive";
  oracleDepth: number;
  oracleBeam: number;
  fittedHorizon: number;
  statesPerGame: number;
  iterations: number;
  epochs: number;
  contrastiveMargin: number;
  contrastiveTemperature: number;
  validateRollout: boolean;
  validateSparse: boolean;
  evaluateOnly: boolean;
  exportLast: boolean;
  outputPath: string;
  resumePath?: string;
  selfTest: boolean;
}

interface TrainableModel {
  embedding: Float32Array;
  accumulatorBias: Float32Array;
  hiddenWeights: Float32Array;
  hiddenBias: Float32Array;
  outputWeights: Float32Array;
  outputBias: number;
}

interface TrainablePolicyModel {
  embedding: Float32Array;
  accumulatorBias: Float32Array;
  hiddenWeights: Float32Array;
  hiddenBias: Float32Array;
  outputWeights: Float32Array;
  outputBias: Float32Array;
}

interface VectorMoments {
  first: Float32Array;
  second: Float32Array;
}

interface AdamState {
  step: number;
  betaOnePower: number;
  betaTwoPower: number;
  embedding: VectorMoments;
  accumulatorBias: VectorMoments;
  hiddenWeights: VectorMoments;
  hiddenBias: VectorMoments;
  outputWeights: VectorMoments;
  outputBiasFirst: number;
  outputBiasSecond: number;
}

interface PolicyAdamState {
  betaOnePower: number;
  betaTwoPower: number;
  embedding: VectorMoments;
  accumulatorBias: VectorMoments;
  hiddenWeights: VectorMoments;
  hiddenBias: VectorMoments;
  outputWeights: VectorMoments;
  outputBias: VectorMoments;
}

interface ForwardPass {
  tokenIds: Uint16Array;
  accumulatorBeforeRelu: Float32Array;
  accumulator: Float32Array;
  hiddenBeforeRelu: Float32Array;
  hidden: Float32Array;
  /** Prediction in units of TARGET_SCALE. */
  output: number;
}

interface PolicyForwardPass {
  tokenIds: Uint16Array;
  mirrored: boolean;
  accumulatorBeforeRelu: Float32Array;
  accumulator: Float32Array;
  hiddenBeforeRelu: Float32Array;
  hidden: Float32Array;
  canonicalLogits: Float32Array;
}

interface EpisodeStep {
  /** score is normalized to zero: training never sees points earned earlier. */
  state: GameState;
  position: LearnedEvaluatorPosition;
  teacherColumn: number;
}

interface EpisodeResult {
  score: number;
  moves: number;
  completed: boolean;
  trajectory?: readonly EpisodeStep[];
}

interface FittedExample {
  position: LearnedEvaluatorPosition;
  target: number;
}

interface ContrastivePair {
  better: LearnedEvaluatorPosition;
  worse: LearnedEvaluatorPosition;
}

interface PolicyOptions {
  samples: number;
  learnedFraction: number;
  baseline: "combined" | "teacher";
  /** Common-random-number probes reduce noise while collecting train states. */
  commonActionSamples: boolean;
  epsilon: number;
  terminalUtility: number;
}

interface ValidationSide {
  meanScore: number;
  meanMoves: number;
  completedGames: number;
}

interface ValidationResult {
  learned: ValidationSide;
  baseline: ValidationSide;
  teacher: ValidationSide;
  pairedMeanScoreDelta: number;
  pairedMeanMoveDelta: number;
}

interface DirectPolicyValidationReference {
  baselineResults: readonly EpisodeResult[];
  teacherResults: readonly EpisodeResult[];
}

export function parseTrainingArguments(
  arguments_: readonly string[],
): TrainingArguments | null {
  let games = 150;
  let policySamples = 3;
  let warmupGames = 25;
  let rampGames = 75;
  let learningRate = 0.001;
  let terminalUtility = -250_000;
  let maxMoves = 500;
  let epsilon = 0.08;
  let validationGames = 64;
  let calibrationGames = 32;
  let validateEvery = 25;
  let teacher: TrainingArguments["teacher"] = "shape";
  let oracleDepth = 4;
  let oracleBeam = 128;
  let fittedHorizon = 40;
  let statesPerGame = 8;
  let iterations = 2;
  let epochs = 2;
  let contrastiveMargin = 10_000;
  let contrastiveTemperature = 25_000;
  let validateRollout = false;
  let validateSparse = false;
  let evaluateOnly = false;
  let exportLast = false;
  let outputPath = "drop7-value-model.json";
  let resumePath: string | undefined;
  let selfTest = false;

  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--self-test") {
      selfTest = true;
      continue;
    }
    if (flag === "--export-last") {
      exportLast = true;
      continue;
    }
    if (flag === "--validate-rollout") {
      validateRollout = true;
      continue;
    }
    if (flag === "--validate-sparse") {
      validateSparse = true;
      continue;
    }
    if (flag === "--evaluate-only") {
      evaluateOnly = true;
      continue;
    }

    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`Missing value after ${flag}`);
    index += 1;

    switch (flag) {
      case "--games":
        games = parseBoundedPositiveInteger(value, flag, MAX_GAMES);
        break;
      case "--samples":
        policySamples = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_POLICY_SAMPLES,
        );
        break;
      case "--warmup":
        warmupGames = parseNonNegativeInteger(value, flag);
        break;
      case "--ramp":
        rampGames = parseNonNegativeInteger(value, flag);
        break;
      case "--lr":
        learningRate = parsePositiveFinite(value, flag);
        if (learningRate > 1) throw new Error("--lr cannot exceed 1");
        break;
      case "--terminal":
        terminalUtility = parseFinite(value, flag);
        if (terminalUtility >= 0) {
          throw new Error("--terminal must be negative");
        }
        break;
      case "--max-moves":
        maxMoves = parseBoundedPositiveInteger(value, flag, 100_000);
        break;
      case "--epsilon":
        epsilon = parseFinite(value, flag);
        if (epsilon < 0 || epsilon > 1) {
          throw new Error("--epsilon must be between 0 and 1");
        }
        break;
      case "--validation-games":
        validationGames = parseBoundedPositiveInteger(
          value,
          flag,
          1_000,
        );
        break;
      case "--calibration-games":
        calibrationGames = parseBoundedPositiveInteger(
          value,
          flag,
          1_000,
        );
        break;
      case "--validate-every":
        validateEvery = parseBoundedPositiveInteger(value, flag, MAX_GAMES);
        break;
      case "--teacher":
        if (
          value !== "shape" &&
          value !== "oracle" &&
          value !== "fitted" &&
          value !== "contrastive"
        ) {
          throw new Error(
            "--teacher must be shape, oracle, fitted, or contrastive",
          );
        }
        teacher = value;
        break;
      case "--oracle-depth":
        oracleDepth = parseBoundedPositiveInteger(value, flag, 20);
        break;
      case "--oracle-beam":
        oracleBeam = parseBoundedPositiveInteger(value, flag, 100_000);
        break;
      case "--fitted-horizon":
        fittedHorizon = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_FITTED_HORIZON,
        );
        break;
      case "--states-per-game":
        statesPerGame = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_STATES_PER_GAME,
        );
        break;
      case "--iterations":
        iterations = parseBoundedPositiveInteger(value, flag, 20);
        break;
      case "--epochs":
        epochs = parseBoundedPositiveInteger(value, flag, 100);
        break;
      case "--contrastive-margin":
        contrastiveMargin = parseFinite(value, flag);
        if (contrastiveMargin < 0) {
          throw new Error("--contrastive-margin must be non-negative");
        }
        break;
      case "--contrastive-temperature":
        contrastiveTemperature = parsePositiveFinite(value, flag);
        break;
      case "--model":
      case "--model-output":
      case "--output":
        outputPath = value;
        break;
      case "--resume":
        resumePath = value;
        break;
      default:
        throw new Error(`Unknown option ${flag}`);
    }
  }

  return {
    games,
    policySamples,
    warmupGames,
    rampGames,
    learningRate,
    terminalUtility,
    maxMoves,
    epsilon,
    validationGames,
    calibrationGames,
    validateEvery,
    teacher,
    oracleDepth,
    oracleBeam,
    fittedHorizon,
    statesPerGame,
    iterations,
    epochs,
    contrastiveMargin,
    contrastiveTemperature,
    validateRollout,
    validateSparse,
    evaluateOnly,
    exportLast,
    outputPath,
    ...(resumePath === undefined ? {} : { resumePath }),
    selfTest,
  };
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseTrainingArguments(arguments_);
  if (options === null) {
    process.stdout.write(helpText());
    return;
  }
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  if (options.evaluateOnly) {
    await runEvaluationOnly(options);
    return;
  }
  if (options.teacher === "contrastive") {
    await runContrastiveTraining(options);
    return;
  }
  if (options.teacher === "fitted") {
    await runFittedValueTraining(options);
    return;
  }
  if (options.teacher === "oracle") {
    await runOraclePolicyTraining(options);
    return;
  }

  const model = options.resumePath
    ? await loadTrainableModel(options.resumePath)
    : createInitialTrainableModel();
  const optimizer = createAdamState(model);
  let bestModel = createBaselineResidualModel(model);
  let bestValidationDelta = 0;
  let bestCheckpoint = 0;
  let completedGames = 0;
  let cappedGames = 0;
  let trainedPositions = 0;
  let cumulativeLoss = 0;

  process.stderr.write(
    `training ${options.games} ${options.teacher} teacher games from seeds ${formatSeed(TRAINING_SEED_START)}..${formatSeed(TRAINING_SEED_START + options.games - 1)}; validation uses fixed seeds ${formatSeed(VALIDATION_SEED_START)}..${formatSeed(VALIDATION_SEED_START + options.validationGames - 1)}\n`,
  );

  for (let gameIndex = 0; gameIndex < options.games; gameIndex += 1) {
    const learnedFraction = policyLearnedFraction(
      gameIndex,
      options.warmupGames,
      options.rampGames,
    );
    const seed = (TRAINING_SEED_START + gameIndex) >>> 0;
    const episode = runEpisode(
      seed,
      model,
      {
        samples: options.policySamples,
        learnedFraction,
        baseline: "teacher",
        commonActionSamples: true,
        epsilon: options.epsilon,
        terminalUtility: options.terminalUtility,
      },
      options.maxMoves,
      true,
    );

    if (episode.completed) completedGames += 1;
    else cappedGames += 1;

    if ((episode.trajectory?.length ?? 0) > 0) {
      const result = trainCompletedEpisode(
        episode.trajectory ?? [],
        seed,
        options.policySamples,
        model,
        optimizer,
        options.learningRate,
      );
      trainedPositions += result.positions;
      cumulativeLoss += result.loss;
    }

    const gamesRun = gameIndex + 1;
    if (
      gamesRun % options.validateEvery === 0 ||
      gamesRun === options.games
    ) {
      const validation = validatePolicy(model, options);
      if (validation.pairedMeanScoreDelta > bestValidationDelta) {
        bestModel = cloneTrainableModel(model);
        bestValidationDelta = validation.pairedMeanScoreDelta;
        bestCheckpoint = gamesRun;
      }
      const meanLoss =
        trainedPositions === 0 ? null : cumulativeLoss / trainedPositions;
      process.stderr.write(
        `${formatTrainingProgress(gamesRun, completedGames, cappedGames, trainedPositions, meanLoss, learnedFraction)}\n`,
      );
      process.stderr.write(`${formatValidation(validation, options)}\n`);
      process.stderr.write(
        `checkpoint · best ${bestCheckpoint} games · paired score delta ${formatSignedInteger(bestValidationDelta)}\n`,
      );
    }
  }

  if (trainedPositions === 0) {
    throw new Error(
      "No training positions were generated. Increase --max-moves.",
    );
  }

  const exportedModel = options.exportLast ? model : bestModel;
  const exportedCheckpoint = options.exportLast
    ? options.games
    : bestCheckpoint;
  const artifact = exportModel(exportedModel);
  await writeModel(options.outputPath, artifact);
  const summary = `${completedGames} complete games · ${cappedGames} capped · ${trainedPositions} positions · exported checkpoint ${exportedCheckpoint} (${formatSignedInteger(bestValidationDelta)} best paired validation score)\n`;
  if (options.outputPath === "-") {
    // Keep stdout valid JSON for shell pipelines.
    process.stderr.write(summary);
  } else {
    process.stdout.write(`wrote ${resolve(options.outputPath)} · ${summary}`);
  }
}

async function runOraclePolicyTraining(options: TrainingArguments) {
  const model = options.resumePath
    ? await loadTrainablePolicyModel(options.resumePath)
    : createInitialTrainablePolicyModel();
  const optimizer = createPolicyAdamState(model);
  let bestModel = cloneTrainablePolicyModel(model);
  let bestValidationDelta = Number.NEGATIVE_INFINITY;
  let bestCheckpoint = 0;
  let completedGames = 0;
  let cappedGames = 0;
  let trainedPositions = 0;
  const trajectory: EpisodeStep[] = [];

  process.stderr.write(
    `training ${options.games} oracle policy games (depth ${options.oracleDepth}, beam ${options.oracleBeam}) from seeds ${formatSeed(TRAINING_SEED_START)}..${formatSeed(TRAINING_SEED_START + options.games - 1)}; fair validation uses ${formatSeed(VALIDATION_SEED_START)}..${formatSeed(VALIDATION_SEED_START + options.validationGames - 1)}\n`,
  );

  for (let gameIndex = 0; gameIndex < options.games; gameIndex += 1) {
    const seed = (TRAINING_SEED_START + gameIndex) >>> 0;
    const episode = runOracleEpisode(
      seed,
      options.oracleDepth,
      options.oracleBeam,
      options.maxMoves,
    );
    if (episode.completed) completedGames += 1;
    else cappedGames += 1;
    trajectory.push(...(episode.trajectory ?? []));
  }

  if (trajectory.length === 0) {
    throw new Error("The oracle generated no policy labels");
  }

  // Train epochs over the entire corpus rather than exhausting one game at a
  // time. Global shuffles avoid catastrophic bias toward the final seed and
  // make each oracle position equally likely to influence a checkpoint.
  const validationReference = createDirectPolicyValidationReference(options);
  for (let epoch = 0; epoch < options.epochs; epoch += 1) {
    const result = trainPolicyEpisode(
      trajectory,
      mix32(TRAINING_SEED_START ^ Math.imul(epoch + 1, SHUFFLE_DOMAIN)),
      1,
      model,
      optimizer,
      options.learningRate,
    );
    trainedPositions += result.positions;
    const training = summarizePolicyLabels(trajectory, model);
    const validation = validateDirectPolicy(
      model,
      options,
      validationReference,
    );
    if (validation.pairedMeanScoreDelta > bestValidationDelta) {
      bestModel = cloneTrainablePolicyModel(model);
      bestValidationDelta = validation.pairedMeanScoreDelta;
      bestCheckpoint = epoch + 1;
    }
    process.stderr.write(
      `epoch ${epoch + 1}/${options.epochs} · updates ${trainedPositions} · train CE ${training.crossEntropy.toFixed(5)} · oracle agreement ${(training.accuracy * 100).toFixed(1)}%\n`,
    );
    process.stderr.write(`${formatValidation(validation, options)}\n`);
    process.stderr.write(
      `checkpoint · best epoch ${bestCheckpoint} · paired score delta ${formatSignedInteger(bestValidationDelta)}\n`,
    );
  }

  const exportedModel = options.exportLast ? model : bestModel;
  const exportedCheckpoint = options.exportLast
    ? options.epochs
    : bestCheckpoint;
  const artifact = exportPolicyModel(exportedModel);
  await writePolicyModel(options.outputPath, artifact);
  const summary = `${completedGames} complete games · ${cappedGames} capped · ${trajectory.length} labels · ${trainedPositions} updates · exported epoch ${exportedCheckpoint} (${formatSignedInteger(bestValidationDelta)} best paired validation score)\n`;
  if (options.outputPath === "-") process.stderr.write(summary);
  else process.stdout.write(`wrote ${resolve(options.outputPath)} · ${summary}`);
}

async function runFittedValueTraining(options: TrainingArguments) {
  const model = options.resumePath
    ? await loadTrainableModel(options.resumePath)
    : createInitialTrainableModel();
  const optimizer = createAdamState(model);
  let bestModel = createBaselineResidualModel(model);
  let bestTrainingDelta = 0;
  let bestIteration = 0;
  let behaviorFraction = 0;
  let totalExamples = 0;
  let totalUpdates = 0;

  process.stderr.write(
    `fitted value iteration · ${options.games} fair training seeds ${formatSeed(TRAINING_SEED_START)}..${formatSeed(TRAINING_SEED_START + options.games - 1)} · ${options.statesPerGame} states/game · ${options.policySamples} common samples · ${options.fittedHorizon}-ply continuations\n`,
  );

  for (let iteration = 0; iteration < options.iterations; iteration += 1) {
    const examples: FittedExample[] = [];
    let completedGames = 0;
    let cappedGames = 0;

    for (let gameIndex = 0; gameIndex < options.games; gameIndex += 1) {
      const seed = (TRAINING_SEED_START + gameIndex) >>> 0;
      const episode = runEpisode(
        seed,
        model,
        {
          samples: options.policySamples,
          learnedFraction: behaviorFraction,
          baseline: "combined",
          commonActionSamples: true,
          epsilon: options.epsilon,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
        true,
      );
      if (episode.completed) completedGames += 1;
      else cappedGames += 1;
      for (const step of evenlySpacedSteps(
        episode.trajectory ?? [],
        options.statesPerGame,
      )) {
        examples.push(
          ...estimateFittedExamples(
            step.state,
            seed,
            model,
            behaviorFraction,
            options,
          ),
        );
      }
    }

    if (examples.length === 0) {
      throw new Error("Fitted value iteration generated no examples");
    }
    totalExamples += examples.length;
    let iterationLoss = 0;
    let iterationUpdates = 0;
    for (let epoch = 0; epoch < options.epochs; epoch += 1) {
      const order = Array.from(
        { length: examples.length },
        (_, index) => index,
      );
      shuffle(
        order,
        seededRandom(
          mix32(
            TRAINING_SEED_START ^
              Math.imul(iteration + 1, MOVE_MULTIPLIER) ^
              Math.imul(epoch + 1, SAMPLE_MULTIPLIER),
          ),
        ),
      );
      for (const index of order) {
        const example = examples[index];
        iterationLoss += trainExample(
          example.position,
          example.target,
          model,
          optimizer,
          options.learningRate,
        );
        iterationUpdates += 1;
      }
    }
    totalUpdates += iterationUpdates;

    const previousBehaviorFraction = behaviorFraction;
    const calibration = calibrateResidualScale(model, options);
    behaviorFraction = calibration.scale;
    if (calibration.pairedMeanScoreDelta > bestTrainingDelta) {
      bestModel = scaledResidualModel(model, behaviorFraction);
      bestTrainingDelta = calibration.pairedMeanScoreDelta;
      bestIteration = iteration + 1;
    }
    const targets = examples.map((example) => example.target);
    process.stderr.write(
      `iteration ${iteration + 1}/${options.iterations} · behavior residual ${(previousBehaviorFraction * 100).toFixed(1)}% · ${completedGames} complete / ${cappedGames} capped · ${examples.length} examples · target |mean| ${Math.round(mean(targets.map(Math.abs))).toLocaleString("en-US")} / max ${Math.round(Math.max(...targets.map(Math.abs))).toLocaleString("en-US")} · loss ${(iterationLoss / iterationUpdates).toFixed(5)}\n`,
    );
    process.stderr.write(
      `training calibration · residual ${(behaviorFraction * 100).toFixed(1)}% · ${formatValidationSide(calibration.learned, options.games)} vs combined ${formatValidationSide(calibration.baseline, options.games)} · paired delta ${formatSignedInteger(calibration.pairedMeanScoreDelta)} · best iteration ${bestIteration}\n`,
    );
  }

  const exportedModel = options.exportLast
    ? scaledResidualModel(model, behaviorFraction)
    : bestModel;
  const exportedIteration = options.exportLast
    ? options.iterations
    : bestIteration;
  const validation = validatePolicy(exportedModel, options);
  process.stderr.write(`held-out ${formatValidation(validation, options)}\n`);
  if (options.validateRollout) {
    const rolloutValidation = validateRolloutLeaf(exportedModel, options);
    process.stderr.write(
      `short rollout validation · learned ${formatValidationSide(rolloutValidation.learned, options.validationGames)} · combined ${formatValidationSide(rolloutValidation.baseline, options.validationGames)} · paired delta ${formatSignedInteger(rolloutValidation.pairedMeanScoreDelta)} score / ${rolloutValidation.pairedMeanMoveDelta >= 0 ? "+" : ""}${rolloutValidation.pairedMeanMoveDelta.toFixed(1)} moves\n`,
    );
  }

  const artifact = exportModel(exportedModel);
  await writeModel(options.outputPath, artifact);
  const summary = `${totalExamples} examples · ${totalUpdates} updates · exported iteration ${exportedIteration} (${formatSignedInteger(bestTrainingDelta)} training calibration; ${formatSignedInteger(validation.pairedMeanScoreDelta)} held-out one-ply score)\n`;
  if (options.outputPath === "-") process.stderr.write(summary);
  else process.stdout.write(`wrote ${resolve(options.outputPath)} · ${summary}`);
}

async function runContrastiveTraining(options: TrainingArguments) {
  const model = options.resumePath
    ? await loadTrainableModel(options.resumePath)
    : createInitialTrainableModel();
  const optimizer = createAdamState(model);
  const baselineModel = createBaselineResidualModel(model);
  const pairs: ContrastivePair[] = [];
  let oracleCompleted = 0;
  let baselineCompleted = 0;

  process.stderr.write(
    `contrastive state training · ${options.games} collection seeds ${formatSeed(TRAINING_SEED_START)}..${formatSeed(TRAINING_SEED_START + options.games - 1)} · ${options.calibrationGames} disjoint calibration seeds ${formatSeed(CALIBRATION_SEED_START)}..${formatSeed(CALIBRATION_SEED_START + options.calibrationGames - 1)} · depth-${options.oracleDepth}/beam-${options.oracleBeam} positives · combined-policy negatives · up to ${options.statesPerGame} matched states/game\n`,
  );

  for (let gameIndex = 0; gameIndex < options.games; gameIndex += 1) {
    const seed = (TRAINING_SEED_START + gameIndex) >>> 0;
    const oracle = runOracleEpisode(
      seed,
      options.oracleDepth,
      options.oracleBeam,
      options.maxMoves,
    );
    const baseline = runEpisode(
      seed,
      baselineModel,
      {
        samples: options.policySamples,
        learnedFraction: 0,
        baseline: "combined",
        commonActionSamples: false,
        epsilon: 0,
        terminalUtility: options.terminalUtility,
      },
      options.maxMoves,
      true,
    );
    if (oracle.completed) oracleCompleted += 1;
    if (baseline.completed) baselineCompleted += 1;
    pairs.push(
      ...matchedTrajectoryPairs(
        oracle.trajectory ?? [],
        baseline.trajectory ?? [],
        options.statesPerGame,
      ),
    );
  }

  if (pairs.length === 0) {
    throw new Error("No distinct matched oracle/baseline states were generated");
  }

  let bestModel = createBaselineResidualModel(model);
  let bestTrainingDelta = 0;
  let bestEpoch = 0;
  let totalUpdates = 0;
  const initialRanking = summarizeContrastivePairs(model, pairs);
  process.stderr.write(
    `corpus · ${pairs.length} pairs · oracle ${oracleCompleted}/${options.games} terminal · combined ${baselineCompleted}/${options.games} terminal · initial ordering ${(initialRanking.accuracy * 100).toFixed(1)}% / ${formatSignedInteger(initialRanking.meanGap)} mean gap\n`,
  );

  for (let epoch = 0; epoch < options.epochs; epoch += 1) {
    const order = Array.from({ length: pairs.length }, (_, index) => index);
    shuffle(
      order,
      seededRandom(
        mix32(
          TRAINING_SEED_START ^
            Math.imul(epoch + 1, SHUFFLE_DOMAIN),
        ),
      ),
    );
    let loss = 0;
    for (const index of order) {
      loss += trainContrastivePair(
        pairs[index],
        model,
        optimizer,
        options.learningRate,
        options.contrastiveMargin,
        options.contrastiveTemperature,
      );
      totalUpdates += 1;
    }
    const ranking = summarizeContrastivePairs(model, pairs);
    const calibration = calibrateResidualScale(
      model,
      options,
      Array.from(
        { length: options.calibrationGames },
        (_, index) => (CALIBRATION_SEED_START + index) >>> 0,
      ),
    );
    if (calibration.pairedMeanScoreDelta > bestTrainingDelta) {
      bestModel = scaledResidualModel(model, calibration.scale);
      bestTrainingDelta = calibration.pairedMeanScoreDelta;
      bestEpoch = epoch + 1;
    }
    process.stderr.write(
      `epoch ${epoch + 1}/${options.epochs} · loss ${(loss / pairs.length).toFixed(5)} · oracle ordering ${(ranking.accuracy * 100).toFixed(1)}% · mean gap ${formatSignedInteger(ranking.meanGap)} · scale ${(calibration.scale * 100).toFixed(1)}% · training score delta ${formatSignedInteger(calibration.pairedMeanScoreDelta)} · best epoch ${bestEpoch}\n`,
    );
  }

  const exportedModel = options.exportLast ? model : bestModel;
  const exportedEpoch = options.exportLast ? options.epochs : bestEpoch;
  const validation = validatePolicy(exportedModel, options);
  process.stderr.write(`held-out ${formatValidation(validation, options)}\n`);
  if (options.validateRollout) {
    const rolloutValidation = validateRolloutLeaf(exportedModel, options);
    process.stderr.write(
      `established rollout validation · learned ${formatValidationSide(rolloutValidation.learned, options.validationGames)} · combined ${formatValidationSide(rolloutValidation.baseline, options.validationGames)} · paired delta ${formatSignedInteger(rolloutValidation.pairedMeanScoreDelta)} score / ${rolloutValidation.pairedMeanMoveDelta >= 0 ? "+" : ""}${rolloutValidation.pairedMeanMoveDelta.toFixed(1)} moves\n`,
    );
  }
  if (options.validateSparse && validation.pairedMeanScoreDelta > 0) {
    const trainingSparse = validateSparseLeaf(
      exportedModel,
      options,
      Array.from(
        { length: 8 },
        (_, index) => (TRAINING_SEED_START + index) >>> 0,
      ),
    );
    const validationSparse = validateSparseLeaf(
      exportedModel,
      options,
      Array.from(
        { length: 16 },
        (_, index) => (VALIDATION_SEED_START + index) >>> 0,
      ),
    );
    process.stderr.write(
      `sparse d3/s4 training · learned ${formatValidationSide(trainingSparse.learned, 8)} · combined ${formatValidationSide(trainingSparse.baseline, 8)} · delta ${formatSignedInteger(trainingSparse.pairedMeanScoreDelta)}\n`,
    );
    process.stderr.write(
      `sparse d3/s4 validation · learned ${formatValidationSide(validationSparse.learned, 16)} · combined ${formatValidationSide(validationSparse.baseline, 16)} · delta ${formatSignedInteger(validationSparse.pairedMeanScoreDelta)}\n`,
    );
  }

  const artifact = exportModel(exportedModel);
  await writeModel(options.outputPath, artifact);
  const summary = `${pairs.length} matched pairs · ${totalUpdates} updates · exported epoch ${exportedEpoch} (${formatSignedInteger(bestTrainingDelta)} training calibration; ${formatSignedInteger(validation.pairedMeanScoreDelta)} held-out one-ply score)\n`;
  if (options.outputPath === "-") process.stderr.write(summary);
  else process.stdout.write(`wrote ${resolve(options.outputPath)} · ${summary}`);
}

async function runEvaluationOnly(options: TrainingArguments) {
  if (!options.resumePath) {
    throw new Error("--evaluate-only requires --resume PATH");
  }
  const model = await loadTrainableModel(options.resumePath);
  const validation = validatePolicy(model, options);
  process.stdout.write(`frozen ${formatValidation(validation, options)}\n`);
  if (options.validateRollout) {
    const rollout = validateRolloutLeaf(model, options);
    process.stdout.write(
      `frozen 4x12 rollout · learned ${formatValidationSide(rollout.learned, options.validationGames)} · combined ${formatValidationSide(rollout.baseline, options.validationGames)} · delta ${formatSignedInteger(rollout.pairedMeanScoreDelta)}\n`,
    );
  }
  if (options.validateSparse) {
    const training = validateSparseLeaf(
      model,
      options,
      Array.from(
        { length: 8 },
        (_, index) => (TRAINING_SEED_START + index) >>> 0,
      ),
    );
    const heldOut = validateSparseLeaf(
      model,
      options,
      Array.from(
        { length: 16 },
        (_, index) => (VALIDATION_SEED_START + index) >>> 0,
      ),
    );
    process.stdout.write(
      `frozen sparse d3/s4 training · learned ${formatValidationSide(training.learned, 8)} · combined ${formatValidationSide(training.baseline, 8)} · delta ${formatSignedInteger(training.pairedMeanScoreDelta)}\n`,
    );
    process.stdout.write(
      `frozen sparse d3/s4 validation · learned ${formatValidationSide(heldOut.learned, 16)} · combined ${formatValidationSide(heldOut.baseline, 16)} · delta ${formatSignedInteger(heldOut.pairedMeanScoreDelta)}\n`,
    );
  }
}

/**
 * Small deterministic optimizer/export check. It intentionally uses synthetic
 * values so it completes quickly while exercising sparse gradients and export.
 */
export function runSelfTest() {
  const model = createInitialTrainableModel();
  const optimizer = createAdamState(model);
  const examples = smokeExamples();
  const before = meanExampleLoss(model, examples);

  for (let epoch = 0; epoch < 360; epoch += 1) {
    for (const example of examples) {
      trainExample(
        example.position,
        example.target,
        model,
        optimizer,
        0.01,
      );
    }
  }

  const after = meanExampleLoss(model, examples);
  if (!(after < before * 0.5)) {
    throw new Error(
      `self-test loss did not fall enough: ${before} -> ${after}`,
    );
  }

  const artifact = exportModel(model);
  const roundTripped: unknown = JSON.parse(JSON.stringify(artifact)) as unknown;
  validateSerializedLearnedEvaluatorWeights(roundTripped);
  const compiled = compileLearnedEvaluatorWeights(roundTripped);
  const position = examples[1].position;
  const mirror = {
    board: mirrorBoard(position.board),
    movesRemaining: position.movesRemaining,
  };
  const forward = evaluateLearnedPosition(position, compiled);
  const reflected = evaluateLearnedPosition(mirror, compiled);
  if (!Number.isFinite(forward)) {
    throw new Error("self-test exported inference was not finite");
  }
  if (forward !== reflected) {
    throw new Error(
      `self-test mirror mismatch: ${forward} !== ${reflected}`,
    );
  }

  const determinismOptions: PolicyOptions = {
    samples: 1,
    learnedFraction: 0,
    baseline: "combined",
    commonActionSamples: true,
    epsilon: 0.1,
    terminalUtility: -250_000,
  };
  const first = runEpisode(VALIDATION_SEED_START, model, determinismOptions, 6);
  const second = runEpisode(VALIDATION_SEED_START, model, determinismOptions, 6);
  if (first.score !== second.score || first.moves !== second.moves) {
    throw new Error("self-test headless streams were not deterministic");
  }

  const policyModel = createInitialTrainablePolicyModel();
  const policyOptimizer = createPolicyAdamState(policyModel);
  const policyExamples = smokePolicyExamples();
  const policyBefore = summarizePolicyLabels(
    policyExamples,
    policyModel,
  ).crossEntropy;
  trainPolicyEpisode(
    policyExamples,
    MODEL_INITIALIZATION_SEED,
    200,
    policyModel,
    policyOptimizer,
    0.01,
  );
  const policyAfter = summarizePolicyLabels(
    policyExamples,
    policyModel,
  ).crossEntropy;
  if (!(policyAfter < policyBefore * 0.25)) {
    throw new Error(
      `self-test policy loss did not fall enough: ${policyBefore} -> ${policyAfter}`,
    );
  }
  const policyArtifact = exportPolicyModel(policyModel);
  const compiledPolicy = compileLearnedPolicyWeights(
    JSON.parse(JSON.stringify(policyArtifact)) as unknown,
  );
  const policyState = policyExamples[1].state;
  const policyForward = evaluateLearnedPolicy(policyState, compiledPolicy);
  const policyMirror = evaluateLearnedPolicy(
    { ...policyState, board: mirrorBoard(policyState.board) },
    compiledPolicy,
  );
  if (
    policyForward.bestColumn === null ||
    policyMirror.bestColumn === null ||
    policyForward.bestColumn + policyMirror.bestColumn !== BOARD_SIZE - 1 ||
    [...policyForward.logits].some(
      (logit, column) =>
        logit !== policyMirror.logits[BOARD_SIZE - 1 - column],
    )
  ) {
    throw new Error("self-test direct policy was not mirror equivariant");
  }

  const fittedOptions = parseTrainingArguments([
    "--teacher",
    "fitted",
    "--samples",
    "2",
    "--fitted-horizon",
    "2",
  ]);
  if (fittedOptions === null) {
    throw new Error("self-test could not construct fitted options");
  }
  const fittedFirst = estimateFittedExamples(
    policyExamples[0].state,
    MODEL_INITIALIZATION_SEED,
    model,
    0,
    fittedOptions,
  );
  const fittedSecond = estimateFittedExamples(
    policyExamples[0].state,
    MODEL_INITIALIZATION_SEED,
    model,
    0,
    fittedOptions,
  );
  if (
    fittedFirst.length === 0 ||
    fittedFirst.length !== fittedSecond.length ||
    fittedFirst.some(
      (example, index) =>
        example.target !== fittedSecond[index].target ||
        example.position.board.join("") !==
          fittedSecond[index].position.board.join(""),
    ) ||
    Math.abs(mean(fittedFirst.map((example) => example.target))) > 1e-6
  ) {
    throw new Error(
      "self-test fitted targets were not deterministic and centered",
    );
  }
  const contrastiveModel = createInitialTrainableModel();
  const contrastiveOptimizer = createAdamState(contrastiveModel);
  const contrastivePair: ContrastivePair = {
    better: examples[1].position,
    worse: examples[0].position,
  };
  const contrastiveBefore = summarizeContrastivePairs(
    contrastiveModel,
    [contrastivePair],
  ).meanGap;
  for (let step = 0; step < 80; step += 1) {
    trainContrastivePair(
      contrastivePair,
      contrastiveModel,
      contrastiveOptimizer,
      0.001,
      100_000,
      25_000,
    );
  }
  const contrastiveAfter = summarizeContrastivePairs(
    contrastiveModel,
    [contrastivePair],
  ).meanGap;
  if (!(contrastiveAfter > contrastiveBefore)) {
    throw new Error(
      `self-test contrastive gap did not increase: ${contrastiveBefore} -> ${contrastiveAfter}`,
    );
  }

  process.stdout.write(
    `self-test ok · value Huber ${before.toFixed(6)} -> ${after.toFixed(6)} · policy CE ${policyBefore.toFixed(6)} -> ${policyAfter.toFixed(6)} · ${fittedFirst.length} centered fitted targets · contrastive gap ${formatSignedInteger(contrastiveBefore)} -> ${formatSignedInteger(contrastiveAfter)} · inference ${forward.toFixed(3)} · mirror exact · streams deterministic\n`,
  );
}

function runEpisode(
  seed: number,
  model: TrainableModel,
  policy: PolicyOptions,
  maxMoves: number,
  captureTrajectory = false,
): EpisodeResult {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  const trajectory: EpisodeStep[] | undefined = captureTrajectory
    ? []
    : undefined;

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = choosePolicyColumn(state, seed, model, policy);
    if (column === null) {
      throw new Error("A live Drop7 game had no policy move");
    }

    const position: LearnedEvaluatorPosition = {
      board: state.board.slice(),
      movesRemaining: state.movesRemaining,
    };
    const revealSeed = actualRevealSeed(seed, state.movesPlayed);
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Policy chose illegal column ${column}`);
    trajectory?.push({
      state: {
        ...state,
        board: position.board,
        score: 0,
      },
      position,
      teacherColumn: column,
    });

    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          // This domain-separated stream is independent of how many covered
          // discs a policy happened to reveal during the preceding move.
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }

  return {
    score: state.score,
    moves: state.movesPlayed,
    completed: state.gameOver,
    ...(trajectory === undefined ? {} : { trajectory }),
  };
}

function runOracleEpisode(
  seed: number,
  depth: number,
  beamWidth: number,
  maxMoves: number,
): EpisodeResult {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  const trajectory: EpisodeStep[] = [];

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const plan = planOracleMove(state, seed, depth, beamWidth);
    const column = plan.column;
    if (column === null) {
      throw new Error("Oracle teacher returned no move for a live game");
    }
    const position: LearnedEvaluatorPosition = {
      board: state.board.slice(),
      movesRemaining: state.movesRemaining,
    };
    trajectory.push({
      state: { ...state, board: position.board, score: 0 },
      position,
      teacherColumn: column,
    });

    const move = playMove(
      state,
      column,
      seededRandom(actualRevealSeed(seed, state.movesPlayed)),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`Oracle teacher chose illegal column ${column}`);
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }

  return {
    score: state.score,
    moves: state.movesPlayed,
    completed: state.gameOver,
    trajectory,
  };
}

function evenlySpacedSteps(
  trajectory: readonly EpisodeStep[],
  maximum: number,
) {
  if (trajectory.length <= maximum) return trajectory;
  const selected: EpisodeStep[] = [];
  const used = new Set<number>();
  for (let sample = 0; sample < maximum; sample += 1) {
    const index = Math.min(
      trajectory.length - 1,
      Math.floor(((sample + 0.5) * trajectory.length) / maximum),
    );
    if (!used.has(index)) {
      used.add(index);
      selected.push(trajectory[index]);
    }
  }
  return selected;
}

function matchedTrajectoryPairs(
  betterTrajectory: readonly EpisodeStep[],
  worseTrajectory: readonly EpisodeStep[],
  maximum: number,
) {
  const worseByMove = new Map(
    worseTrajectory.map((step) => [step.state.movesPlayed, step] as const),
  );
  const candidates: ContrastivePair[] = [];
  for (const better of betterTrajectory) {
    const worse = worseByMove.get(better.state.movesPlayed);
    if (!worse || learnedPositionsAreSame(better.position, worse.position)) {
      continue;
    }
    candidates.push({ better: better.position, worse: worse.position });
  }
  if (candidates.length <= maximum) return candidates;
  return Array.from({ length: maximum }, (_, sample) =>
    candidates[
      Math.min(
        candidates.length - 1,
        Math.floor(((sample + 0.5) * candidates.length) / maximum),
      )
    ],
  );
}

function learnedPositionsAreSame(
  first: LearnedEvaluatorPosition,
  second: LearnedEvaluatorPosition,
) {
  const firstTokens = extractLearnedEvaluatorTokens(first).tokenIds;
  const secondTokens = extractLearnedEvaluatorTokens(second).tokenIds;
  if (firstTokens.length !== secondTokens.length) return false;
  for (let index = 0; index < firstTokens.length; index += 1) {
    if (firstTokens[index] !== secondTokens[index]) return false;
  }
  return true;
}

function estimateFittedExamples(
  root: GameState,
  seed: number,
  model: TrainableModel,
  learnedFraction: number,
  options: TrainingArguments,
): FittedExample[] {
  const actions: Array<{
    corrections: number[];
    positions: LearnedEvaluatorPosition[];
  }> = [];

  for (const column of columnOrderForBoard(root.board)) {
    if (root.board[column] !== EMPTY) continue;
    const corrections: number[] = [];
    const positions: LearnedEvaluatorPosition[] = [];
    for (let sample = 0; sample < options.policySamples; sample += 1) {
      const move = playMove(
        root,
        column,
        seededRandom(
          fittedEventSeed(
            seed,
            root.movesPlayed,
            sample,
            0,
            FITTED_REVEAL_DOMAIN,
          ),
        ),
        { captureAnimation: false },
      );
      if (!move) continue;
      if (move.state.gameOver) {
        // The ordinary one-ply evaluator already assigns terminalUtility, so
        // there is no residual correction to teach for this action.
        corrections.push(0);
        continue;
      }
      const successor: GameState = {
        ...move.state,
        score: 0,
        nextDisc: fittedDisc(
          seed,
          root.movesPlayed,
          sample,
          0,
        ),
      };
      const position = {
        board: successor.board,
        movesRemaining: successor.movesRemaining,
      } satisfies LearnedEvaluatorPosition;
      const continuation = sampleFittedContinuation(
        successor,
        seed,
        root.movesPlayed,
        sample,
        model,
        learnedFraction,
        options,
      );
      corrections.push(continuation - combinedValue(position));
      positions.push(position);
    }
    if (corrections.length > 0) actions.push({ corrections, positions });
  }

  if (actions.length === 0) return [];
  const actionCorrections = actions.map((action) => mean(action.corrections));
  const center = mean(actionCorrections);
  const examples: FittedExample[] = [];
  for (let actionIndex = 0; actionIndex < actions.length; actionIndex += 1) {
    const target = Math.max(
      -FITTED_TARGET_LIMIT,
      Math.min(
        FITTED_TARGET_LIMIT,
        actionCorrections[actionIndex] - center,
      ),
    );
    for (const position of actions[actionIndex].positions) {
      examples.push({ position, target });
    }
  }
  return examples;
}

function sampleFittedContinuation(
  initial: GameState,
  seed: number,
  rootMove: number,
  sample: number,
  model: TrainableModel,
  learnedFraction: number,
  options: TrainingArguments,
) {
  let state = initial;
  let total = 0;
  for (let ply = 0; ply < options.fittedHorizon; ply += 1) {
    const column = choosePolicyColumn(
      state,
      fittedEventSeed(
        seed,
        rootMove,
        sample,
        ply + 1,
        FITTED_POLICY_DOMAIN,
      ),
      model,
      {
        samples: options.policySamples,
        learnedFraction,
        baseline: "combined",
        commonActionSamples: true,
        epsilon: 0,
        terminalUtility: options.terminalUtility,
      },
    );
    if (column === null) return total + options.terminalUtility;
    const move = playMove(
      state,
      column,
      seededRandom(
        fittedEventSeed(
          seed,
          rootMove,
          sample,
          ply + 1,
          FITTED_REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) return total + options.terminalUtility;
    total += move.scoreDelta;
    if (move.state.gameOver) return total + options.terminalUtility;
    state = {
      ...move.state,
      score: 0,
      nextDisc: fittedDisc(seed, rootMove, sample, ply + 1),
    };
  }
  return (
    total +
    leafValue(
      state,
      model,
      learnedFraction,
      "combined",
      options.terminalUtility,
    )
  );
}

function fittedEventSeed(
  seed: number,
  rootMove: number,
  sample: number,
  ply: number,
  domain: number,
) {
  return mix32(
    seed ^
      Math.imul(rootMove + 1, MOVE_MULTIPLIER) ^
      Math.imul(sample + 1, SAMPLE_MULTIPLIER) ^
      Math.imul(ply + 1, COLUMN_MULTIPLIER) ^
      domain,
  );
}

function fittedDisc(
  seed: number,
  rootMove: number,
  sample: number,
  ply: number,
): DiscValue {
  const value = fittedEventSeed(
    seed,
    rootMove,
    sample,
    ply,
    FITTED_DISC_DOMAIN,
  );
  return (Math.floor((value / 4_294_967_296) * BOARD_SIZE) + 1) as DiscValue;
}

function visiblePlannerSeed(state: GameState) {
  let hash = 0x811c_9dc5;
  for (const tokenId of extractLearnedEvaluatorTokens(state).tokenIds) {
    hash ^= tokenId + 1;
    hash = Math.imul(hash, 0x0100_0193);
  }
  hash ^= state.nextDisc;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.level;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.movesPlayed;
  return mix32(hash);
}

function trainContrastivePair(
  pair: ContrastivePair,
  model: TrainableModel,
  optimizer: AdamState,
  learningRate: number,
  margin: number,
  temperature: number,
) {
  const betterPass = forwardPass(pair.better, model);
  const worsePass = forwardPass(pair.worse, model);
  const difference =
    combinedValue(pair.better) -
    combinedValue(pair.worse) +
    (betterPass.output - worsePass.output) * TARGET_SCALE;
  const violation = margin - difference;
  if (violation <= 0) return 0;
  const normalizedViolation = violation / temperature;
  const slope = Math.min(1, normalizedViolation);
  const betterGradient = clipGradient(
    (-slope * TARGET_SCALE) / temperature,
  );
  applyOutputGradient(
    betterPass,
    betterGradient,
    model,
    optimizer,
    learningRate,
  );
  applyOutputGradient(
    worsePass,
    -betterGradient,
    model,
    optimizer,
    learningRate,
  );
  return normalizedViolation <= 1
    ? 0.5 * normalizedViolation * normalizedViolation
    : normalizedViolation - 0.5;
}

function summarizeContrastivePairs(
  model: TrainableModel,
  pairs: readonly ContrastivePair[],
) {
  let correct = 0;
  let totalGap = 0;
  for (const pair of pairs) {
    const gap =
      combinedValue(pair.better) -
      combinedValue(pair.worse) +
      (forwardPass(pair.better, model).output -
        forwardPass(pair.worse, model).output) *
        TARGET_SCALE;
    totalGap += gap;
    if (gap > 0) correct += 1;
  }
  return {
    accuracy: correct / pairs.length,
    meanGap: totalGap / pairs.length,
  };
}

function trainPolicyEpisode(
  trajectory: readonly EpisodeStep[],
  seed: number,
  epochs: number,
  model: TrainablePolicyModel,
  optimizer: PolicyAdamState,
  learningRate: number,
) {
  let loss = 0;
  let positions = 0;
  for (let epoch = 0; epoch < epochs; epoch += 1) {
    const order = Array.from(
      { length: trajectory.length },
      (_, index) => index,
    );
    shuffle(
      order,
      seededRandom(
        mix32(
          seed ^
            SHUFFLE_DOMAIN ^
            Math.imul((epoch + 1) >>> 0, SAMPLE_MULTIPLIER),
        ),
      ),
    );
    for (const index of order) {
      const step = trajectory[index];
      const pass = forwardPolicy(step.state, model);
      const teacherColumn = pass.mirrored
        ? BOARD_SIZE - 1 - step.teacherColumn
        : step.teacherColumn;
      const legalCanonicalColumns = legalColumns(step.state.board).map(
        (column) => (pass.mirrored ? BOARD_SIZE - 1 - column : column),
      );
      const maximum = Math.max(
        ...legalCanonicalColumns.map(
          (column) => pass.canonicalLogits[column],
        ),
      );
      const exponentials = new Float64Array(BOARD_SIZE);
      let total = 0;
      for (const column of legalCanonicalColumns) {
        const value = Math.exp(pass.canonicalLogits[column] - maximum);
        exponentials[column] = value;
        total += value;
      }
      const gradient = new Float32Array(BOARD_SIZE);
      const uniformTarget =
        ACTION_LABEL_SMOOTHING / legalCanonicalColumns.length;
      for (const column of legalCanonicalColumns) {
        const probability = exponentials[column] / total;
        const target =
          uniformTarget +
          (column === teacherColumn ? 1 - ACTION_LABEL_SMOOTHING : 0);
        loss -= target * Math.log(Math.max(Number.MIN_VALUE, probability));
        gradient[column] = probability - target;
      }
      applyPolicyGradient(
        pass,
        gradient,
        model,
        optimizer,
        learningRate,
      );
      positions += 1;
    }
  }
  return { positions, loss };
}

function summarizePolicyLabels(
  trajectory: readonly EpisodeStep[],
  model: TrainablePolicyModel,
) {
  let crossEntropy = 0;
  let correct = 0;
  for (const step of trajectory) {
    const pass = forwardPolicy(step.state, model);
    const teacherColumn = pass.mirrored
      ? BOARD_SIZE - 1 - step.teacherColumn
      : step.teacherColumn;
    const legalCanonicalColumns = legalColumns(step.state.board).map(
      (column) => (pass.mirrored ? BOARD_SIZE - 1 - column : column),
    );
    const maximum = Math.max(
      ...legalCanonicalColumns.map(
        (column) => pass.canonicalLogits[column],
      ),
    );
    let total = 0;
    for (const column of legalCanonicalColumns) {
      total += Math.exp(pass.canonicalLogits[column] - maximum);
    }
    const teacherProbability =
      Math.exp(pass.canonicalLogits[teacherColumn] - maximum) / total;
    crossEntropy -= Math.log(
      Math.max(Number.MIN_VALUE, teacherProbability),
    );

    let bestColumn = legalCanonicalColumns[0];
    let bestLogit = Number.NEGATIVE_INFINITY;
    for (const column of CENTER_FIRST_COLUMNS) {
      if (!legalCanonicalColumns.includes(column)) continue;
      if (pass.canonicalLogits[column] > bestLogit) {
        bestColumn = column;
        bestLogit = pass.canonicalLogits[column];
      }
    }
    if (bestColumn === teacherColumn) correct += 1;
  }
  return {
    crossEntropy: crossEntropy / trajectory.length,
    accuracy: correct / trajectory.length,
  };
}

function forwardPolicy(
  position: LearnedPolicyPosition,
  model: TrainablePolicyModel,
): PolicyForwardPass {
  const { tokenIds, mirrored } = extractLearnedPolicyTokens(position);
  const accumulatorBeforeRelu = new Float32Array(model.accumulatorBias);
  for (const tokenId of tokenIds) {
    const offset = tokenId * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < LEARNED_POLICY_ACCUMULATOR_SIZE; unit += 1) {
      accumulatorBeforeRelu[unit] = Math.fround(
        accumulatorBeforeRelu[unit] + model.embedding[offset + unit],
      );
    }
  }
  const accumulator = new Float32Array(LEARNED_POLICY_ACCUMULATOR_SIZE);
  for (let unit = 0; unit < accumulator.length; unit += 1) {
    accumulator[unit] = relu(accumulatorBeforeRelu[unit]);
  }

  const hiddenBeforeRelu = new Float32Array(LEARNED_POLICY_HIDDEN_SIZE);
  const hidden = new Float32Array(LEARNED_POLICY_HIDDEN_SIZE);
  for (let hiddenUnit = 0; hiddenUnit < hidden.length; hiddenUnit += 1) {
    let sum = model.hiddenBias[hiddenUnit];
    const offset = hiddenUnit * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < accumulator.length; unit += 1) {
      sum = multiplyAdd(
        sum,
        accumulator[unit],
        model.hiddenWeights[offset + unit],
      );
    }
    hiddenBeforeRelu[hiddenUnit] = sum;
    hidden[hiddenUnit] = relu(sum);
  }

  const canonicalLogits = new Float32Array(BOARD_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let sum = model.outputBias[column];
    const offset = column * LEARNED_POLICY_HIDDEN_SIZE;
    for (let hiddenUnit = 0; hiddenUnit < hidden.length; hiddenUnit += 1) {
      sum = multiplyAdd(
        sum,
        hidden[hiddenUnit],
        model.outputWeights[offset + hiddenUnit],
      );
    }
    canonicalLogits[column] = sum;
  }
  return {
    tokenIds,
    mirrored,
    accumulatorBeforeRelu,
    accumulator,
    hiddenBeforeRelu,
    hidden,
    canonicalLogits,
  };
}

function applyPolicyGradient(
  pass: PolicyForwardPass,
  outputGradient: Float32Array,
  model: TrainablePolicyModel,
  optimizer: PolicyAdamState,
  learningRate: number,
) {
  const outputWeightGradient = new Float32Array(model.outputWeights.length);
  const hiddenGradient = new Float32Array(LEARNED_POLICY_HIDDEN_SIZE);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const gradient = outputGradient[column];
    const offset = column * LEARNED_POLICY_HIDDEN_SIZE;
    for (let hiddenUnit = 0; hiddenUnit < hiddenGradient.length; hiddenUnit += 1) {
      outputWeightGradient[offset + hiddenUnit] = clipGradient(
        gradient * pass.hidden[hiddenUnit],
      );
      hiddenGradient[hiddenUnit] +=
        gradient * model.outputWeights[offset + hiddenUnit];
    }
  }

  const hiddenWeightGradient = new Float32Array(model.hiddenWeights.length);
  const accumulatorGradient = new Float32Array(
    LEARNED_POLICY_ACCUMULATOR_SIZE,
  );
  for (let hiddenUnit = 0; hiddenUnit < hiddenGradient.length; hiddenUnit += 1) {
    if (pass.hiddenBeforeRelu[hiddenUnit] <= 0) {
      hiddenGradient[hiddenUnit] = 0;
      continue;
    }
    hiddenGradient[hiddenUnit] = clipGradient(hiddenGradient[hiddenUnit]);
    const offset = hiddenUnit * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < accumulatorGradient.length; unit += 1) {
      hiddenWeightGradient[offset + unit] = clipGradient(
        hiddenGradient[hiddenUnit] * pass.accumulator[unit],
      );
      accumulatorGradient[unit] +=
        hiddenGradient[hiddenUnit] * model.hiddenWeights[offset + unit];
    }
  }
  for (let unit = 0; unit < accumulatorGradient.length; unit += 1) {
    accumulatorGradient[unit] =
      pass.accumulatorBeforeRelu[unit] > 0
        ? clipGradient(accumulatorGradient[unit])
        : 0;
  }

  optimizer.betaOnePower *= ADAM_BETA_ONE;
  optimizer.betaTwoPower *= ADAM_BETA_TWO;
  const correctionOne = 1 - optimizer.betaOnePower;
  const correctionTwo = 1 - optimizer.betaTwoPower;
  adamUpdateVector(
    model.outputWeights,
    optimizer.outputWeights,
    outputWeightGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.outputBias,
    optimizer.outputBias,
    outputGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.hiddenWeights,
    optimizer.hiddenWeights,
    hiddenWeightGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.hiddenBias,
    optimizer.hiddenBias,
    hiddenGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.accumulatorBias,
    optimizer.accumulatorBias,
    accumulatorGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  for (const tokenId of pass.tokenIds) {
    const offset = tokenId * LEARNED_POLICY_ACCUMULATOR_SIZE;
    for (let unit = 0; unit < accumulatorGradient.length; unit += 1) {
      adamUpdateIndex(
        model.embedding,
        optimizer.embedding,
        offset + unit,
        accumulatorGradient[unit],
        learningRate,
        correctionOne,
        correctionTwo,
      );
    }
  }

  // Direct classification otherwise has an easy all-dead ReLU attractor.
  for (let unit = 0; unit < model.accumulatorBias.length; unit += 1) {
    model.accumulatorBias[unit] = Math.max(
      ORACLE_RELU_BIAS_FLOOR,
      model.accumulatorBias[unit],
    );
  }
  for (let unit = 0; unit < model.hiddenBias.length; unit += 1) {
    model.hiddenBias[unit] = Math.max(
      ORACLE_RELU_BIAS_FLOOR,
      model.hiddenBias[unit],
    );
  }
}

function choosePolicyColumn(
  state: GameState,
  seed: number,
  model: TrainableModel,
  options: PolicyOptions,
) {
  const columns = legalColumns(state.board);
  if (columns.length === 0) return null;
  const orderedColumns = columnOrderForBoard(state.board).filter((column) =>
    columns.includes(column),
  );

  if (
    deterministicSample(
      seed,
      state.movesPlayed,
      0,
      POLICY_EXPLORATION_DOMAIN,
    ) < options.epsilon
  ) {
    const sample = deterministicSample(
      seed,
      state.movesPlayed,
      0,
      POLICY_CHOICE_DOMAIN,
    );
    return orderedColumns[Math.floor(sample * orderedColumns.length)];
  }

  let bestColumn = orderedColumns[0];
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of orderedColumns) {
    let value = 0;
    for (let sample = 0; sample < options.samples; sample += 1) {
      // Each column receives the same deterministic random stream for a given
      // sample, reducing chance noise in comparisons without peeking at the
      // stream used by the move that is ultimately played.
      const probe = playMove(
        state,
        column,
        seededRandom(
          options.commonActionSamples
            ? siblingRevealSeed(seed, state.movesPlayed, sample)
            : policyRevealSeed(
                seed,
                state.movesPlayed,
                column,
                sample,
              ),
        ),
        { captureAnimation: false },
      );
      if (!probe) {
        value += options.terminalUtility;
        continue;
      }
      value +=
        probe.scoreDelta +
        leafValue(
          probe.state,
          model,
          options.learnedFraction,
          options.baseline,
          options.terminalUtility,
        );
    }
    value /= options.samples;
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function leafValue(
  state: GameState,
  model: TrainableModel,
  learnedFraction: number,
  baselineKind: PolicyOptions["baseline"],
  terminalUtility: number,
) {
  if (state.gameOver) return terminalUtility;
  const heuristic = evaluateHeuristic(state, "combined");
  const baseline =
    baselineKind === "teacher" ? evaluateTeacherValue(state) : heuristic;
  if (learnedFraction === 0) return baseline;
  const learned = forwardPass(state, model).output * TARGET_SCALE;
  if (!Number.isFinite(learned)) {
    throw new Error("Learned policy evaluation became non-finite");
  }
  if (learnedFraction === 1) return heuristic + learned;
  return baseline + (heuristic + learned - baseline) * learnedFraction;
}

function trainCompletedEpisode(
  trajectory: readonly EpisodeStep[],
  seed: number,
  policySamples: number,
  model: TrainableModel,
  optimizer: AdamState,
  learningRate: number,
) {
  const order = Array.from({ length: trajectory.length }, (_, index) => index);
  shuffle(order, seededRandom(mix32(seed ^ SHUFFLE_DOMAIN)));
  let loss = 0;
  let positions = 0;
  for (const index of order) {
    const step = trajectory[index];
    const trainPosition = (
      position: LearnedEvaluatorPosition,
      state: GameState,
    ) => {
      const target = evaluateTeacherValue(state) - combinedValue(position);
      loss += trainExample(
        position,
        target,
        model,
        optimizer,
        learningRate,
      );
      positions += 1;
    };

    trainPosition(step.position, step.state);

    // Counterfactual sibling states are the key supervision: every legal
    // action is sampled with common random numbers, including actions the
    // behavior policy did not choose. Matching the teacher on these siblings
    // trains the ordering used by one-ply search instead of merely fitting the
    // scalar return of one realized trajectory.
    for (const column of columnOrderForBoard(step.state.board)) {
      if (step.state.board[column] !== EMPTY) continue;
      for (let sample = 0; sample < policySamples; sample += 1) {
        const move = playMove(
          step.state,
          column,
          seededRandom(
            siblingRevealSeed(seed, step.state.movesPlayed, sample),
          ),
          { captureAnimation: false },
        );
        if (!move || move.state.gameOver) continue;
        const position = {
          board: move.state.board,
          movesRemaining: move.state.movesRemaining,
        } satisfies LearnedEvaluatorPosition;
        trainPosition(position, { ...move.state, score: 0 });
      }
    }
  }
  return { positions, loss };
}

/** Sparse because no dense embedding-gradient buffer is ever materialized. */
function trainExample(
  position: LearnedEvaluatorPosition,
  rawTarget: number,
  model: TrainableModel,
  optimizer: AdamState,
  learningRate: number,
  weight = 1,
) {
  const pass = forwardPass(position, model);
  const target = rawTarget / TARGET_SCALE;
  const loss = huberLoss(pass.output - target);
  const outputGradient = clipGradient(
    huberDerivative(pass.output - target) * weight,
  );

  applyOutputGradient(
    pass,
    outputGradient,
    model,
    optimizer,
    learningRate,
  );
  return loss * weight;
}

function applyOutputGradient(
  pass: ForwardPass,
  outputGradient: number,
  model: TrainableModel,
  optimizer: AdamState,
  learningRate: number,
) {

  const hiddenGradient = new Float32Array(LEARNED_EVALUATOR_HIDDEN_SIZE);
  const accumulatorGradient = new Float32Array(
    LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
  );
  const outputWeightGradient = new Float32Array(
    LEARNED_EVALUATOR_HIDDEN_SIZE,
  );
  const hiddenWeightGradient = new Float32Array(
    model.hiddenWeights.length,
  );

  for (
    let hiddenUnit = 0;
    hiddenUnit < LEARNED_EVALUATOR_HIDDEN_SIZE;
    hiddenUnit += 1
  ) {
    outputWeightGradient[hiddenUnit] = clipGradient(
      outputGradient * pass.hidden[hiddenUnit],
    );
    if (pass.hiddenBeforeRelu[hiddenUnit] <= 0) continue;
    const gradient = clipGradient(
      outputGradient * model.outputWeights[hiddenUnit],
    );
    hiddenGradient[hiddenUnit] = gradient;
    const weightOffset =
      hiddenUnit * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let accumulatorUnit = 0;
      accumulatorUnit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      accumulatorUnit += 1
    ) {
      hiddenWeightGradient[weightOffset + accumulatorUnit] = clipGradient(
        gradient * pass.accumulator[accumulatorUnit],
      );
      accumulatorGradient[accumulatorUnit] +=
        gradient * model.hiddenWeights[weightOffset + accumulatorUnit];
    }
  }

  for (
    let accumulatorUnit = 0;
    accumulatorUnit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    accumulatorUnit += 1
  ) {
    accumulatorGradient[accumulatorUnit] =
      pass.accumulatorBeforeRelu[accumulatorUnit] > 0
        ? clipGradient(accumulatorGradient[accumulatorUnit])
        : 0;
  }

  optimizer.step += 1;
  optimizer.betaOnePower *= ADAM_BETA_ONE;
  optimizer.betaTwoPower *= ADAM_BETA_TWO;
  const correctionOne = 1 - optimizer.betaOnePower;
  const correctionTwo = 1 - optimizer.betaTwoPower;

  adamUpdateVector(
    model.outputWeights,
    optimizer.outputWeights,
    outputWeightGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  model.outputBias = adamUpdateScalar(
    model.outputBias,
    outputGradient,
    optimizer,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.hiddenWeights,
    optimizer.hiddenWeights,
    hiddenWeightGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.hiddenBias,
    optimizer.hiddenBias,
    hiddenGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );
  adamUpdateVector(
    model.accumulatorBias,
    optimizer.accumulatorBias,
    accumulatorGradient,
    learningRate,
    correctionOne,
    correctionTwo,
  );

  // Each active token contributes once, so its row receives the accumulator
  // gradient directly. Untouched rows allocate no per-example gradients and
  // receive no optimizer work.
  for (const tokenId of pass.tokenIds) {
    const offset = tokenId * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let unit = 0;
      unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      unit += 1
    ) {
      adamUpdateIndex(
        model.embedding,
        optimizer.embedding,
        offset + unit,
        accumulatorGradient[unit],
        learningRate,
        correctionOne,
        correctionTwo,
      );
    }
  }
}

function forwardPass(
  position: LearnedEvaluatorPosition,
  model: TrainableModel,
): ForwardPass {
  const { tokenIds } = extractLearnedEvaluatorTokens(position);
  const accumulatorBeforeRelu = new Float32Array(model.accumulatorBias);
  for (const tokenId of tokenIds) {
    const offset = tokenId * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let unit = 0;
      unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      unit += 1
    ) {
      accumulatorBeforeRelu[unit] = Math.fround(
        accumulatorBeforeRelu[unit] + model.embedding[offset + unit],
      );
    }
  }

  const accumulator = new Float32Array(LEARNED_EVALUATOR_ACCUMULATOR_SIZE);
  for (
    let unit = 0;
    unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    unit += 1
  ) {
    accumulator[unit] = relu(accumulatorBeforeRelu[unit]);
  }

  const hiddenBeforeRelu = new Float32Array(
    LEARNED_EVALUATOR_HIDDEN_SIZE,
  );
  const hidden = new Float32Array(LEARNED_EVALUATOR_HIDDEN_SIZE);
  for (
    let hiddenUnit = 0;
    hiddenUnit < LEARNED_EVALUATOR_HIDDEN_SIZE;
    hiddenUnit += 1
  ) {
    let sum = model.hiddenBias[hiddenUnit];
    const offset = hiddenUnit * LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
    for (
      let unit = 0;
      unit < LEARNED_EVALUATOR_ACCUMULATOR_SIZE;
      unit += 1
    ) {
      sum = multiplyAdd(sum, accumulator[unit], model.hiddenWeights[offset + unit]);
    }
    hiddenBeforeRelu[hiddenUnit] = sum;
    hidden[hiddenUnit] = relu(sum);
  }

  let output = model.outputBias;
  for (
    let hiddenUnit = 0;
    hiddenUnit < LEARNED_EVALUATOR_HIDDEN_SIZE;
    hiddenUnit += 1
  ) {
    output = multiplyAdd(
      output,
      hidden[hiddenUnit],
      model.outputWeights[hiddenUnit],
    );
  }
  return {
    tokenIds,
    accumulatorBeforeRelu,
    accumulator,
    hiddenBeforeRelu,
    hidden,
    output,
  };
}

function validatePolicy(
  model: TrainableModel,
  options: TrainingArguments,
): ValidationResult {
  const learnedResults: EpisodeResult[] = [];
  const baselineResults: EpisodeResult[] = [];
  const teacherResults: EpisodeResult[] = [];
  for (let index = 0; index < options.validationGames; index += 1) {
    const seed = (VALIDATION_SEED_START + index) >>> 0;
    learnedResults.push(
      runEpisode(
        seed,
        model,
        {
          samples: options.policySamples,
          learnedFraction: 1,
          baseline: "combined",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
    baselineResults.push(
      runEpisode(
        seed,
        model,
        {
          samples: options.policySamples,
          learnedFraction: 0,
          baseline: "combined",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
    teacherResults.push(
      runEpisode(
        seed,
        model,
        {
          samples: options.policySamples,
          learnedFraction: 0,
          baseline: "teacher",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
  }

  return {
    learned: summarizeEpisodes(learnedResults),
    baseline: summarizeEpisodes(baselineResults),
    teacher: summarizeEpisodes(teacherResults),
    pairedMeanScoreDelta: mean(
      learnedResults.map(
        (episode, index) => episode.score - baselineResults[index].score,
      ),
    ),
    pairedMeanMoveDelta: mean(
      learnedResults.map(
        (episode, index) => episode.moves - baselineResults[index].moves,
      ),
    ),
  };
}

function calibrateResidualScale(
  model: TrainableModel,
  options: TrainingArguments,
  seeds = Array.from(
    { length: options.games },
    (_, index) => (TRAINING_SEED_START + index) >>> 0,
  ),
) {
  const baselineResults = seeds.map((seed) =>
    runEpisode(
      seed,
      model,
      {
        samples: options.policySamples,
        learnedFraction: 0,
        baseline: "combined",
        commonActionSamples: false,
        epsilon: 0,
        terminalUtility: options.terminalUtility,
      },
      options.maxMoves,
    ),
  );
  let bestScale = 0;
  let bestResults = baselineResults;
  let bestDelta = 0;
  for (const scale of [
    1 / 512,
    1 / 256,
    1 / 128,
    1 / 64,
    1 / 32,
    1 / 16,
    1 / 8,
    1 / 4,
    1 / 2,
    1,
  ]) {
    const results = seeds.map((seed) =>
      runEpisode(
        seed,
        model,
        {
          samples: options.policySamples,
          learnedFraction: scale,
          baseline: "combined",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
    const delta = mean(
      results.map(
        (episode, index) => episode.score - baselineResults[index].score,
      ),
    );
    if (delta > bestDelta) {
      bestScale = scale;
      bestResults = results;
      bestDelta = delta;
    }
  }
  return {
    scale: bestScale,
    learned: summarizeEpisodes(bestResults),
    baseline: summarizeEpisodes(baselineResults),
    pairedMeanScoreDelta: bestDelta,
  };
}

function scaledResidualModel(model: TrainableModel, scale: number) {
  const scaled = cloneTrainableModel(model);
  for (let index = 0; index < scaled.outputWeights.length; index += 1) {
    scaled.outputWeights[index] = Math.fround(
      scaled.outputWeights[index] * scale,
    );
  }
  scaled.outputBias = Math.fround(scaled.outputBias * scale);
  return scaled;
}

/**
 * Fair validation for the distilled policy. The policy receives only the
 * current board, move clock, and visible next disc. The game seed is used
 * solely after the column has been chosen, to realize the environment's
 * covered-disc reveals and next disc.
 */
function validateDirectPolicy(
  model: TrainablePolicyModel,
  options: TrainingArguments,
  reference = createDirectPolicyValidationReference(options),
): ValidationResult {
  const compiled = compileLearnedPolicyWeights(exportPolicyModel(model));
  const learnedResults: EpisodeResult[] = [];

  for (let index = 0; index < options.validationGames; index += 1) {
    const seed = (VALIDATION_SEED_START + index) >>> 0;
    learnedResults.push(
      runDirectPolicyEpisode(seed, compiled, options.maxMoves),
    );
  }

  return {
    learned: summarizeEpisodes(learnedResults),
    baseline: summarizeEpisodes(reference.baselineResults),
    teacher: summarizeEpisodes(reference.teacherResults),
    pairedMeanScoreDelta: mean(
      learnedResults.map(
        (episode, index) =>
          episode.score - reference.baselineResults[index].score,
      ),
    ),
    pairedMeanMoveDelta: mean(
      learnedResults.map(
        (episode, index) =>
          episode.moves - reference.baselineResults[index].moves,
      ),
    ),
  };
}

function createDirectPolicyValidationReference(
  options: TrainingArguments,
): DirectPolicyValidationReference {
  const baselineModel = createBaselineResidualModel(
    createInitialTrainableModel(),
  );
  const baselineResults: EpisodeResult[] = [];
  const teacherResults: EpisodeResult[] = [];

  for (let index = 0; index < options.validationGames; index += 1) {
    const seed = (VALIDATION_SEED_START + index) >>> 0;
    baselineResults.push(
      runEpisode(
        seed,
        baselineModel,
        {
          samples: options.policySamples,
          learnedFraction: 0,
          baseline: "combined",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
    teacherResults.push(
      runEpisode(
        seed,
        baselineModel,
        {
          samples: options.policySamples,
          learnedFraction: 0,
          baseline: "teacher",
          commonActionSamples: false,
          epsilon: 0,
          terminalUtility: options.terminalUtility,
        },
        options.maxMoves,
      ),
    );
  }
  return { baselineResults, teacherResults };
}

function runDirectPolicyEpisode(
  seed: number,
  policy: CompiledLearnedPolicyWeights,
  maxMoves: number,
): EpisodeResult {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = evaluateLearnedPolicy(state, policy).bestColumn;
    if (column === null) {
      throw new Error("A live Drop7 game had no distilled-policy move");
    }
    const move = playMove(
      state,
      column,
      seededRandom(actualRevealSeed(seed, state.movesPlayed)),
      { captureAnimation: false },
    );
    if (!move) {
      throw new Error(`Distilled policy chose illegal column ${column}`);
    }
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }

  return {
    score: state.score,
    moves: state.movesPlayed,
    completed: state.gameOver,
  };
}

function validateRolloutLeaf(
  model: TrainableModel,
  options: TrainingArguments,
): ValidationResult {
  const learnedResults: EpisodeResult[] = [];
  const baselineResults: EpisodeResult[] = [];
  for (let index = 0; index < options.validationGames; index += 1) {
    const seed = (VALIDATION_SEED_START + index) >>> 0;
    learnedResults.push(
      runRolloutLeafEpisode(seed, model, true, options),
    );
    baselineResults.push(
      runRolloutLeafEpisode(seed, model, false, options),
    );
  }
  return {
    learned: summarizeEpisodes(learnedResults),
    baseline: summarizeEpisodes(baselineResults),
    teacher: summarizeEpisodes(baselineResults),
    pairedMeanScoreDelta: mean(
      learnedResults.map(
        (episode, index) => episode.score - baselineResults[index].score,
      ),
    ),
    pairedMeanMoveDelta: mean(
      learnedResults.map(
        (episode, index) => episode.moves - baselineResults[index].moves,
      ),
    ),
  };
}

function validateSparseLeaf(
  model: TrainableModel,
  options: TrainingArguments,
  seeds: readonly number[],
): ValidationResult {
  const learnedResults = seeds.map((seed) =>
    runSparseLeafEpisode(seed, model, true, options),
  );
  const baselineResults = seeds.map((seed) =>
    runSparseLeafEpisode(seed, model, false, options),
  );
  return {
    learned: summarizeEpisodes(learnedResults),
    baseline: summarizeEpisodes(baselineResults),
    teacher: summarizeEpisodes(baselineResults),
    pairedMeanScoreDelta: mean(
      learnedResults.map(
        (episode, index) => episode.score - baselineResults[index].score,
      ),
    ),
    pairedMeanMoveDelta: mean(
      learnedResults.map(
        (episode, index) => episode.moves - baselineResults[index].moves,
      ),
    ),
  };
}

function runSparseLeafEpisode(
  seed: number,
  model: TrainableModel,
  learned: boolean,
  options: TrainingArguments,
): EpisodeResult {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const evaluation = evaluateSparseExpectimaxMoves(state, {
      maxDepth: 3,
      chanceSamples: 4,
      maxCacheEntries: 40_000,
      seed: 0xd707_5eed,
      terminalUtility: options.terminalUtility,
      ...(learned
        ? {
            evaluator: (position: GameState) =>
              evaluateHeuristic(position, "combined") +
              forwardPass(position, model).output * TARGET_SCALE,
          }
        : { heuristicProfile: "combined" as const }),
    });
    if (evaluation.bestColumn === null) {
      throw new Error("A live sparse-validation game had no move");
    }
    const move = playMove(
      state,
      evaluation.bestColumn,
      seededRandom(actualRevealSeed(seed, state.movesPlayed)),
      { captureAnimation: false },
    );
    if (!move) {
      throw new Error(
        `Sparse validation chose illegal column ${evaluation.bestColumn}`,
      );
    }
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }
  return {
    score: state.score,
    moves: state.movesPlayed,
    completed: state.gameOver,
  };
}

function runRolloutLeafEpisode(
  seed: number,
  model: TrainableModel,
  learned: boolean,
  options: TrainingArguments,
): EpisodeResult {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const evaluation = evaluateRolloutMoves(state, {
      rollouts: VALIDATION_ROLLOUTS,
      horizon: VALIDATION_ROLLOUT_HORIZON,
      continuationSamples: 1,
      seed: visiblePlannerSeed(state),
      heuristicProfile: "combined",
      terminalUtility: options.terminalUtility,
      ...(learned
        ? {
            evaluator: (position: GameState) =>
              evaluateHeuristic(position, "combined") +
              forwardPass(position, model).output * TARGET_SCALE,
          }
        : {}),
    });
    if (evaluation.bestColumn === null) {
      throw new Error("A live rollout-validation game had no move");
    }
    const move = playMove(
      state,
      evaluation.bestColumn,
      seededRandom(actualRevealSeed(seed, state.movesPlayed)),
      { captureAnimation: false },
    );
    if (!move) {
      throw new Error(
        `Rollout validation chose illegal column ${evaluation.bestColumn}`,
      );
    }
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }
  return {
    score: state.score,
    moves: state.movesPlayed,
    completed: state.gameOver,
  };
}

function summarizeEpisodes(episodes: readonly EpisodeResult[]): ValidationSide {
  return {
    meanScore: mean(episodes.map((episode) => episode.score)),
    meanMoves: mean(episodes.map((episode) => episode.moves)),
    completedGames: episodes.filter((episode) => episode.completed).length,
  };
}

function createInitialTrainableModel() {
  const artifact = createRandomLearnedEvaluatorWeights(
    MODEL_INITIALIZATION_SEED,
    MODEL_INITIALIZATION_SCALE,
  );
  // A fresh initializer already lives in normalized target units. Serialized
  // models, in contrast, scale their final layer back to raw score units.
  const model = artifactToTrainable(artifact, false);
  // Small positive biases keep both ReLU stages trainable at startup despite
  // summing many signed embeddings. They remain ordinary learned parameters.
  model.accumulatorBias.fill(0.1);
  model.hiddenBias.fill(0.1);
  model.outputBias = 0;
  return model;
}

function createInitialTrainablePolicyModel(): TrainablePolicyModel {
  const model = policyArtifactToTrainable(createZeroLearnedPolicyWeights());
  const random = seededRandom(
    mix32(MODEL_INITIALIZATION_SEED ^ 0x504f_4c59),
  );
  const fillRandom = (weights: Float32Array, scale: number) => {
    for (let index = 0; index < weights.length; index += 1) {
      weights[index] = Math.fround((random() * 2 - 1) * scale);
    }
  };
  fillRandom(model.embedding, MODEL_INITIALIZATION_SCALE);
  fillRandom(model.hiddenWeights, MODEL_INITIALIZATION_SCALE);
  fillRandom(model.outputWeights, MODEL_INITIALIZATION_SCALE);
  model.accumulatorBias.fill(0.1);
  model.hiddenBias.fill(0.1);
  model.outputBias.fill(0);
  return model;
}

function cloneTrainableModel(model: TrainableModel): TrainableModel {
  return {
    embedding: new Float32Array(model.embedding),
    accumulatorBias: new Float32Array(model.accumulatorBias),
    hiddenWeights: new Float32Array(model.hiddenWeights),
    hiddenBias: new Float32Array(model.hiddenBias),
    outputWeights: new Float32Array(model.outputWeights),
    outputBias: model.outputBias,
  };
}

function cloneTrainablePolicyModel(
  model: TrainablePolicyModel,
): TrainablePolicyModel {
  return {
    embedding: new Float32Array(model.embedding),
    accumulatorBias: new Float32Array(model.accumulatorBias),
    hiddenWeights: new Float32Array(model.hiddenWeights),
    hiddenBias: new Float32Array(model.hiddenBias),
    outputWeights: new Float32Array(model.outputWeights),
    outputBias: new Float32Array(model.outputBias),
  };
}

function createBaselineResidualModel(model: TrainableModel) {
  const baseline = cloneTrainableModel(model);
  baseline.outputWeights.fill(0);
  baseline.outputBias = 0;
  return baseline;
}

async function loadTrainableModel(path: string) {
  const contents = await readFile(path, "utf8");
  const parsed: unknown = JSON.parse(contents) as unknown;
  validateSerializedLearnedEvaluatorWeights(parsed);
  if (parsed.baseline !== "combined") {
    throw new Error(
      "--resume requires a residual model with baseline set to combined",
    );
  }
  return artifactToTrainable(parsed, true);
}

async function loadTrainablePolicyModel(path: string) {
  const contents = await readFile(path, "utf8");
  const parsed: unknown = JSON.parse(contents) as unknown;
  validateSerializedLearnedPolicyWeights(parsed);
  return policyArtifactToTrainable(parsed);
}

function artifactToTrainable(
  artifact: SerializedLearnedEvaluatorWeights,
  rawOutputUnits: boolean,
): TrainableModel {
  const outputScale = rawOutputUnits ? TARGET_SCALE : 1;
  return {
    embedding: new Float32Array(artifact.embedding),
    accumulatorBias: new Float32Array(artifact.accumulatorBias),
    hiddenWeights: new Float32Array(artifact.hiddenWeights),
    hiddenBias: new Float32Array(artifact.hiddenBias),
    outputWeights: Float32Array.from(artifact.outputWeights, (value) =>
      Math.fround(value / outputScale),
    ),
    outputBias: Math.fround(artifact.outputBias / outputScale),
  };
}

function policyArtifactToTrainable(
  artifact: SerializedLearnedPolicyWeights,
): TrainablePolicyModel {
  return {
    embedding: new Float32Array(artifact.embedding),
    accumulatorBias: new Float32Array(artifact.accumulatorBias),
    hiddenWeights: new Float32Array(artifact.hiddenWeights),
    hiddenBias: new Float32Array(artifact.hiddenBias),
    outputWeights: new Float32Array(artifact.outputWeights),
    outputBias: new Float32Array(artifact.outputBias),
  };
}

function exportModel(model: TrainableModel): SerializedLearnedEvaluatorWeights {
  const artifact: SerializedLearnedEvaluatorWeights = {
    format: LEARNED_EVALUATOR_FORMAT,
    version: LEARNED_EVALUATOR_VERSION,
    baseline: "combined",
    embedding: Array.from(model.embedding),
    accumulatorBias: Array.from(model.accumulatorBias),
    hiddenWeights: Array.from(model.hiddenWeights),
    hiddenBias: Array.from(model.hiddenBias),
    outputWeights: Array.from(model.outputWeights, (value) =>
      Math.fround(value * TARGET_SCALE),
    ),
    outputBias: Math.fround(model.outputBias * TARGET_SCALE),
  };
  validateSerializedLearnedEvaluatorWeights(artifact);
  return artifact;
}

function exportPolicyModel(
  model: TrainablePolicyModel,
): SerializedLearnedPolicyWeights {
  const artifact: SerializedLearnedPolicyWeights = {
    format: LEARNED_POLICY_FORMAT,
    version: LEARNED_POLICY_VERSION,
    embedding: Array.from(model.embedding),
    accumulatorBias: Array.from(model.accumulatorBias),
    hiddenWeights: Array.from(model.hiddenWeights),
    hiddenBias: Array.from(model.hiddenBias),
    outputWeights: Array.from(model.outputWeights),
    outputBias: Array.from(model.outputBias),
  };
  validateSerializedLearnedPolicyWeights(artifact);
  return artifact;
}

async function writeModel(
  outputPath: string,
  artifact: SerializedLearnedEvaluatorWeights,
) {
  const serialized = `${JSON.stringify(artifact)}\n`;
  if (outputPath === "-") {
    process.stdout.write(serialized);
    return;
  }
  const absolute = resolve(outputPath);
  await mkdir(dirname(absolute), { recursive: true });
  const temporary = `${absolute}.${process.pid}.tmp`;
  await writeFile(temporary, serialized, "utf8");
  await rename(temporary, absolute);
}

async function writePolicyModel(
  outputPath: string,
  artifact: SerializedLearnedPolicyWeights,
) {
  const serialized = `${JSON.stringify(artifact)}\n`;
  if (outputPath === "-") {
    process.stdout.write(serialized);
    return;
  }
  const absolute = resolve(outputPath);
  await mkdir(dirname(absolute), { recursive: true });
  const temporary = `${absolute}.${process.pid}.tmp`;
  await writeFile(temporary, serialized, "utf8");
  await rename(temporary, absolute);
}

function createAdamState(model: TrainableModel): AdamState {
  return {
    step: 0,
    betaOnePower: 1,
    betaTwoPower: 1,
    embedding: createMoments(model.embedding.length),
    accumulatorBias: createMoments(model.accumulatorBias.length),
    hiddenWeights: createMoments(model.hiddenWeights.length),
    hiddenBias: createMoments(model.hiddenBias.length),
    outputWeights: createMoments(model.outputWeights.length),
    outputBiasFirst: 0,
    outputBiasSecond: 0,
  };
}

function createPolicyAdamState(model: TrainablePolicyModel): PolicyAdamState {
  return {
    betaOnePower: 1,
    betaTwoPower: 1,
    embedding: createMoments(model.embedding.length),
    accumulatorBias: createMoments(model.accumulatorBias.length),
    hiddenWeights: createMoments(model.hiddenWeights.length),
    hiddenBias: createMoments(model.hiddenBias.length),
    outputWeights: createMoments(model.outputWeights.length),
    outputBias: createMoments(model.outputBias.length),
  };
}

function createMoments(length: number): VectorMoments {
  return {
    first: new Float32Array(length),
    second: new Float32Array(length),
  };
}

function adamUpdateVector(
  weights: Float32Array,
  moments: VectorMoments,
  gradient: Float32Array,
  learningRate: number,
  correctionOne: number,
  correctionTwo: number,
) {
  for (let index = 0; index < weights.length; index += 1) {
    adamUpdateIndex(
      weights,
      moments,
      index,
      gradient[index],
      learningRate,
      correctionOne,
      correctionTwo,
    );
  }
}

function adamUpdateIndex(
  weights: Float32Array,
  moments: VectorMoments,
  index: number,
  gradient: number,
  learningRate: number,
  correctionOne: number,
  correctionTwo: number,
) {
  const first =
    ADAM_BETA_ONE * moments.first[index] +
    (1 - ADAM_BETA_ONE) * gradient;
  const second =
    ADAM_BETA_TWO * moments.second[index] +
    (1 - ADAM_BETA_TWO) * gradient * gradient;
  moments.first[index] = Math.fround(first);
  moments.second[index] = Math.fround(second);
  const correctedFirst = first / correctionOne;
  const correctedSecond = second / correctionTwo;
  weights[index] = Math.fround(
    weights[index] -
      (learningRate * correctedFirst) /
        (Math.sqrt(correctedSecond) + ADAM_EPSILON),
  );
}

function adamUpdateScalar(
  weight: number,
  gradient: number,
  optimizer: AdamState,
  learningRate: number,
  correctionOne: number,
  correctionTwo: number,
) {
  optimizer.outputBiasFirst =
    ADAM_BETA_ONE * optimizer.outputBiasFirst +
    (1 - ADAM_BETA_ONE) * gradient;
  optimizer.outputBiasSecond =
    ADAM_BETA_TWO * optimizer.outputBiasSecond +
    (1 - ADAM_BETA_TWO) * gradient * gradient;
  const correctedFirst = optimizer.outputBiasFirst / correctionOne;
  const correctedSecond = optimizer.outputBiasSecond / correctionTwo;
  return Math.fround(
    weight -
      (learningRate * correctedFirst) /
        (Math.sqrt(correctedSecond) + ADAM_EPSILON),
  );
}

function policyLearnedFraction(
  gameIndex: number,
  warmupGames: number,
  rampGames: number,
) {
  if (gameIndex < warmupGames) return 0;
  if (rampGames === 0) return 1;
  return Math.min(1, (gameIndex - warmupGames + 1) / rampGames);
}

function actualRevealSeed(seed: number, move: number) {
  // Matches headless.ts so policy comparisons can reuse the exact paired
  // upcoming-disc and reveal streams used by the benchmark harness.
  return mix32(
    seed ^
      Math.imul((move + 1) >>> 0, MOVE_MULTIPLIER) ^
      ACTUAL_REVEAL_DOMAIN,
  );
}

function policyRevealSeed(
  seed: number,
  move: number,
  column: number,
  sample: number,
) {
  return mix32(
    seed ^
      Math.imul((move + 1) >>> 0, MOVE_MULTIPLIER) ^
      Math.imul((column + 1) >>> 0, COLUMN_MULTIPLIER) ^
      Math.imul((sample + 1) >>> 0, SAMPLE_MULTIPLIER) ^
      POLICY_REVEAL_DOMAIN,
  );
}

function siblingRevealSeed(seed: number, move: number, sample: number) {
  return mix32(
    seed ^
      Math.imul((move + 1) >>> 0, MOVE_MULTIPLIER) ^
      Math.imul((sample + 1) >>> 0, SAMPLE_MULTIPLIER) ^
      POLICY_REVEAL_DOMAIN,
  );
}

function deterministicSample(
  seed: number,
  move: number,
  sample: number,
  domain: number,
) {
  return (
    mix32(
      seed ^
        Math.imul((move + 1) >>> 0, MOVE_MULTIPLIER) ^
        Math.imul((sample + 1) >>> 0, SAMPLE_MULTIPLIER) ^
        domain,
    ) / 4_294_967_296
  );
}

function columnOrderForBoard(board: Board): readonly number[] {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (forward < mirrored) return CENTER_FIRST_COLUMNS;
      if (forward > mirrored) return MIRRORED_CENTER_FIRST_COLUMNS;
    }
  }
  return CENTER_FIRST_COLUMNS;
}

function smokeExamples() {
  const base = createInitialBoard();
  const makePosition = (
    entries: readonly (readonly [number, Cell])[],
    movesRemaining: number,
  ): LearnedEvaluatorPosition => {
    const board = base.slice();
    for (const [index, cell] of entries) board[index] = cell;
    return { board, movesRemaining };
  };
  return [
    { position: makePosition([], 5), target: -15_000 },
    { position: makePosition([[35, 4]], 4), target: 22_000 },
    {
      position: makePosition(
        [
          [35, 2],
          [36, 3],
        ],
        3,
      ),
      target: 48_000,
    },
    {
      position: makePosition(
        [
          [28, 6],
          [35, 2],
          [36, 3],
          [37, 5],
        ],
        1,
      ),
      target: 91_000,
    },
  ];
}

function smokePolicyExamples(): EpisodeStep[] {
  const makeStep = (
    nextDisc: GameState["nextDisc"],
    teacherColumn: number,
    entries: readonly (readonly [number, Cell])[] = [],
  ) => {
    const board = createInitialBoard().slice();
    for (const [index, cell] of entries) board[index] = cell;
    const state: GameState = {
      board,
      nextDisc,
      score: 0,
      level: 1,
      movesRemaining: MOVES_PER_LEVEL,
      movesPlayed: 0,
      gameOver: false,
    };
    return {
      state,
      position: { board, movesRemaining: state.movesRemaining },
      teacherColumn,
    };
  };
  return [
    makeStep(1, 1),
    makeStep(7, 5, [[42, 2]]),
    makeStep(4, 3, [
      [42, SOLID],
      [43, 2],
      [44, 5],
    ]),
  ];
}

function meanExampleLoss(
  model: TrainableModel,
  examples: readonly {
    position: LearnedEvaluatorPosition;
    target: number;
  }[],
) {
  return mean(
    examples.map((example) =>
      huberLoss(
        forwardPass(example.position, model).output -
          example.target / TARGET_SCALE,
      ),
    ),
  );
}

function combinedValue(position: LearnedEvaluatorPosition) {
  return evaluateHeuristic(
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

/**
 * Fixed training teacher. It augments the runtime combined heuristic with
 * board-shape risks: high covered discs
 * (especially on weakly connected edges), elevated low numbers, excessive
 * peak height, and rough neighboring columns. The learned model distills only
 * this correction and excludes the combined baseline from its target.
 */
function evaluateTeacherValue(state: GameState) {
  if (state.gameOver) return -500_000;
  const heights = teacherColumnHeights(state.board);
  let coveredHeightRisk = 0;
  let lowNumberHeightRisk = 0;

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = state.board[row * BOARD_SIZE + column];
      const edgeMultiplier = column === 0 || column === 6 ? 1.65 : 1;
      if (cell === SOLID) {
        coveredHeightRisk += elevation ** 2 * edgeMultiplier;
      } else if (cell === CRACKED) {
        coveredHeightRisk += elevation ** 2 * edgeMultiplier * 0.72;
      } else if (cell === 1 || cell === 2) {
        lowNumberHeightRisk += Math.max(0, elevation - 2) ** 2;
      }
    }
  }

  const maximumHeight = Math.max(...heights);
  const dangerHeight = Math.max(0, maximumHeight - 4);
  let roughness = 0;
  for (let column = 1; column < BOARD_SIZE; column += 1) {
    roughness += Math.abs(heights[column] - heights[column - 1]);
  }

  return (
    evaluateHeuristic(state, "combined") -
    coveredHeightRisk * 95 -
    lowNumberHeightRisk * 85 -
    dangerHeight ** 2 * 1_250 -
    roughness * 90
  );
}

function teacherColumnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

function mirrorBoard(board: Board): Board {
  const mirrored = Array<Cell>(board.length).fill(EMPTY);
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      mirrored[row * BOARD_SIZE + BOARD_SIZE - 1 - column] =
        board[row * BOARD_SIZE + column];
    }
  }
  return mirrored;
}

function shuffle(values: number[], random: () => number) {
  for (let index = values.length - 1; index > 0; index -= 1) {
    const other = Math.floor(random() * (index + 1));
    [values[index], values[other]] = [values[other], values[index]];
  }
}

function huberLoss(error: number) {
  const magnitude = Math.abs(error);
  return magnitude <= HUBER_DELTA
    ? 0.5 * error * error
    : HUBER_DELTA * (magnitude - 0.5 * HUBER_DELTA);
}

function huberDerivative(error: number) {
  return Math.max(-HUBER_DELTA, Math.min(HUBER_DELTA, error));
}

function clipGradient(value: number) {
  return Math.max(-GRADIENT_CLIP, Math.min(GRADIENT_CLIP, value));
}

function relu(value: number) {
  return value > 0 ? value : 0;
}

function multiplyAdd(sum: number, left: number, right: number) {
  return Math.fround(sum + Math.fround(left * right));
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

function mean(values: readonly number[]) {
  return values.length === 0
    ? 0
    : values.reduce((sum, value) => sum + value, 0) / values.length;
}

function parseBoundedPositiveInteger(
  value: string,
  flag: string,
  maximum: number,
) {
  const parsed = parseNonNegativeInteger(value, flag);
  if (parsed < 1 || parsed > maximum) {
    throw new Error(`${flag} must be an integer from 1 to ${maximum}`);
  }
  return parsed;
}

function parseNonNegativeInteger(value: string, flag: string) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 0) {
    throw new Error(`${flag} must be a non-negative integer`);
  }
  return parsed;
}

function parsePositiveFinite(value: string, flag: string) {
  const parsed = parseFinite(value, flag);
  if (parsed <= 0) throw new Error(`${flag} must be greater than zero`);
  return parsed;
}

function parseFinite(value: string, flag: string) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) throw new Error(`${flag} must be finite`);
  return parsed;
}

function formatTrainingProgress(
  games: number,
  completed: number,
  capped: number,
  positions: number,
  meanLoss: number | null,
  learnedFraction: number,
) {
  return `train ${games} · complete ${completed} · capped ${capped} · positions ${positions} · loss ${meanLoss === null ? "n/a" : meanLoss.toFixed(5)} · learned mix ${(learnedFraction * 100).toFixed(0)}%`;
}

function formatValidation(
  validation: ValidationResult,
  options: TrainingArguments,
) {
  const scoreDelta = Math.round(validation.pairedMeanScoreDelta);
  const moveDelta = validation.pairedMeanMoveDelta;
  return `validation · learned ${formatValidationSide(validation.learned, options.validationGames)} · combined ${formatValidationSide(validation.baseline, options.validationGames)} · teacher ${formatValidationSide(validation.teacher, options.validationGames)} · paired delta ${scoreDelta >= 0 ? "+" : ""}${scoreDelta.toLocaleString("en-US")} score / ${moveDelta >= 0 ? "+" : ""}${moveDelta.toFixed(1)} moves`;
}

function formatValidationSide(side: ValidationSide, games: number) {
  return `${Math.round(side.meanScore).toLocaleString("en-US")} score / ${side.meanMoves.toFixed(1)} moves / ${side.completedGames}/${games} terminal`;
}

function formatSeed(seed: number) {
  return `0x${(seed >>> 0).toString(16).padStart(8, "0")}`;
}

function formatSignedInteger(value: number) {
  const rounded = Math.round(value);
  return `${rounded >= 0 ? "+" : ""}${rounded.toLocaleString("en-US")}`;
}

function helpText() {
  return `Drop7 offline learned value/policy trainer

Usage:
  node --experimental-strip-types approaches/value-policy-learning/value-model/train.ts [options]

Options:
  --games N              Training games from the fixed training range (default: 150)
  --samples N            Paired reveal samples per one-ply policy move (default: 3)
  --warmup N             Combined-heuristic-only games (default: 25)
  --ramp N               Games to linearly blend in the learned value (default: 75)
  --lr N                 Adam learning rate (default: 0.001)
  --terminal N           Negative terminal utility (default: -250000)
  --max-moves N          Per-game cap; sibling labels remain usable (default: 500)
  --epsilon N            Deterministic exploration probability (default: 0.08)
  --validation-games N   Fixed validation seeds per checkpoint (default: 64)
  --calibration-games N  Disjoint contrastive probe seeds (default: 32)
  --validate-every N     Training games between validation reports (default: 25)
  --teacher NAME         shape, oracle, fitted, or contrastive (default: shape)
  --oracle-depth N       Oracle lookahead used only for labels (default: 4)
  --oracle-beam N        Oracle beam width used only for labels (default: 128)
  --fitted-horizon N     Fair continuation moves, up to 80 (default: 40)
  --states-per-game N    Evenly sampled fitted states/game (default: 8)
  --iterations N         On-policy fitted-value passes (default: 2)
  --epochs N             Label passes per completed trajectory (default: 2)
  --contrastive-margin N Oracle-state ranking margin (default: 10000)
  --contrastive-temperature N  Pairwise loss temperature (default: 25000)
  --validate-rollout     Also run paired 4x12-ply leaf validation
  --validate-sparse      Also run sparse d3/s4 leaf validation
  --evaluate-only        Evaluate a frozen --resume model without training
  --export-last          Export final weights instead of safe best checkpoint
  --output PATH          JSON model destination; '-' writes JSON to stdout
  --resume PATH          Resume weights from a validated JSON model
  --self-test            Fast loss/export/symmetry/determinism smoke test
  --help                 Show this help

Training seeds begin at ${formatSeed(TRAINING_SEED_START)}, contrastive calibration seeds at
${formatSeed(CALIBRATION_SEED_START)}, and validation seeds at ${formatSeed(VALIDATION_SEED_START)}.
Upcoming discs and reveal streams are keyed by seed and move, so learned and
baseline validation games are paired. Shape training
distills fixed board-shape corrections, oracle training learns a direct action
head, fitted training learns centered long-continuation corrections, and
contrastive training ranks matched oracle states over fair-policy states.
state.score is always zeroed so points earned before a position can never leak
into its value.
`;
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  void runCli(process.argv.slice(2)).catch((error: unknown) => {
    const message = error instanceof Error ? error.stack ?? error.message : String(error);
    process.stderr.write(`${message}\n`);
    process.exitCode = 1;
  });
}
