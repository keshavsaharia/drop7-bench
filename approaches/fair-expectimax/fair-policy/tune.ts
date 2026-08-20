import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  CLEAR_BONUS,
  CRACKED,
  EMPTY,
  LEVEL_BONUS,
  MOVES_PER_LEVEL,
  SOLID,
  createInitialBoard,
  contiguousLineLength,
  playMove,
  placeDisc,
  seededRandom,
  type Board,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import {
  extractHeuristicFeatures,
  type HeuristicFeatures,
} from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { planOracleMove } from "../../oracle-curriculum/perfect-information-oracle/main.ts";

/**
 * Fair-policy laboratory for a one-ply, sampled Drop7 policy.
 *
 * The environment seed is deliberately absent from chooseMove(). Planner
 * samples are a pure function of the observable position and a fixed policy
 * seed, so neither training nor deployment can peek at future game randomness.
 */

export const FAIR_TUNER_FORMAT = "drop7-fair-cem" as const;
export const FAIR_TUNER_VERSION = 1 as const;
export const TRAINING_SEED_START = 0x1d70_0000;
export const VALIDATION_SEED_START = 0x7d70_0000;
export const RESERVED_FINAL_SEED_START = 0xd700_0000;

const DEFAULT_GENERATIONS = 10;
const DEFAULT_POPULATION = 24;
const DEFAULT_ELITES = 6;
const DEFAULT_TRAINING_GAMES = 16;
const DEFAULT_VALIDATION_GAMES = 64;
const DEFAULT_POLICY_SAMPLES = 3;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_TUNER_SEED = 0xc3e0_2026;
const DEFAULT_POLICY_SEED = 0xfa17_d707;
const DEFAULT_OUTPUT = "drop7-fair-weights.json";
const DEFAULT_ORACLE_DEPTH = 10;
const DEFAULT_ORACLE_BEAM = 64;
const DEFAULT_IMITATION_EPOCHS = 120;
const DEFAULT_IMITATION_LEARNING_RATE = 0.025;
const DEFAULT_IMITATION_TEMPERATURE = 5_000;
const DEFAULT_DAGGER_ROUNDS = 3;
const DEFAULT_ROLLOUTS = 64;
const DEFAULT_ROLLOUT_HORIZON = 6;
const DEFAULT_CONTINUATION_SAMPLES = 2;
const DEFAULT_POLICY_ITERATION_SCENARIOS = 12;
const DEFAULT_POLICY_ITERATION_HORIZON = 60;
const DEFAULT_CRITICAL_STATES_PER_GAME = 16;
const MAX_GAMES = 10_000;
const MAX_POPULATION = 1_000;
const MAX_GENERATIONS = 10_000;
const MAX_POLICY_SAMPLES = 32;
const MAX_MOVES = 10_000;
const SCORE_TARGET = 1_000_000;
const SCORE_CAP = 2_500_000;
const TERMINAL_UTILITY = -2_500_000;
const CEM_UPDATE_RATE = 0.72;
const MINIMUM_STD_FRACTION = 0.06;

const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const POLICY_REVEAL_DOMAIN = 0x5052_564c;
const POLICY_DISC_DOMAIN = 0x5044_4953;
const POLICY_SAMPLE_MULTIPLIER = 0x9e37_79b9;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const CANDIDATE_DOMAIN = 0x4341_4e44;
const ROLLOUT_REVEAL_DOMAIN = 0x5252_564c;
const ROLLOUT_DISC_DOMAIN = 0x5244_4953;
const ROLLOUT_PLY_MULTIPLIER = 0xc2b2_ae35;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;

const PARAMETER_SPECS = [
  parameter("immediateScore", 1, 0.35, 0.1, 3),
  parameter("clearedDiscValue", 0, 130, -500, 1_200),
  parameter("revealedCoverValue", 120, 180, -500, 1_800),
  parameter("chainDepthValue", 120, 300, -1_000, 3_000),
  parameter("openColumns", 180, 150, -200, 1_200),
  parameter("heightLoad", -10, 8, -80, 10),
  parameter("solidCells", -620, 350, -3_000, 200),
  parameter("crackedCells", -220, 220, -1_500, 400),
  parameter("numberedCells", -18, 45, -400, 200),
  parameter("highLowNumbers", -90, 100, -900, 300),
  parameter("directPotential", 140, 160, -600, 1_200),
  parameter("latentChainPotential", 360, 320, -1_000, 2_500),
  parameter("crackedExposure", 100, 150, -500, 1_200),
  parameter("solidExposure", 40, 110, -500, 800),
  parameter("adjacentOnes", -550, 350, -2_500, 500),
  parameter("tripleTwos", -750, 450, -3_000, 500),
  parameter("deadLowNumbers", -120, 180, -1_200, 500),
  parameter("coveredHeightRisk", -95, 75, -600, 100),
  parameter("lowNumberHeightRisk", -85, 75, -600, 100),
  parameter("dangerHeightSquared", -1_250, 700, -6_000, 100),
  parameter("roughness", -90, 100, -900, 300),
  parameter("risePressure", -35, 35, -300, 100),
  parameter("nextDiscVerticalOptions", 220, 220, -600, 1_500),
  parameter("landingHeight", -80, 140, -1_200, 800),
  parameter("landingHeightSquared", -20, 45, -400, 200),
  parameter("centerDistance", -20, 100, -800, 800),
  parameter("neighborHeightGap", -40, 100, -800, 500),
  parameter("coveredInColumn", -100, 180, -1_200, 800),
  parameter("lowNumbersInColumn", -100, 180, -1_200, 800),
  parameter("verticalBuildDistance", -40, 100, -800, 500),
  parameter("verticalOvershoot", -200, 180, -1_500, 500),
  parameter("horizontalBuildDistance", -40, 100, -800, 500),
  parameter("horizontalOvershoot", -200, 180, -1_500, 500),
  parameter("oneMoveFromTrigger", 180, 220, -800, 1_500),
  parameter("imminentRiseHeight", -120, 180, -1_500, 500),
] as const;

type ParameterName = (typeof PARAMETER_SPECS)[number]["name"];
export type FairPolicyWeights = Readonly<Record<ParameterName, number>>;

interface ParameterSpec<Name extends string = string> {
  name: Name;
  initialMean: number;
  initialStd: number;
  minimum: number;
  maximum: number;
}

export interface TunerArguments {
  generations: number;
  population: number;
  elites: number;
  trainingGames: number;
  validationGames: number;
  policySamples: number;
  maxMoves: number;
  tunerSeed: number;
  policySeed: number;
  outputPath: string;
  resumePath?: string;
  evaluatePath?: string;
  oracleGames: number;
  oracleDepth: number;
  oracleBeam: number;
  imitationEpochs: number;
  imitationLearningRate: number;
  imitationTemperature: number;
  daggerRounds: number;
  rolloutValidation: boolean;
  rollouts: number;
  rolloutHorizon: number;
  continuationSamples: number;
  policyIterationRounds: number;
  policyIterationScenarios: number;
  policyIterationHorizon: number;
  criticalStatesPerGame: number;
  selfTest: boolean;
}

interface Distribution {
  means: number[];
  standardDeviations: number[];
}

interface PolicyGameResult {
  seed: number;
  score: number;
  moves: number;
  finalLevel: number;
  gameOver: boolean;
  censored: boolean;
  clears: number;
  maxChain: number;
}

interface PolicySummary {
  games: number;
  meanObjective: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanClears: number;
  meanMaxChain: number;
  results: readonly PolicyGameResult[];
}

interface CandidateEvaluation {
  weights: FairPolicyWeights;
  vector: number[];
  summary: PolicySummary;
}

interface PairedValidation {
  baseline: Omit<PolicySummary, "results">;
  candidate: Omit<PolicySummary, "results">;
  pairedMeanScoreDelta: number;
  pairedMedianScoreDelta: number;
  pairedMeanMoveDelta: number;
  wins: number;
  ties: number;
  losses: number;
}

interface OracleTrainingExample {
  actions: readonly ActionFeatures[];
  teacherColumn: number;
  importance: number;
}

interface ImitationStats {
  loss: number;
  accuracy: number;
}

interface OracleTeacherMetadata {
  games: number;
  examples: number;
  depth: number;
  beamWidth: number;
  daggerRounds: number;
  lastBehaviorSummary: Omit<PolicySummary, "results">;
  imitationLoss: number;
  imitationAccuracy: number;
}

interface PolicyIterationMetadata {
  rounds: number;
  examples: number;
  scenarios: number;
  horizon: number;
  criticalStatesPerGame: number;
  lastBehaviorSummary: Omit<PolicySummary, "results">;
  imitationLoss: number;
  imitationAccuracy: number;
}

export interface FairTunerArtifact {
  format: typeof FAIR_TUNER_FORMAT;
  version: typeof FAIR_TUNER_VERSION;
  method: "cem" | "oracle-distillation" | "policy-iteration";
  generation: number;
  tunerSeed: number;
  policySeed: number;
  policySamples: number;
  trainingGames: number;
  maxMoves: number;
  trainingSeedStart: number;
  validationSeedStart: number;
  parameterNames: readonly ParameterName[];
  distribution: Distribution;
  champion: {
    weights: FairPolicyWeights;
    training: Omit<PolicySummary, "results">;
  };
  validation?: PairedValidation;
  rolloutValidation?: PairedValidation;
  teacher?: OracleTeacherMetadata;
  policyIteration?: PolicyIterationMetadata;
}

interface PositionTerms {
  heuristic: HeuristicFeatures;
  coveredHeightRisk: number;
  lowNumberHeightRisk: number;
  dangerHeightSquared: number;
  roughness: number;
  risePressure: number;
  nextDiscVerticalOptions: number;
}

interface PolicySettings {
  samples: number;
  policySeed: number;
  rollout?: {
    count: number;
    horizon: number;
    continuationSamples: number;
    riskAversion?: number;
  };
}

interface ActionFeatures {
  column: number;
  values: number[];
  fixedUtility: number;
}

function parameter<Name extends string>(
  name: Name,
  initialMean: number,
  initialStd: number,
  minimum: number,
  maximum: number,
): ParameterSpec<Name> {
  return { name, initialMean, initialStd, minimum, maximum };
}

export function initialFairPolicyWeights(): FairPolicyWeights {
  return vectorToWeights(PARAMETER_SPECS.map((spec) => spec.initialMean));
}

export function runFairPolicyGame(
  seed: number,
  weights: FairPolicyWeights,
  settings: PolicySettings,
  maxMoves: number,
): PolicyGameResult {
  const gameSeed = unsignedSeed(seed, "game seed");
  const moveCap = boundedPositiveInteger(maxMoves, "maxMoves", MAX_MOVES);
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(gameSeed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let clears = 0;
  let maxChain = 0;

  while (!state.gameOver && state.movesPlayed < moveCap) {
    // The environment seed is intentionally not passed into chooseFairMove.
    const column = chooseFairMove(state, weights, settings);
    if (column === null) {
      throw new Error("Fair policy found no legal move in a live game");
    }

    const step = playActualGameMove(state, column, gameSeed);
    if (!step) throw new Error(`Fair policy chose illegal column ${column}`);

    clears += clearCount(step.move);
    maxChain = Math.max(maxChain, step.move.waves.length);
    state = step.state;
  }

  return {
    seed: gameSeed,
    score: state.score,
    moves: state.movesPlayed,
    finalLevel: state.level,
    gameOver: state.gameOver,
    censored: !state.gameOver,
    clears,
    maxChain,
  };
}

function playActualGameMove(
  state: GameState,
  column: number,
  gameSeed: number,
) {
  const revealSeed = mix32(
    gameSeed ^
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
        nextDisc: headlessDisc(gameSeed, move.state.movesPlayed),
      };
  return { move, state: nextState };
}

/**
 * One-ply sampled policy. Its only sources of entropy are the observable
 * position and policySeed; future environment randomness cannot enter here.
 */
export function chooseFairMove(
  state: GameState,
  weights: FairPolicyWeights,
  settings: PolicySettings,
) {
  if (state.gameOver) return null;
  if (settings.rollout) {
    return chooseFairRolloutMove(state, weights, settings);
  }
  const samples = boundedPositiveInteger(
    settings.samples,
    "policy samples",
    MAX_POLICY_SAMPLES,
  );
  const policySeed = unsignedSeed(settings.policySeed, "policy seed");
  const observable = canonicalObservableState(state);
  const weightVector = weightsToVector(weights);
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;

  for (const column of columnOrder(observable.mirrored)) {
    if (state.board[column] !== EMPTY) continue;
    const action = extractActionFeatures(
      state,
      column,
      samples,
      policySeed,
      observable.hash,
    );
    const value = scoreAction(action, weightVector);
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }

  return bestColumn;
}

/**
 * Fair policy-rollout improvement: sampled root futures use solver-local
 * chance tapes, while every continuation decision is made by the seed-blind
 * one-ply student from the then-observable state.
 */
function chooseFairRolloutMove(
  state: GameState,
  weights: FairPolicyWeights,
  settings: PolicySettings,
) {
  const rollout = settings.rollout!;
  const observable = canonicalObservableState(state);
  const continuationSettings: PolicySettings = {
    samples: settings.samples,
    policySeed: settings.policySeed,
  };
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;

  for (const rootColumn of columnOrder(observable.mirrored)) {
    if (state.board[rootColumn] !== EMPTY) continue;
    let value = 0;
    let squaredValue = 0;
    for (let scenario = 0; scenario < rollout.count; scenario += 1) {
      let position = state;
      let column: number = rootColumn;
      let reward = 0;
      let terminal = false;
      for (let ply = 0; ply < rollout.horizon; ply += 1) {
        const reveal = rolloutSample(
          observable.hash,
          settings.policySeed,
          scenario,
          rollout.count,
          ply,
          ROLLOUT_REVEAL_DOMAIN,
        );
        const move = playMove(position, column, () => reveal, {
          captureAnimation: false,
        });
        if (!move) {
          reward += TERMINAL_UTILITY;
          terminal = true;
          break;
        }
        reward += move.scoreDelta;
        if (move.state.gameOver) {
          reward += TERMINAL_UTILITY;
          terminal = true;
          break;
        }
        position = {
          ...move.state,
          score: 0,
          nextDisc: rolloutDisc(
            observable.hash,
            settings.policySeed,
            scenario,
            rollout.count,
            ply + 1,
          ),
        };
        if (ply + 1 < rollout.horizon) {
          const continuation = chooseFairMove(
            position,
            weights,
            continuationSettings,
          );
          if (continuation === null) {
            reward += TERMINAL_UTILITY;
            terminal = true;
            break;
          }
          column = continuation;
        }
      }
      const scenarioValue =
        reward + (terminal ? 0 : evaluateFairPosition(position, weights));
      value += scenarioValue;
      squaredValue += scenarioValue * scenarioValue;
    }
    value /= rollout.count;
    const variance = Math.max(0, squaredValue / rollout.count - value * value);
    const selectionValue =
      value - (rollout.riskAversion ?? 0) * Math.sqrt(variance);
    if (selectionValue > bestValue) {
      bestValue = selectionValue;
      bestColumn = rootColumn;
    }
  }
  return bestColumn;
}

export function evaluateFairPosition(
  state: GameState,
  weights: FairPolicyWeights,
) {
  if (state.gameOver) return TERMINAL_UTILITY;
  return dot(positionFeatureVector(state), weightsToVector(weights));
}

function extractActionFeatures(
  state: GameState,
  column: number,
  samples: number,
  policySeed: number,
  observableHash = canonicalObservableState(state).hash,
): ActionFeatures {
  const values = actionSpecificFeatureVector(state, column);
  let fixedUtility = 0;

  for (let sample = 0; sample < samples; sample += 1) {
    // A constant reveal value within a probe preserves exact reflection
    // symmetry even when mirrored cover indexes are visited in reverse order.
    const reveal = policySample(
      observableHash,
      policySeed,
      sample,
      samples,
      POLICY_REVEAL_DOMAIN,
    );
    const move = playMove(state, column, () => reveal, {
      captureAnimation: false,
    });
    if (!move) continue;
    addVector(values, transitionFeatureVector(move), 1 / samples);
    if (move.state.gameOver) {
      fixedUtility += TERMINAL_UTILITY / samples;
      continue;
    }
    const nextState: GameState = {
      ...move.state,
      score: 0,
      nextDisc: policyDisc(
        observableHash,
        policySeed,
        sample,
        samples,
      ),
    };
    addVector(values, positionFeatureVector(nextState), 1 / samples);
  }
  return { column, values, fixedUtility };
}

function transitionFeatureVector(move: MoveResult) {
  let cleared = 0;
  let revealed = 0;
  for (const wave of move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  const chainDepth = move.waves.length;
  return [
    move.scoreDelta,
    cleared,
    revealed,
    Math.max(0, chainDepth - 1) ** 2,
    ...Array<number>(PARAMETER_SPECS.length - 4).fill(0),
  ];
}

function positionFeatureVector(state: GameState) {
  const terms = extractPositionTerms(state);
  const features = terms.heuristic;
  return [
    0,
    0,
    0,
    0,
    features.openColumns,
    features.heightLoad,
    features.solidCells,
    features.crackedCells,
    features.numberedCells,
    features.highLowNumbers,
    features.directPotential,
    features.latentChainPotential,
    features.crackedExposure,
    features.solidExposure,
    features.adjacentOnes,
    features.tripleTwos,
    features.deadLowNumbers,
    terms.coveredHeightRisk,
    terms.lowNumberHeightRisk,
    terms.dangerHeightSquared,
    terms.roughness,
    terms.risePressure,
    terms.nextDiscVerticalOptions,
    ...Array<number>(PARAMETER_SPECS.length - 23).fill(0),
  ];
}

function actionSpecificFeatureVector(state: GameState, column: number) {
  const values = Array<number>(PARAMETER_SPECS.length).fill(0);
  const heights = columnHeights(state.board);
  const landingHeight = heights[column] + 1;
  const leftHeight = column > 0 ? heights[column - 1] : landingHeight;
  const rightHeight =
    column + 1 < BOARD_SIZE ? heights[column + 1] : landingHeight;
  let coveredInColumn = 0;
  let lowNumbersInColumn = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const cell = state.board[row * BOARD_SIZE + column];
    if (cell === SOLID || cell === CRACKED) coveredInColumn += 1;
    if (cell === 1 || cell === 2) lowNumbersInColumn += 1;
  }

  const placed = placeDisc(state.board, column, state.nextDisc);
  const landingRow = BOARD_SIZE - landingHeight;
  const horizontalLength = placed
    ? contiguousLineLength(placed, landingRow, column, "row")
    : 0;
  const verticalBuildDistance = Math.max(0, state.nextDisc - landingHeight);
  const verticalOvershoot = Math.max(0, landingHeight - state.nextDisc);
  const horizontalBuildDistance = Math.max(
    0,
    state.nextDisc - horizontalLength,
  );
  const horizontalOvershoot = Math.max(
    0,
    horizontalLength - state.nextDisc,
  );
  const triggerDistance = Math.min(
    Math.abs(state.nextDisc - landingHeight),
    Math.abs(state.nextDisc - horizontalLength),
  );

  const offset = 23;
  values[offset] = landingHeight;
  values[offset + 1] = landingHeight ** 2;
  values[offset + 2] = Math.abs(column - Math.floor(BOARD_SIZE / 2));
  values[offset + 3] =
    Math.abs(landingHeight - leftHeight) +
    Math.abs(landingHeight - rightHeight);
  values[offset + 4] = coveredInColumn;
  values[offset + 5] = lowNumbersInColumn;
  values[offset + 6] = verticalBuildDistance;
  values[offset + 7] = verticalOvershoot;
  values[offset + 8] = horizontalBuildDistance;
  values[offset + 9] = horizontalOvershoot;
  values[offset + 10] = triggerDistance === 1 ? 1 : 0;
  values[offset + 11] =
    state.movesRemaining === 1 ? landingHeight ** 2 : 0;
  return values;
}

function scoreAction(action: ActionFeatures, weights: readonly number[]) {
  return action.fixedUtility + dot(action.values, weights);
}

function extractPositionTerms(state: GameState): PositionTerms {
  const heights = columnHeights(state.board);
  let coveredHeightRisk = 0;
  let lowNumberHeightRisk = 0;
  let risePressure = 0;
  let nextDiscVerticalOptions = 0;

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const height = heights[column];
    risePressure += height ** 3 / state.movesRemaining;
    if (height < BOARD_SIZE && height + 1 === state.nextDisc) {
      nextDiscVerticalOptions += 1;
    }
  }

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

  let roughness = 0;
  for (let column = 1; column < BOARD_SIZE; column += 1) {
    roughness += Math.abs(heights[column] - heights[column - 1]);
  }
  const dangerHeightSquared = Math.max(0, Math.max(...heights) - 4) ** 2;

  return {
    heuristic: extractHeuristicFeatures(state),
    coveredHeightRisk,
    lowNumberHeightRisk,
    dangerHeightSquared,
    roughness,
    risePressure,
    nextDiscVerticalOptions,
  };
}

function evaluatePolicy(
  weights: FairPolicyWeights,
  seeds: readonly number[],
  settings: PolicySettings,
  maxMoves: number,
): PolicySummary {
  const results = seeds.map((seed) =>
    runFairPolicyGame(seed, weights, settings, maxMoves),
  );
  return summarizeResults(results, maxMoves);
}

function summarizeResults(
  results: readonly PolicyGameResult[],
  maxMoves: number,
): PolicySummary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    games: results.length,
    meanObjective: mean(
      results.map((result) => boundedGameObjective(result, maxMoves)),
    ),
    meanScore: mean(scores),
    medianScore: percentile(scores, 0.5),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censoredGames: results.filter((result) => result.censored).length,
    meanClears: mean(results.map((result) => result.clears)),
    meanMaxChain: mean(results.map((result) => result.maxChain)),
    results,
  };
}

/** Bounded [0, 1] target with explicit capped-game and chain components. */
function boundedGameObjective(result: PolicyGameResult, maxMoves: number) {
  const targetScore = Math.min(result.score, SCORE_TARGET) / SCORE_TARGET;
  const cappedScore = Math.min(result.score, SCORE_CAP) / SCORE_CAP;
  const survival = Math.min(result.moves, maxMoves) / maxMoves;
  const censoring = result.censored ? 1 : 0;
  const clears = Math.min(result.clears, 5) / 5;
  const chain = Math.min(result.maxChain, 20) / 20;
  return (
    targetScore * 0.35 +
    cappedScore * 0.1 +
    survival * 0.35 +
    censoring * 0.15 +
    clears * 0.025 +
    chain * 0.025
  );
}

export async function tuneFairPolicy(options: TunerArguments) {
  const trainingSeeds = consecutiveSeeds(
    TRAINING_SEED_START,
    options.trainingGames,
  );
  const validationSeeds = consecutiveSeeds(
    VALIDATION_SEED_START,
    options.validationGames,
  );
  const settings: PolicySettings = {
    samples: options.policySamples,
    policySeed: options.policySeed,
  };
  const initialWeights = initialFairPolicyWeights();
  const baseline = evaluatePolicy(
    initialWeights,
    trainingSeeds,
    settings,
    options.maxMoves,
  );
  let distribution = initialDistribution();
  let completedGenerations = 0;
  let champion: CandidateEvaluation = {
    weights: initialWeights,
    vector: weightsToVector(initialWeights),
    summary: baseline,
  };

  if (options.resumePath) {
    const artifact = await readArtifact(options.resumePath);
    assertCompatibleArtifact(artifact, options);
    distribution = copyDistribution(artifact.distribution);
    completedGenerations = artifact.generation;
    const resumedWeights = artifact.champion.weights;
    champion = {
      weights: resumedWeights,
      vector: weightsToVector(resumedWeights),
      summary: evaluatePolicy(
        resumedWeights,
        trainingSeeds,
        settings,
        options.maxMoves,
      ),
    };
  }

  process.stdout.write(
    `fair CEM · training ${formatSeedRange(trainingSeeds)} · validation ${formatSeedRange(validationSeeds)} · final range ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  process.stdout.write(
    `baseline · ${formatSummary(baseline)} · objective ${baseline.meanObjective.toFixed(5)}\n`,
  );

  const evaluationCache = new Map<string, PolicySummary>();
  evaluationCache.set(vectorKey(champion.vector), champion.summary);
  evaluationCache.set(
    vectorKey(weightsToVector(initialWeights)),
    baseline,
  );

  for (
    let generation = completedGenerations;
    generation < options.generations;
    generation += 1
  ) {
    const vectors = createPopulation(
      distribution,
      champion.vector,
      options.population,
      options.tunerSeed,
      generation,
    );
    const candidates = vectors.map((vector): CandidateEvaluation => {
      const key = vectorKey(vector);
      let summary = evaluationCache.get(key);
      if (!summary) {
        summary = evaluatePolicy(
          vectorToWeights(vector),
          trainingSeeds,
          settings,
          options.maxMoves,
        );
        evaluationCache.set(key, summary);
      }
      return { vector, weights: vectorToWeights(vector), summary };
    });
    candidates.sort(compareCandidates);
    const elites = candidates.slice(0, options.elites);
    distribution = updateDistribution(distribution, elites);
    if (compareCandidates(elites[0], champion) < 0) champion = elites[0];

    process.stdout.write(
      `generation ${(generation + 1).toString().padStart(2)} · ${formatSummary(elites[0].summary)} · objective ${elites[0].summary.meanObjective.toFixed(5)} · champion ${champion.summary.meanObjective.toFixed(5)}\n`,
    );

    await writeArtifact(options.outputPath, {
      format: FAIR_TUNER_FORMAT,
      version: FAIR_TUNER_VERSION,
      method: "cem",
      generation: generation + 1,
      tunerSeed: options.tunerSeed,
      policySeed: options.policySeed,
      policySamples: options.policySamples,
      trainingGames: options.trainingGames,
      maxMoves: options.maxMoves,
      trainingSeedStart: TRAINING_SEED_START,
      validationSeedStart: VALIDATION_SEED_START,
      parameterNames: parameterNames(),
      distribution,
      champion: {
        weights: champion.weights,
        training: omitResults(champion.summary),
      },
    });
  }

  const validation = pairedValidation(
    initialWeights,
    champion.weights,
    validationSeeds,
    settings,
    options.maxMoves,
  );
  const policyRolloutValidation = options.rolloutValidation
    ? pairedValidation(
        initialFairPolicyWeights(),
        champion.weights,
        validationSeeds,
        withPolicyRollout(settings, options),
        options.maxMoves,
      )
    : undefined;
  const artifact: FairTunerArtifact = {
    format: FAIR_TUNER_FORMAT,
    version: FAIR_TUNER_VERSION,
    method: "cem",
    generation: Math.max(completedGenerations, options.generations),
    tunerSeed: options.tunerSeed,
    policySeed: options.policySeed,
    policySamples: options.policySamples,
    trainingGames: options.trainingGames,
    maxMoves: options.maxMoves,
    trainingSeedStart: TRAINING_SEED_START,
    validationSeedStart: VALIDATION_SEED_START,
    parameterNames: parameterNames(),
    distribution,
    champion: {
      weights: champion.weights,
      training: omitResults(champion.summary),
    },
    validation,
    ...(policyRolloutValidation === undefined
      ? {}
      : { rolloutValidation: policyRolloutValidation }),
  };
  await writeArtifact(options.outputPath, artifact);

  process.stdout.write(`validation baseline · ${formatSummary(validation.baseline)}\n`);
  process.stdout.write(`validation winner   · ${formatSummary(validation.candidate)}\n`);
  process.stdout.write(
    `paired delta ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
  );
  if (policyRolloutValidation) {
    process.stdout.write(
      `policy-rollout baseline · ${formatSummary(policyRolloutValidation.baseline)}\n`,
    );
    process.stdout.write(
      `policy-rollout student  · ${formatSummary(policyRolloutValidation.candidate)}\n`,
    );
    process.stdout.write(
      `rollout paired delta ${signedInteger(policyRolloutValidation.pairedMeanScoreDelta)} points · ${signedNumber(policyRolloutValidation.pairedMeanMoveDelta, 1)} moves · W/T/L ${policyRolloutValidation.wins}/${policyRolloutValidation.ties}/${policyRolloutValidation.losses}\n`,
    );
  }
  process.stdout.write(`checkpoint ${resolve(options.outputPath)}\n`);
  return artifact;
}

export async function distillOraclePolicy(options: TunerArguments) {
  const trainingSeeds = consecutiveSeeds(
    TRAINING_SEED_START,
    options.oracleGames,
  );
  const validationSeeds = consecutiveSeeds(
    VALIDATION_SEED_START,
    options.validationGames,
  );
  const settings: PolicySettings = {
    samples: options.policySamples,
    policySeed: options.policySeed,
  };
  let startingWeights = initialFairPolicyWeights();
  if (options.resumePath) {
    const artifact = await readArtifact(options.resumePath);
    startingWeights = artifact.champion.weights;
  }

  process.stdout.write(
    `oracle teacher (UNFAIR, training only) · ${formatSeedRange(trainingSeeds)} · depth ${options.oracleDepth} · beam ${options.oracleBeam}\n`,
  );
  process.stdout.write(
    `runtime student is mirror-safe and seed-blind · validation ${formatSeedRange(validationSeeds)} · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  const examples: OracleTrainingExample[] = [];
  let lastBehaviorSummary: PolicySummary | undefined;
  let distilled = {
    weights: startingWeights,
    stats: { loss: Number.POSITIVE_INFINITY, accuracy: 0 },
  };
  for (let round = 1; round <= options.daggerRounds; round += 1) {
    const dataset = createOracleDataset(
      trainingSeeds,
      settings,
      options.maxMoves,
      options.oracleDepth,
      options.oracleBeam,
      distilled.weights,
      `DAgger ${round}`,
    );
    examples.push(...dataset.examples);
    lastBehaviorSummary = dataset.summary;
    process.stdout.write(
      `DAgger ${round} behavior · ${formatSummary(dataset.summary)} · ${formatInteger(dataset.examples.length)} new / ${formatInteger(examples.length)} total labels\n`,
    );
    const trained = trainActionScorer(
      examples,
      distilled.weights,
      options.imitationEpochs,
      options.imitationLearningRate,
      options.imitationTemperature,
    );
    const selected = selectTrainingBlend(
      distilled.weights,
      trained.weights,
      trainingSeeds,
      settings,
      options.maxMoves,
    );
    process.stdout.write(
      `DAgger ${round} conservative blend α=${selected.alpha.toFixed(2)} · ${formatSummary(selected.summary)}\n`,
    );
    distilled = { weights: selected.weights, stats: trained.stats };
  }
  if (!lastBehaviorSummary) throw new Error("DAgger produced no training data");
  const studentTraining = evaluatePolicy(
    distilled.weights,
    trainingSeeds,
    settings,
    options.maxMoves,
  );
  const validation = pairedValidation(
    initialFairPolicyWeights(),
    distilled.weights,
    validationSeeds,
    settings,
    options.maxMoves,
  );
  const policyRolloutValidation = options.rolloutValidation
    ? pairedValidation(
        initialFairPolicyWeights(),
        distilled.weights,
        validationSeeds,
        withPolicyRollout(settings, options),
        options.maxMoves,
      )
    : undefined;
  const championVector = weightsToVector(distilled.weights);
  const artifact: FairTunerArtifact = {
    format: FAIR_TUNER_FORMAT,
    version: FAIR_TUNER_VERSION,
    method: "oracle-distillation",
    generation: options.imitationEpochs,
    tunerSeed: options.tunerSeed,
    policySeed: options.policySeed,
    policySamples: options.policySamples,
    trainingGames: options.oracleGames,
    maxMoves: options.maxMoves,
    trainingSeedStart: TRAINING_SEED_START,
    validationSeedStart: VALIDATION_SEED_START,
    parameterNames: parameterNames(),
    distribution: {
      means: championVector,
      standardDeviations: PARAMETER_SPECS.map(
        (spec) => spec.initialStd * MINIMUM_STD_FRACTION,
      ),
    },
    champion: {
      weights: distilled.weights,
      training: omitResults(studentTraining),
    },
    validation,
    ...(policyRolloutValidation === undefined
      ? {}
      : { rolloutValidation: policyRolloutValidation }),
    teacher: {
      games: trainingSeeds.length,
      examples: examples.length,
      depth: options.oracleDepth,
      beamWidth: options.oracleBeam,
      daggerRounds: options.daggerRounds,
      lastBehaviorSummary: omitResults(lastBehaviorSummary),
      imitationLoss: distilled.stats.loss,
      imitationAccuracy: distilled.stats.accuracy,
    },
  };
  await writeArtifact(options.outputPath, artifact);

  process.stdout.write(`student training   · ${formatSummary(studentTraining)}\n`);
  process.stdout.write(`validation baseline · ${formatSummary(validation.baseline)}\n`);
  process.stdout.write(`validation student  · ${formatSummary(validation.candidate)}\n`);
  process.stdout.write(
    `paired delta ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
  );
  if (policyRolloutValidation) {
    process.stdout.write(
      `policy-rollout baseline · ${formatSummary(policyRolloutValidation.baseline)}\n`,
    );
    process.stdout.write(
      `policy-rollout student  · ${formatSummary(policyRolloutValidation.candidate)}\n`,
    );
    process.stdout.write(
      `rollout paired delta ${signedInteger(policyRolloutValidation.pairedMeanScoreDelta)} points · ${signedNumber(policyRolloutValidation.pairedMeanMoveDelta, 1)} moves · W/T/L ${policyRolloutValidation.wins}/${policyRolloutValidation.ties}/${policyRolloutValidation.losses}\n`,
    );
  }
  process.stdout.write(`checkpoint ${resolve(options.outputPath)}\n`);
  return artifact;
}

export async function improveFairPolicy(options: TunerArguments) {
  const trainingSeeds = consecutiveSeeds(
    TRAINING_SEED_START,
    options.trainingGames,
  );
  const validationSeeds = consecutiveSeeds(
    VALIDATION_SEED_START,
    options.validationGames,
  );
  const settings: PolicySettings = {
    samples: options.policySamples,
    policySeed: options.policySeed,
  };
  let studentWeights = initialFairPolicyWeights();
  if (options.resumePath) {
    studentWeights = (await readArtifact(options.resumePath)).champion.weights;
  }

  process.stdout.write(
    `fair Monte Carlo policy iteration · training ${formatSeedRange(trainingSeeds)} · validation ${formatSeedRange(validationSeeds)} · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  process.stdout.write(
    `${options.policyIterationScenarios} common-random scenarios · horizon ${options.policyIterationHorizon} · ${options.criticalStatesPerGame} danger/rise labels per game\n`,
  );

  const examples: OracleTrainingExample[] = [];
  let lastBehaviorSummary: PolicySummary | undefined;
  let trainingStats: ImitationStats = {
    loss: Number.POSITIVE_INFINITY,
    accuracy: 0,
  };

  for (let round = 1; round <= options.policyIterationRounds; round += 1) {
    const dataset = createPolicyImprovementDataset(
      trainingSeeds,
      studentWeights,
      settings,
      options.maxMoves,
      options.policyIterationScenarios,
      options.policyIterationHorizon,
      options.criticalStatesPerGame,
      round,
    );
    examples.push(...dataset.examples);
    lastBehaviorSummary = dataset.summary;
    process.stdout.write(
      `policy iteration ${round} behavior · ${formatSummary(dataset.summary)} · ${formatInteger(dataset.examples.length)} new / ${formatInteger(examples.length)} total Q labels\n`,
    );
    const trained = trainActionScorer(
      examples,
      studentWeights,
      options.imitationEpochs,
      options.imitationLearningRate,
      options.imitationTemperature,
    );
    const selected = selectTrainingBlend(
      studentWeights,
      trained.weights,
      trainingSeeds,
      settings,
      options.maxMoves,
    );
    process.stdout.write(
      `policy iteration ${round} conservative blend α=${selected.alpha.toFixed(2)} · ${formatSummary(selected.summary)}\n`,
    );
    studentWeights = selected.weights;
    trainingStats = trained.stats;
  }
  if (!lastBehaviorSummary) {
    throw new Error("Policy iteration produced no behavior summary");
  }

  const studentTraining = evaluatePolicy(
    studentWeights,
    trainingSeeds,
    settings,
    options.maxMoves,
  );
  const validation = pairedValidation(
    initialFairPolicyWeights(),
    studentWeights,
    validationSeeds,
    settings,
    options.maxMoves,
  );
  const championVector = weightsToVector(studentWeights);
  const artifact: FairTunerArtifact = {
    format: FAIR_TUNER_FORMAT,
    version: FAIR_TUNER_VERSION,
    method: "policy-iteration",
    generation:
      options.policyIterationRounds * options.imitationEpochs,
    tunerSeed: options.tunerSeed,
    policySeed: options.policySeed,
    policySamples: options.policySamples,
    trainingGames: options.trainingGames,
    maxMoves: options.maxMoves,
    trainingSeedStart: TRAINING_SEED_START,
    validationSeedStart: VALIDATION_SEED_START,
    parameterNames: parameterNames(),
    distribution: {
      means: championVector,
      standardDeviations: PARAMETER_SPECS.map(
        (spec) => spec.initialStd * MINIMUM_STD_FRACTION,
      ),
    },
    champion: {
      weights: studentWeights,
      training: omitResults(studentTraining),
    },
    validation,
    policyIteration: {
      rounds: options.policyIterationRounds,
      examples: examples.length,
      scenarios: options.policyIterationScenarios,
      horizon: options.policyIterationHorizon,
      criticalStatesPerGame: options.criticalStatesPerGame,
      lastBehaviorSummary: omitResults(lastBehaviorSummary),
      imitationLoss: trainingStats.loss,
      imitationAccuracy: trainingStats.accuracy,
    },
  };
  await writeArtifact(options.outputPath, artifact);

  process.stdout.write(`student training   · ${formatSummary(studentTraining)}\n`);
  process.stdout.write(`validation baseline · ${formatSummary(validation.baseline)}\n`);
  process.stdout.write(`validation student  · ${formatSummary(validation.candidate)}\n`);
  process.stdout.write(
    `paired delta ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
  );
  process.stdout.write(`checkpoint ${resolve(options.outputPath)}\n`);
  return artifact;
}

function createPolicyImprovementDataset(
  seeds: readonly number[],
  behaviorWeights: FairPolicyWeights,
  settings: PolicySettings,
  maxMoves: number,
  scenarios: number,
  horizon: number,
  criticalStatesPerGame: number,
  round: number,
) {
  const examples: OracleTrainingExample[] = [];
  const results: PolicyGameResult[] = [];

  for (const seed of seeds) {
    let state: GameState = {
      board: createInitialBoard(),
      nextDisc: headlessDisc(seed, 0),
      score: 0,
      level: 1,
      movesRemaining: MOVES_PER_LEVEL,
      movesPlayed: 0,
      gameOver: false,
    };
    let clears = 0;
    let maxChain = 0;
    let labeledStates = 0;

    while (!state.gameOver && state.movesPlayed < maxMoves) {
      const heights = columnHeights(state.board);
      const maximumHeight = Math.max(...heights);
      const critical =
        maximumHeight >= 5 ||
        (state.movesRemaining <= 2 && maximumHeight >= 4);
      if (critical && labeledStates < criticalStatesPerGame) {
        const estimate = estimatePolicyImprovementAction(
          state,
          behaviorWeights,
          settings,
          scenarios,
          horizon,
        );
        const observable = canonicalObservableState(state);
        const actions = columnOrder(observable.mirrored)
          .filter((column) => state.board[column] === EMPTY)
          .map((column) =>
            extractActionFeatures(
              state,
              column,
              settings.samples,
              settings.policySeed,
              observable.hash,
            ),
          );
        examples.push({
          actions,
          teacherColumn: estimate.column,
          importance:
            (1 + 2 * (maximumHeight / BOARD_SIZE) ** 2) *
            Math.max(0.35, Math.min(3, estimate.gap / 10_000)),
        });
        labeledStates += 1;
      }

      const column = chooseFairMove(state, behaviorWeights, settings);
      if (column === null) {
        throw new Error("Policy iteration behavior found no live move");
      }
      const step = playActualGameMove(state, column, seed);
      if (!step) throw new Error("Policy iteration behavior chose illegally");
      clears += clearCount(step.move);
      maxChain = Math.max(maxChain, step.move.waves.length);
      state = step.state;
    }

    const result: PolicyGameResult = {
      seed,
      score: state.score,
      moves: state.movesPlayed,
      finalLevel: state.level,
      gameOver: state.gameOver,
      censored: !state.gameOver,
      clears,
      maxChain,
    };
    results.push(result);
    process.stdout.write(
      `PI ${round} seed ${formatSeed(seed)} · ${formatInteger(result.score)} · ${result.moves} moves · ${labeledStates} Q labels\n`,
    );
  }
  return { examples, summary: summarizeResults(results, maxMoves) };
}

function estimatePolicyImprovementAction(
  state: GameState,
  behaviorWeights: FairPolicyWeights,
  settings: PolicySettings,
  scenarios: number,
  horizon: number,
) {
  const observable = canonicalObservableState(state);
  const columns: Array<{ column: number; value: number }> = [];

  for (const rootColumn of columnOrder(observable.mirrored)) {
    if (state.board[rootColumn] !== EMPTY) continue;
    let value = 0;
    for (let scenario = 0; scenario < scenarios; scenario += 1) {
      let position = state;
      let column: number = rootColumn;
      let reward = 0;
      let terminal = false;
      for (let ply = 0; ply < horizon; ply += 1) {
        const reveal = rolloutSample(
          observable.hash,
          settings.policySeed ^ 0x5049_5445,
          scenario,
          scenarios,
          ply,
          ROLLOUT_REVEAL_DOMAIN,
        );
        const move = playMove(position, column, () => reveal, {
          captureAnimation: false,
        });
        if (!move) {
          reward += TERMINAL_UTILITY;
          terminal = true;
          break;
        }
        reward += move.scoreDelta;
        if (move.state.gameOver) {
          reward += TERMINAL_UTILITY;
          terminal = true;
          break;
        }
        position = {
          ...move.state,
          score: 0,
          nextDisc: rolloutDisc(
            observable.hash,
            settings.policySeed ^ 0x5049_5445,
            scenario,
            scenarios,
            ply + 1,
          ),
        };
        if (ply + 1 < horizon) {
          const continuation = chooseFairMove(
            position,
            behaviorWeights,
            settings,
          );
          if (continuation === null) {
            reward += TERMINAL_UTILITY;
            terminal = true;
            break;
          }
          column = continuation;
        }
      }
      value +=
        reward +
        (terminal
          ? 0
          : 500_000 + evaluateFairPosition(position, behaviorWeights));
    }
    columns.push({ column: rootColumn, value: value / scenarios });
  }

  columns.sort((first, second) => second.value - first.value);
  if (columns.length === 0) {
    throw new Error("Policy improvement found no legal root action");
  }
  return {
    column: columns[0].column,
    gap: columns[0].value - (columns[1]?.value ?? columns[0].value),
  };
}

function createOracleDataset(
  seeds: readonly number[],
  settings: PolicySettings,
  maxMoves: number,
  depth: number,
  beamWidth: number,
  behaviorWeights: FairPolicyWeights,
  label: string,
) {
  const examples: OracleTrainingExample[] = [];
  const results: PolicyGameResult[] = [];

  for (const seed of seeds) {
    let state: GameState = {
      board: createInitialBoard(),
      nextDisc: headlessDisc(seed, 0),
      score: 0,
      level: 1,
      movesRemaining: MOVES_PER_LEVEL,
      movesPlayed: 0,
      gameOver: false,
    };
    let clears = 0;
    let maxChain = 0;

    while (!state.gameOver && state.movesPlayed < maxMoves) {
      // This is the sole intentionally privileged call: the teacher sees the
      // training game's future stream. None of that seed enters action features.
      const teacher = planOracleMove(state, seed, depth, beamWidth);
      if (teacher.column === null) {
        throw new Error("Oracle teacher returned no move for a live state");
      }
      const observable = canonicalObservableState(state);
      const actions = columnOrder(observable.mirrored)
        .filter((column) => state.board[column] === EMPTY)
        .map((column) =>
          extractActionFeatures(
            state,
            column,
            settings.samples,
            settings.policySeed,
            observable.hash,
          ),
        );
      if (!actions.some((action) => action.column === teacher.column)) {
        throw new Error("Oracle teacher label was not a legal student action");
      }
      const maximumHeight = Math.max(...columnHeights(state.board));
      examples.push({
        actions,
        teacherColumn: teacher.column,
        importance:
          1 +
          2 * (maximumHeight / BOARD_SIZE) ** 2 +
          (state.movesRemaining === 1 ? 0.5 : 0),
      });

      // Execute only the seed-blind student's move. The oracle action remains
      // an offline label and can never steer the collected game trajectory.
      const behaviorColumn = chooseFairMove(state, behaviorWeights, settings);
      if (behaviorColumn === null) {
        throw new Error("Fair DAgger behavior found no move for a live state");
      }
      const step = playActualGameMove(state, behaviorColumn, seed);
      if (!step) throw new Error("Fair DAgger behavior chose an illegal move");
      clears += clearCount(step.move);
      maxChain = Math.max(maxChain, step.move.waves.length);
      state = step.state;
    }

    const result: PolicyGameResult = {
      seed,
      score: state.score,
      moves: state.movesPlayed,
      finalLevel: state.level,
      gameOver: state.gameOver,
      censored: !state.gameOver,
      clears,
      maxChain,
    };
    results.push(result);
    process.stdout.write(
      `${label} seed ${formatSeed(seed)} · ${formatInteger(result.score)} · ${result.moves} moves · ${result.clears} clears · chain ${result.maxChain}${result.censored ? " · capped" : ""}\n`,
    );
  }

  return { examples, summary: summarizeResults(results, maxMoves) };
}

function trainActionScorer(
  examples: readonly OracleTrainingExample[],
  startingWeights: FairPolicyWeights,
  epochs: number,
  learningRate: number,
  temperature: number,
) {
  const weights = weightsToVector(startingWeights);
  const normalized = weights.map(
    (weight, index) =>
      (weight - PARAMETER_SPECS[index].initialMean) /
      PARAMETER_SPECS[index].initialStd,
  );
  const firstMoment = Array<number>(weights.length).fill(0);
  const secondMoment = Array<number>(weights.length).fill(0);
  let betaOnePower = 1;
  let betaTwoPower = 1;
  let bestWeights = [...weights];
  let bestStats = imitationStats(examples, weights, temperature);
  const reportEvery = Math.max(1, Math.floor(epochs / 10));

  for (let epoch = 1; epoch <= epochs; epoch += 1) {
    const gradient = imitationGradient(
      examples,
      weights,
      normalized,
      temperature,
    );
    betaOnePower *= 0.9;
    betaTwoPower *= 0.999;
    for (let index = 0; index < weights.length; index += 1) {
      firstMoment[index] = firstMoment[index] * 0.9 + gradient[index] * 0.1;
      secondMoment[index] =
        secondMoment[index] * 0.999 + gradient[index] ** 2 * 0.001;
      const correctedFirst = firstMoment[index] / (1 - betaOnePower);
      const correctedSecond = secondMoment[index] / (1 - betaTwoPower);
      normalized[index] -=
        (learningRate * correctedFirst) /
        (Math.sqrt(correctedSecond) + 1e-8);
      weights[index] = clipParameter(
        index,
        PARAMETER_SPECS[index].initialMean +
          normalized[index] * PARAMETER_SPECS[index].initialStd,
      );
      normalized[index] =
        (weights[index] - PARAMETER_SPECS[index].initialMean) /
        PARAMETER_SPECS[index].initialStd;
    }

    const stats = imitationStats(examples, weights, temperature);
    if (
      stats.loss < bestStats.loss ||
      (stats.loss === bestStats.loss && stats.accuracy > bestStats.accuracy)
    ) {
      bestStats = stats;
      bestWeights = [...weights];
    }
    if (epoch === 1 || epoch % reportEvery === 0 || epoch === epochs) {
      process.stdout.write(
        `imitation epoch ${epoch.toString().padStart(3)} · loss ${stats.loss.toFixed(5)} · accuracy ${(stats.accuracy * 100).toFixed(2)}%\n`,
      );
    }
  }

  return { weights: vectorToWeights(bestWeights), stats: bestStats };
}

function imitationGradient(
  examples: readonly OracleTrainingExample[],
  weights: readonly number[],
  normalized: readonly number[],
  temperature: number,
) {
  const gradient = Array<number>(weights.length).fill(0);
  let totalImportance = 0;

  for (const example of examples) {
    const probabilities = actionProbabilities(example.actions, weights, temperature);
    const teacher = example.actions.find(
      (action) => action.column === example.teacherColumn,
    )!;
    for (let index = 0; index < gradient.length; index += 1) {
      let expectedFeature = 0;
      for (let actionIndex = 0; actionIndex < example.actions.length; actionIndex += 1) {
        expectedFeature +=
          probabilities[actionIndex] * example.actions[actionIndex].values[index];
      }
      gradient[index] +=
        example.importance *
        ((expectedFeature - teacher.values[index]) *
          PARAMETER_SPECS[index].initialStd /
          temperature);
    }
    totalImportance += example.importance;
  }

  for (let index = 0; index < gradient.length; index += 1) {
    gradient[index] =
      gradient[index] / totalImportance + normalized[index] * 0.001;
  }
  return gradient;
}

function imitationStats(
  examples: readonly OracleTrainingExample[],
  weights: readonly number[],
  temperature: number,
): ImitationStats {
  let weightedLoss = 0;
  let totalImportance = 0;
  let correct = 0;

  for (const example of examples) {
    const probabilities = actionProbabilities(example.actions, weights, temperature);
    const teacherIndex = example.actions.findIndex(
      (action) => action.column === example.teacherColumn,
    );
    weightedLoss -=
      example.importance * Math.log(Math.max(Number.EPSILON, probabilities[teacherIndex]));
    totalImportance += example.importance;
    let bestIndex = 0;
    for (let index = 1; index < example.actions.length; index += 1) {
      if (probabilities[index] > probabilities[bestIndex]) bestIndex = index;
    }
    if (example.actions[bestIndex].column === example.teacherColumn) correct += 1;
  }
  return {
    loss: weightedLoss / totalImportance,
    accuracy: correct / examples.length,
  };
}

function actionProbabilities(
  actions: readonly ActionFeatures[],
  weights: readonly number[],
  temperature: number,
) {
  const logits = actions.map((action) => scoreAction(action, weights) / temperature);
  const maximum = Math.max(...logits);
  const exponentials = logits.map((logit) => Math.exp(logit - maximum));
  const total = exponentials.reduce((sum, value) => sum + value, 0);
  return exponentials.map((value) => value / total);
}

function pairedValidation(
  baselineWeights: FairPolicyWeights,
  candidateWeights: FairPolicyWeights,
  seeds: readonly number[],
  settings: PolicySettings,
  maxMoves: number,
): PairedValidation {
  const baseline = evaluatePolicy(
    baselineWeights,
    seeds,
    settings,
    maxMoves,
  );
  const candidate = evaluatePolicy(
    candidateWeights,
    seeds,
    settings,
    maxMoves,
  );
  const scoreDeltas = candidate.results.map(
    (result, index) => result.score - baseline.results[index].score,
  );
  const moveDeltas = candidate.results.map(
    (result, index) => result.moves - baseline.results[index].moves,
  );
  return {
    baseline: omitResults(baseline),
    candidate: omitResults(candidate),
    pairedMeanScoreDelta: mean(scoreDeltas),
    pairedMedianScoreDelta: percentile([...scoreDeltas].sort(numberOrder), 0.5),
    pairedMeanMoveDelta: mean(moveDeltas),
    wins: scoreDeltas.filter((delta) => delta > 0).length,
    ties: scoreDeltas.filter((delta) => delta === 0).length,
    losses: scoreDeltas.filter((delta) => delta < 0).length,
  };
}

function selectTrainingBlend(
  current: FairPolicyWeights,
  proposed: FairPolicyWeights,
  seeds: readonly number[],
  settings: PolicySettings,
  maxMoves: number,
) {
  const alphas = [0, 0.1, 0.2, 0.35, 0.5, 0.7, 1] as const;
  let best: {
    alpha: number;
    weights: FairPolicyWeights;
    summary: PolicySummary;
  } | null = null;
  for (const alpha of alphas) {
    const weights = blendWeights(current, proposed, alpha);
    const summary = evaluatePolicy(weights, seeds, settings, maxMoves);
    if (
      !best ||
      summary.meanObjective > best.summary.meanObjective ||
      (summary.meanObjective === best.summary.meanObjective &&
        summary.meanScore > best.summary.meanScore)
    ) {
      best = { alpha, weights, summary };
    }
  }
  return best!;
}

function blendWeights(
  current: FairPolicyWeights,
  proposed: FairPolicyWeights,
  alpha: number,
) {
  return vectorToWeights(
    PARAMETER_SPECS.map(
      (spec) =>
        current[spec.name] * (1 - alpha) +
        proposed[spec.name] * alpha,
    ),
  );
}

function withPolicyRollout(
  settings: PolicySettings,
  options: Pick<
    TunerArguments,
    "rollouts" | "rolloutHorizon" | "continuationSamples"
  >,
): PolicySettings {
  return {
    samples: options.continuationSamples,
    policySeed: settings.policySeed,
    rollout: {
      count: options.rollouts,
      horizon: options.rolloutHorizon,
      continuationSamples: options.continuationSamples,
    },
  };
}

function createPopulation(
  distribution: Distribution,
  champion: readonly number[],
  size: number,
  tunerSeed: number,
  generation: number,
) {
  const vectors: number[][] = [clipVector(champion)];
  const meanVector = clipVector(distribution.means);
  if (vectorKey(meanVector) !== vectorKey(vectors[0])) vectors.push(meanVector);

  for (let candidate = vectors.length; candidate < size; candidate += 1) {
    const random = seededRandom(
      mix32(
        tunerSeed ^
          Math.imul(generation + 1, POLICY_SAMPLE_MULTIPLIER) ^
          Math.imul(candidate + 1, ACTUAL_MOVE_MULTIPLIER) ^
          CANDIDATE_DOMAIN,
      ),
    );
    vectors.push(
      PARAMETER_SPECS.map((spec, index) =>
        clipParameter(
          index,
          distribution.means[index] +
            gaussian(random) * distribution.standardDeviations[index],
        ),
      ),
    );
  }
  return vectors;
}

function updateDistribution(
  previous: Distribution,
  elites: readonly CandidateEvaluation[],
): Distribution {
  const eliteMeans = PARAMETER_SPECS.map((_, index) =>
    mean(elites.map((elite) => elite.vector[index])),
  );
  const eliteStd = PARAMETER_SPECS.map((spec, index) => {
    const average = eliteMeans[index];
    const variance = mean(
      elites.map((elite) => (elite.vector[index] - average) ** 2),
    );
    return Math.max(
      spec.initialStd * MINIMUM_STD_FRACTION,
      Math.sqrt(variance),
    );
  });
  return {
    means: PARAMETER_SPECS.map((_, index) =>
      clipParameter(
        index,
        previous.means[index] * (1 - CEM_UPDATE_RATE) +
          eliteMeans[index] * CEM_UPDATE_RATE,
      ),
    ),
    standardDeviations: PARAMETER_SPECS.map(
      (spec, index) =>
        Math.max(
          spec.initialStd * MINIMUM_STD_FRACTION,
          previous.standardDeviations[index] * (1 - CEM_UPDATE_RATE) +
            eliteStd[index] * CEM_UPDATE_RATE,
        ),
    ),
  };
}

function compareCandidates(
  first: CandidateEvaluation,
  second: CandidateEvaluation,
) {
  return (
    second.summary.meanObjective - first.summary.meanObjective ||
    second.summary.meanScore - first.summary.meanScore ||
    second.summary.meanMoves - first.summary.meanMoves ||
    vectorKey(first.vector).localeCompare(vectorKey(second.vector))
  );
}

function initialDistribution(): Distribution {
  return {
    means: PARAMETER_SPECS.map((spec) => spec.initialMean),
    standardDeviations: PARAMETER_SPECS.map((spec) => spec.initialStd),
  };
}

function vectorToWeights(vector: readonly number[]): FairPolicyWeights {
  if (vector.length !== PARAMETER_SPECS.length) {
    throw new Error(`Expected ${PARAMETER_SPECS.length} policy weights`);
  }
  return Object.fromEntries(
    PARAMETER_SPECS.map((spec, index) => [
      spec.name,
      clipParameter(index, vector[index]),
    ]),
  ) as FairPolicyWeights;
}

function weightsToVector(weights: FairPolicyWeights) {
  return PARAMETER_SPECS.map((spec) => {
    const value = weights[spec.name];
    if (!Number.isFinite(value)) {
      throw new Error(`Policy weight ${spec.name} must be finite`);
    }
    return value;
  });
}

function clipVector(vector: readonly number[]) {
  return vector.map((value, index) => clipParameter(index, value));
}

function clipParameter(index: number, value: number) {
  const spec = PARAMETER_SPECS[index];
  return Math.max(spec.minimum, Math.min(spec.maximum, value));
}

function vectorKey(vector: readonly number[]) {
  return vector.map((value) => value.toPrecision(15)).join(",");
}

function parameterNames() {
  return PARAMETER_SPECS.map((spec) => spec.name);
}

function copyDistribution(distribution: Distribution): Distribution {
  return {
    means: [...distribution.means],
    standardDeviations: [...distribution.standardDeviations],
  };
}

function canonicalObservableState(state: GameState) {
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

function policySample(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
  domain: number,
) {
  const offset = mix32(
    observableHash ^
      policySeed ^
      domain,
  ) % BOARD_SIZE;
  const stratum = Math.floor(((sample + 0.5) * BOARD_SIZE) / samples);
  const disc = ((offset + stratum) % BOARD_SIZE) + 1;
  return (disc - 0.5) / BOARD_SIZE;
}

function policyDisc(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
) {
  return (
    Math.floor(
      policySample(
        observableHash,
        policySeed,
        sample,
        samples,
        POLICY_DISC_DOMAIN,
      ) * BOARD_SIZE,
    ) + 1
  ) as 1 | 2 | 3 | 4 | 5 | 6 | 7;
}

function rolloutSample(
  observableHash: number,
  policySeed: number,
  scenario: number,
  scenarios: number,
  ply: number,
  domain: number,
) {
  const offset =
    mix32(
      observableHash ^
        policySeed ^
        Math.imul(ply + 1, ROLLOUT_PLY_MULTIPLIER) ^
        domain,
    ) % BOARD_SIZE;
  const stratum = Math.floor(((scenario + 0.5) * BOARD_SIZE) / scenarios);
  const disc = ((offset + stratum) % BOARD_SIZE) + 1;
  return (disc - 0.5) / BOARD_SIZE;
}

function rolloutDisc(
  observableHash: number,
  policySeed: number,
  scenario: number,
  scenarios: number,
  ply: number,
) {
  return (
    Math.floor(
      rolloutSample(
        observableHash,
        policySeed,
        scenario,
        scenarios,
        ply,
        ROLLOUT_DISC_DOMAIN,
      ) * BOARD_SIZE,
    ) + 1
  ) as 1 | 2 | 3 | 4 | 5 | 6 | 7;
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

function columnOrder(mirrored: boolean) {
  return mirrored ? MIRRORED_COLUMN_ORDER : COLUMN_ORDER;
}

function addVector(target: number[], source: readonly number[], scale: number) {
  for (let index = 0; index < target.length; index += 1) {
    target[index] += source[index] * scale;
  }
}

function dot(first: readonly number[], second: readonly number[]) {
  let value = 0;
  for (let index = 0; index < first.length; index += 1) {
    value += first[index] * second[index];
  }
  return value;
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

function clearCount(move: MoveResult) {
  const wavePoints = move.waves.reduce((sum, wave) => sum + wave.points, 0);
  const clearPoints =
    move.scoreDelta - wavePoints - (move.levelAdvanced ? LEVEL_BONUS : 0);
  const clears = clearPoints / CLEAR_BONUS;
  if (!Number.isInteger(clears) || clears < 0) {
    throw new Error("Move score could not be decomposed into clear bonuses");
  }
  return clears;
}

function gaussian(random: () => number) {
  const first = Math.max(Number.EPSILON, random());
  const second = random();
  return Math.sqrt(-2 * Math.log(first)) * Math.cos(2 * Math.PI * second);
}

function consecutiveSeeds(start: number, count: number) {
  boundedPositiveInteger(count, "game count", MAX_GAMES);
  if (start + count > RESERVED_FINAL_SEED_START) {
    throw new Error("Training or validation seeds overlap the reserved final range");
  }
  return Array.from({ length: count }, (_, index) => (start + index) >>> 0);
}

function omitResults(summary: PolicySummary): Omit<PolicySummary, "results"> {
  return {
    games: summary.games,
    meanObjective: summary.meanObjective,
    meanScore: summary.meanScore,
    medianScore: summary.medianScore,
    minimumScore: summary.minimumScore,
    maximumScore: summary.maximumScore,
    meanMoves: summary.meanMoves,
    censoredGames: summary.censoredGames,
    meanClears: summary.meanClears,
    meanMaxChain: summary.meanMaxChain,
  };
}

async function writeArtifact(path: string, artifact: FairTunerArtifact) {
  const absolutePath = resolve(path);
  await mkdir(dirname(absolutePath), { recursive: true });
  const temporaryPath = `${absolutePath}.tmp`;
  await writeFile(temporaryPath, `${JSON.stringify(artifact, null, 2)}\n`);
  await rename(temporaryPath, absolutePath);
}

async function readArtifact(path: string): Promise<FairTunerArtifact> {
  const parsed = JSON.parse(await readFile(resolve(path), "utf8")) as unknown;
  validateArtifact(parsed);
  return parsed;
}

function validateArtifact(value: unknown): asserts value is FairTunerArtifact {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new Error("Fair tuner checkpoint must be an object");
  }
  const artifact = value as Partial<FairTunerArtifact>;
  if (
    artifact.format !== FAIR_TUNER_FORMAT ||
    artifact.version !== FAIR_TUNER_VERSION
  ) {
    throw new Error("Unsupported fair tuner checkpoint format");
  }
  if (!artifact.champion?.weights || !artifact.distribution) {
    throw new Error("Fair tuner checkpoint is incomplete");
  }
  weightsToVector(artifact.champion.weights);
  if (
    artifact.distribution.means.length !== PARAMETER_SPECS.length ||
    artifact.distribution.standardDeviations.length !== PARAMETER_SPECS.length ||
    [...artifact.distribution.means, ...artifact.distribution.standardDeviations].some(
      (number) => !Number.isFinite(number),
    )
  ) {
    throw new Error("Fair tuner checkpoint has invalid distribution arrays");
  }
}

function assertCompatibleArtifact(
  artifact: FairTunerArtifact,
  options: TunerArguments,
) {
  if (
    artifact.tunerSeed !== options.tunerSeed ||
    artifact.policySeed !== options.policySeed ||
    artifact.policySamples !== options.policySamples ||
    artifact.trainingGames !== options.trainingGames ||
    artifact.maxMoves !== options.maxMoves ||
    artifact.trainingSeedStart !== TRAINING_SEED_START ||
    artifact.validationSeedStart !== VALIDATION_SEED_START ||
    artifact.parameterNames.join(",") !== parameterNames().join(",")
  ) {
    throw new Error("Checkpoint is incompatible with this tuner configuration");
  }
}

export function parseArguments(
  arguments_: readonly string[],
): TunerArguments | null {
  let generations = DEFAULT_GENERATIONS;
  let population = DEFAULT_POPULATION;
  let elites = DEFAULT_ELITES;
  let trainingGames = DEFAULT_TRAINING_GAMES;
  let validationGames = DEFAULT_VALIDATION_GAMES;
  let policySamples = DEFAULT_POLICY_SAMPLES;
  let maxMoves = DEFAULT_MAX_MOVES;
  let tunerSeed = DEFAULT_TUNER_SEED;
  let policySeed = DEFAULT_POLICY_SEED;
  let outputPath = DEFAULT_OUTPUT;
  let resumePath: string | undefined;
  let evaluatePath: string | undefined;
  let oracleGames = 0;
  let oracleDepth = DEFAULT_ORACLE_DEPTH;
  let oracleBeam = DEFAULT_ORACLE_BEAM;
  let imitationEpochs = DEFAULT_IMITATION_EPOCHS;
  let imitationLearningRate = DEFAULT_IMITATION_LEARNING_RATE;
  let imitationTemperature = DEFAULT_IMITATION_TEMPERATURE;
  let daggerRounds = DEFAULT_DAGGER_ROUNDS;
  let rolloutValidation = false;
  let rollouts = DEFAULT_ROLLOUTS;
  let rolloutHorizon = DEFAULT_ROLLOUT_HORIZON;
  let continuationSamples = DEFAULT_CONTINUATION_SAMPLES;
  let policyIterationRounds = 0;
  let policyIterationScenarios = DEFAULT_POLICY_ITERATION_SCENARIOS;
  let policyIterationHorizon = DEFAULT_POLICY_ITERATION_HORIZON;
  let criticalStatesPerGame = DEFAULT_CRITICAL_STATES_PER_GAME;
  let selfTest = false;

  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--self-test") {
      selfTest = true;
      continue;
    }
    if (flag === "--rollout-validation") {
      rolloutValidation = true;
      continue;
    }
    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`Missing value after ${flag}`);
    index += 1;

    switch (flag) {
      case "--generations":
        generations = parseBoundedPositiveInteger(value, flag, MAX_GENERATIONS);
        break;
      case "--population":
        population = parseBoundedPositiveInteger(value, flag, MAX_POPULATION);
        break;
      case "--elites":
        elites = parseBoundedPositiveInteger(value, flag, MAX_POPULATION);
        break;
      case "--games":
      case "--training-games":
        trainingGames = parseBoundedPositiveInteger(value, flag, MAX_GAMES);
        break;
      case "--validation-games":
        validationGames = parseBoundedPositiveInteger(value, flag, MAX_GAMES);
        break;
      case "--samples":
        policySamples = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_POLICY_SAMPLES,
        );
        break;
      case "--max-moves":
        maxMoves = parseBoundedPositiveInteger(value, flag, MAX_MOVES);
        break;
      case "--tuner-seed":
        tunerSeed = parseSeed(value, flag);
        break;
      case "--policy-seed":
        policySeed = parseSeed(value, flag);
        break;
      case "--output":
        outputPath = value;
        break;
      case "--resume":
        resumePath = value;
        break;
      case "--evaluate":
        evaluatePath = value;
        break;
      case "--oracle-games":
        oracleGames = parseBoundedPositiveInteger(value, flag, MAX_GAMES);
        break;
      case "--oracle-depth":
        oracleDepth = parseBoundedPositiveInteger(value, flag, 100);
        break;
      case "--oracle-beam":
        oracleBeam = parseBoundedPositiveInteger(value, flag, 100_000);
        break;
      case "--imitation-epochs":
        imitationEpochs = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_GENERATIONS,
        );
        break;
      case "--imitation-lr":
        imitationLearningRate = parsePositiveFinite(value, flag);
        break;
      case "--imitation-temperature":
        imitationTemperature = parsePositiveFinite(value, flag);
        break;
      case "--dagger-rounds":
        daggerRounds = parseBoundedPositiveInteger(value, flag, 100);
        break;
      case "--rollouts":
        rollouts = parseBoundedPositiveInteger(value, flag, 100_000);
        break;
      case "--rollout-horizon":
        rolloutHorizon = parseBoundedPositiveInteger(value, flag, 100);
        break;
      case "--continuation-samples":
        continuationSamples = parseBoundedPositiveInteger(
          value,
          flag,
          MAX_POLICY_SAMPLES,
        );
        break;
      case "--policy-iteration-rounds":
        policyIterationRounds = parseBoundedPositiveInteger(value, flag, 100);
        break;
      case "--policy-iteration-scenarios":
        policyIterationScenarios = parseBoundedPositiveInteger(
          value,
          flag,
          10_000,
        );
        break;
      case "--policy-iteration-horizon":
        policyIterationHorizon = parseBoundedPositiveInteger(value, flag, 500);
        break;
      case "--critical-states-per-game":
        criticalStatesPerGame = parseBoundedPositiveInteger(value, flag, 500);
        break;
      default:
        throw new Error(`Unknown option ${flag}`);
    }
  }

  if (elites >= population) {
    throw new Error("--elites must be smaller than --population");
  }
  return {
    generations,
    population,
    elites,
    trainingGames,
    validationGames,
    policySamples,
    maxMoves,
    tunerSeed,
    policySeed,
    outputPath,
    ...(resumePath === undefined ? {} : { resumePath }),
    ...(evaluatePath === undefined ? {} : { evaluatePath }),
    oracleGames,
    oracleDepth,
    oracleBeam,
    imitationEpochs,
    imitationLearningRate,
    imitationTemperature,
    daggerRounds,
    rolloutValidation,
    rollouts,
    rolloutHorizon,
    continuationSamples,
    policyIterationRounds,
    policyIterationScenarios,
    policyIterationHorizon,
    criticalStatesPerGame,
    selfTest,
  };
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options === null) {
    process.stdout.write(helpText());
    return;
  }
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  if (options.evaluatePath) {
    const artifact = await readArtifact(options.evaluatePath);
    const settings: PolicySettings = {
      samples: options.policySamples,
      policySeed: artifact.policySeed,
    };
    const validation = pairedValidation(
      initialFairPolicyWeights(),
      artifact.champion.weights,
      consecutiveSeeds(VALIDATION_SEED_START, options.validationGames),
      settings,
      options.maxMoves,
    );
    process.stdout.write(`validation baseline · ${formatSummary(validation.baseline)}\n`);
    process.stdout.write(`validation candidate · ${formatSummary(validation.candidate)}\n`);
    process.stdout.write(
      `paired delta ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
    );
    if (options.rolloutValidation) {
      const rolloutValidation = pairedValidation(
        initialFairPolicyWeights(),
        artifact.champion.weights,
        consecutiveSeeds(VALIDATION_SEED_START, options.validationGames),
        withPolicyRollout(settings, options),
        options.maxMoves,
      );
      process.stdout.write(
        `policy-rollout baseline · ${formatSummary(rolloutValidation.baseline)}\n`,
      );
      process.stdout.write(
        `policy-rollout candidate · ${formatSummary(rolloutValidation.candidate)}\n`,
      );
      process.stdout.write(
        `rollout paired delta ${signedInteger(rolloutValidation.pairedMeanScoreDelta)} points · ${signedNumber(rolloutValidation.pairedMeanMoveDelta, 1)} moves · W/T/L ${rolloutValidation.wins}/${rolloutValidation.ties}/${rolloutValidation.losses}\n`,
      );
    }
    return;
  }
  if (options.oracleGames > 0) {
    await distillOraclePolicy(options);
    return;
  }
  if (options.policyIterationRounds > 0) {
    await improveFairPolicy(options);
    return;
  }
  await tuneFairPolicy(options);
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await runCli(process.argv.slice(2));
}

function runSelfTest() {
  const weights = initialFairPolicyWeights();
  const settings = { samples: 2, policySeed: DEFAULT_POLICY_SEED };
  const first = runFairPolicyGame(TRAINING_SEED_START, weights, settings, 10);
  const second = runFairPolicyGame(TRAINING_SEED_START, weights, settings, 10);
  if (JSON.stringify(first) !== JSON.stringify(second)) {
    throw new Error("Fair policy is not deterministic");
  }
  // Changing only an unobservable environment seed changes the environment,
  // not the move selected from an otherwise identical observable position.
  const initial: GameState = {
    board: createInitialBoard(),
    nextDisc: 4,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  const move = chooseFairMove(initial, weights, settings);
  if (move === null || move !== chooseFairMove(initial, weights, settings)) {
    throw new Error("Fair planner self-check failed");
  }
  const asymmetricStep = playMove(initial, 0, () => 0.5, {
    captureAnimation: false,
  });
  if (!asymmetricStep) throw new Error("Could not create mirror fixture");
  const asymmetric = { ...asymmetricStep.state, nextDisc: 5 as const };
  const reflected = { ...asymmetric, board: mirrorBoard(asymmetric.board) };
  const asymmetricMove = chooseFairMove(asymmetric, weights, settings);
  const reflectedMove = chooseFairMove(reflected, weights, settings);
  if (
    asymmetricMove === null ||
    reflectedMove === null ||
    reflectedMove !== BOARD_SIZE - 1 - asymmetricMove
  ) {
    throw new Error("Fair planner did not preserve horizontal reflection");
  }
  process.stdout.write(
    `self-test ok · deterministic fair planner · ${formatInteger(first.score)} points after ${first.moves} moves\n`,
  );
}

function helpText() {
  return `Drop7 fair-policy CEM tuner

Usage:
  node --experimental-strip-types approaches/fair-expectimax/fair-policy/tune.ts [options]

Options:
  --generations <n>       Total CEM generations (default: ${DEFAULT_GENERATIONS})
  --population <n>        Candidates per generation (default: ${DEFAULT_POPULATION})
  --elites <n>            Candidates updating the distribution (default: ${DEFAULT_ELITES})
  --games <n>             Fixed training games (default: ${DEFAULT_TRAINING_GAMES})
  --validation-games <n>  Separate paired validation games (default: ${DEFAULT_VALIDATION_GAMES})
  --samples <n>           Fair one-ply chance samples (default: ${DEFAULT_POLICY_SAMPLES})
  --max-moves <n>         Game/censoring cap (default: ${DEFAULT_MAX_MOVES})
  --tuner-seed <uint32>   CEM sampling seed
  --policy-seed <uint32>  Solver-local chance seed
  --output <path>         Atomic checkpoint/export path (default: ${DEFAULT_OUTPUT})
  --resume <path>         Resume a compatible checkpoint
  --evaluate <path>       Validate an exported champion without tuning
  --oracle-games <n>      Distill an unfair oracle on training seeds only
  --oracle-depth <ply>    Oracle teacher lookahead (default: ${DEFAULT_ORACLE_DEPTH})
  --oracle-beam <states>  Oracle teacher beam (default: ${DEFAULT_ORACLE_BEAM})
  --imitation-epochs <n>  Full-batch distillation epochs (default: ${DEFAULT_IMITATION_EPOCHS})
  --imitation-lr <n>      Normalized Adam learning rate (default: ${DEFAULT_IMITATION_LEARNING_RATE})
  --imitation-temperature <n>  Teacher softmax scale (default: ${DEFAULT_IMITATION_TEMPERATURE})
  --dagger-rounds <n>     Student-distribution labeling rounds (default: ${DEFAULT_DAGGER_ROUNDS})
  --rollout-validation    Validate with fair student-policy continuations
  --rollouts <n>          Root rollout scenarios (default: ${DEFAULT_ROLLOUTS})
  --rollout-horizon <n>   Simulated moves per root (default: ${DEFAULT_ROLLOUT_HORIZON})
  --continuation-samples <n>  One-ply student samples inside rollouts (default: ${DEFAULT_CONTINUATION_SAMPLES})
  --policy-iteration-rounds <n>  Fair Monte Carlo policy-improvement rounds
  --policy-iteration-scenarios <n>  Common-random Q samples (default: ${DEFAULT_POLICY_ITERATION_SCENARIOS})
  --policy-iteration-horizon <n>  Q rollout horizon (default: ${DEFAULT_POLICY_ITERATION_HORIZON})
  --critical-states-per-game <n>  Danger/rise Q labels (default: ${DEFAULT_CRITICAL_STATES_PER_GAME})
  --self-test             Run a small deterministic fairness check
  --help, -h              Show this help

Training uses ${formatSeed(TRAINING_SEED_START)}+ and validation uses
${formatSeed(VALIDATION_SEED_START)}+. ${formatSeed(RESERVED_FINAL_SEED_START)}+
is deliberately reserved for a later untouched final test.
`;
}

function formatSummary(summary: Omit<PolicySummary, "results">) {
  return [
    `mean ${formatInteger(summary.meanScore)}`,
    `median ${formatInteger(summary.medianScore)}`,
    `moves ${summary.meanMoves.toFixed(1)}`,
    `capped ${summary.censoredGames}/${summary.games}`,
    `clears ${summary.meanClears.toFixed(2)}`,
    `chain ${summary.meanMaxChain.toFixed(2)}`,
  ].join(" · ");
}

function parseBoundedPositiveInteger(value: string, flag: string, maximum: number) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > maximum) {
    throw new Error(`${flag} must be an integer between 1 and ${maximum}`);
  }
  return parsed;
}

function parsePositiveFinite(value: string, flag: string) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) {
    throw new Error(`${flag} must be a positive finite number`);
  }
  return parsed;
}

function boundedPositiveInteger(value: number, label: string, maximum: number) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new Error(`${label} must be an integer between 1 and ${maximum}`);
  }
  return value;
}

function parseSeed(value: string, flag: string) {
  const parsed = Number(value);
  return unsignedSeed(parsed, flag);
}

function unsignedSeed(value: number, label: string) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error(`${label} must be a uint32 integer`);
  }
  return value >>> 0;
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
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function percentile(sorted: readonly number[], fraction: number) {
  const position = (sorted.length - 1) * fraction;
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  const mix = position - lower;
  return sorted[lower] * (1 - mix) + sorted[upper] * mix;
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function signedInteger(value: number) {
  return `${value >= 0 ? "+" : ""}${formatInteger(value)}`;
}

function signedNumber(value: number, fractionDigits: number) {
  return `${value >= 0 ? "+" : ""}${value.toFixed(fractionDigits)}`;
}

function formatSeed(value: number) {
  return `0x${value.toString(16).padStart(8, "0")}`;
}

function formatSeedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
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
