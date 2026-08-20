import { writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  CRACKED,
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  DEFAULT_PHASE_HORIZON_WEIGHTS,
  extractPhaseHorizonFeatures,
  scorePhaseHorizonFeatures,
  type PhaseHorizonWeights,
} from "../../../src/core/typescript/phase-horizon-evaluator.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import {
  evaluateFairPosition,
  initialFairPolicyWeights,
  type FairPolicyWeights,
} from "../fair-policy/tune.ts";

/**
 * Adds only a phase-throughput residual to the fixed fair-policy leaf.
 * Candidate selection uses two development seeds, then locks the exact scale
 * for a sixteen-game confirmation. Calibration starts only after the selected
 * policy clears both the 400k score and 5% paired-baseline gates.
 */

const TRAINING_SEED_START = 0x1d70_0000;
const CALIBRATION_SEED_START = 0x5d70_0000;
const VALIDATION_SEED_START = 0x7d70_0000;
const RESERVED_FINAL_SEED_START = 0xd700_0000;
const POLICY_SEED = 0xd707_5eed;
const REVEAL_DOMAIN = 0x5245_564c;
const DEFAULT_PILOT_GAMES = 2;
const DEFAULT_CONFIRM_GAMES = 16;
const DEFAULT_CALIBRATION_GAMES = 16;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_MAX_WORK = 1_000_000;
const DEFAULT_MAX_CACHE = 40_000;
const DEFAULT_TERMINAL_UTILITY = -1_000_000;
const REQUIRED_TRAINING_MEAN = 400_000;
const MATERIAL_IMPROVEMENT = 1.05;
const RESIDUAL_SCALES = [0.25, 0.5, 1] as const;

export const FAIR_PHASE_BASELINE_WEIGHTS: FairPolicyWeights = {
  ...initialFairPolicyWeights(),
  directPotential: 1_600,
  latentChainPotential: 700,
  heightLoad: -20,
  roughness: 0,
  revealedCoverValue: 300,
};

/** Fixed phase-only weight vector used strictly as a residual. */
export const RELEASE_DOUBLE_PHASE_WEIGHTS: PhaseHorizonWeights = {
  ...DEFAULT_PHASE_HORIZON_WEIGHTS,
  triggerReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.triggerReadiness * 2,
  releaseReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.releaseReadiness * 2,
};

interface Arguments {
  pilotGames: number;
  confirmGames: number;
  calibrationGames: number;
  maxMoves: number;
  maxWork: number;
  maxCacheEntries: number;
  terminalUtility: number;
  outputPath?: string;
  pilotOnly: boolean;
  selfTest: boolean;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  numberedCleared: number;
  coversRevealed: number;
  coversCracked: number;
  maxChain: number;
  meanDepth: number;
  workPerMove: number;
}

interface Summary {
  games: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanNumberedCleared: number;
  meanCoversRevealed: number;
  meanCoversCracked: number;
  meanMaxChain: number;
  meanDepth: number;
  meanWorkPerMove: number;
  results: readonly GameResult[];
}

interface CandidateResult {
  name: string;
  residualScale: number;
  summary: Summary;
  pairedMeanScoreDelta: number;
  pairedMeanMoveDelta: number;
}

interface LabArtifact {
  format: "drop7-fair-phase-combination";
  version: 1;
  seedPolicy: {
    trainingSeedStart: number;
    calibrationSeedStart: number;
    validationSeedStart: number;
    reservedFinalSeedStart: number;
  };
  search: {
    depth: 3;
    samples: 5;
    policySeed: number;
    maxWork: number;
    maxCacheEntries: number;
    terminalUtility: number;
    maxMoves: number;
  };
  theory: {
    fairWeights: FairPolicyWeights;
    phaseWeights: PhaseHorizonWeights;
    residualScales: readonly number[];
  };
  pilot: CandidateResult[];
  frozenScale: number | null;
  confirmation: CandidateResult[] | null;
  qualified: boolean;
  calibration: CandidateResult[] | null;
  rejected: readonly string[];
}

type LeafEvaluator = (state: GameState) => number;

export function createFairPhaseEvaluator(residualScale: number): LeafEvaluator {
  if (!Number.isFinite(residualScale) || residualScale < 0) {
    throw new Error("fair phase residual scale must be finite and nonnegative");
  }
  return (state) => {
    const fairValue = evaluateFairPosition(state, FAIR_PHASE_BASELINE_WEIGHTS);
    if (state.gameOver || residualScale === 0) return fairValue;
    const phaseValue = scorePhaseHorizonFeatures(
      extractPhaseHorizonFeatures(state),
      RELEASE_DOUBLE_PHASE_WEIGHTS,
    );
    return fairValue + residualScale * phaseValue;
  };
}

export async function runCombinationLab(
  options: Arguments,
): Promise<LabArtifact> {
  const cache = new Map<string, GameResult>();
  const pilotSeeds = seedRange(TRAINING_SEED_START, options.pilotGames);
  const baselinePilot = evaluateCandidate(
    "fair-only",
    0,
    pilotSeeds,
    options,
    cache,
    "pilot",
  );
  const pilot = [
    baselinePilot,
    ...RESIDUAL_SCALES.map((scale) =>
      evaluateCandidate(
        `fair+phase-${scale}`,
        scale,
        pilotSeeds,
        options,
        cache,
        "pilot",
        baselinePilot.summary,
      ),
    ),
  ];
  printLeaderboard("training pilot", pilot);
  const winner = [...pilot]
    .filter((candidate) => candidate.residualScale > 0)
    .sort(compareCandidate)[0];
  const materialPilot =
    winner !== undefined &&
    winner.summary.meanScore >=
      baselinePilot.summary.meanScore * MATERIAL_IMPROVEMENT;
  const rejected = pilot
    .filter(
      (candidate) =>
        candidate.residualScale > 0 && candidate.name !== winner?.name,
    )
    .map(
      (candidate) =>
        `${candidate.name}: pilot mean ${Math.round(candidate.summary.meanScore)} (${signed(candidate.pairedMeanScoreDelta)} vs fair-only)`,
    );

  if (!winner || options.pilotOnly || !materialPilot) {
    const reason = !winner
      ? "no residual candidate was evaluated"
      : options.pilotOnly
        ? "pilot-only mode"
        : `best pilot failed the ${Math.round((MATERIAL_IMPROVEMENT - 1) * 100)}% material-improvement gate`;
    if (winner && !options.pilotOnly && !materialPilot) {
      rejected.push(
        `${winner.name}: pilot mean ${Math.round(winner.summary.meanScore)} failed the material baseline gate`,
      );
    }
    process.stdout.write(`confirmation and calibration skipped: ${reason}\n`);
    return artifact(options, pilot, null, null, false, null, rejected);
  }

  // Lock this scalar before increasing the training sample; confirmation and
  // calibration cannot switch to a different scale.
  const frozenScale = winner.residualScale;
  const confirmationSeeds = seedRange(
    TRAINING_SEED_START,
    options.confirmGames,
  );
  const baselineConfirmation = evaluateCandidate(
    "fair-only",
    0,
    confirmationSeeds,
    options,
    cache,
    "confirmation",
  );
  const winnerConfirmation = evaluateCandidate(
    winner.name,
    frozenScale,
    confirmationSeeds,
    options,
    cache,
    "confirmation",
    baselineConfirmation.summary,
  );
  const confirmation = [baselineConfirmation, winnerConfirmation];
  printLeaderboard("frozen training confirmation", confirmation);
  const qualified =
    options.confirmGames >= 16 &&
    winnerConfirmation.summary.meanScore >= REQUIRED_TRAINING_MEAN &&
    winnerConfirmation.summary.meanScore >=
      baselineConfirmation.summary.meanScore * MATERIAL_IMPROVEMENT;
  if (!qualified) {
    rejected.push(
      `${winner.name}: confirmation mean ${Math.round(winnerConfirmation.summary.meanScore)} did not clear both ${REQUIRED_TRAINING_MEAN} and the material baseline gate`,
    );
    process.stdout.write(
      `calibration skipped: frozen scale ${frozenScale} did not qualify on ${options.confirmGames} training games\n`,
    );
    return artifact(
      options,
      pilot,
      frozenScale,
      confirmation,
      false,
      null,
      rejected,
    );
  }

  const calibrationSeeds = seedRange(
    CALIBRATION_SEED_START,
    options.calibrationGames,
  );
  const baselineCalibration = evaluateCandidate(
    "fair-only",
    0,
    calibrationSeeds,
    options,
    cache,
    "calibration",
  );
  const winnerCalibration = evaluateCandidate(
    winner.name,
    frozenScale,
    calibrationSeeds,
    options,
    cache,
    "calibration",
    baselineCalibration.summary,
  );
  const calibration = [baselineCalibration, winnerCalibration];
  printLeaderboard("frozen calibration", calibration);
  return artifact(
    options,
    pilot,
    frozenScale,
    confirmation,
    true,
    calibration,
    rejected,
  );
}

function artifact(
  options: Arguments,
  pilot: CandidateResult[],
  frozenScale: number | null,
  confirmation: CandidateResult[] | null,
  qualified: boolean,
  calibration: CandidateResult[] | null,
  rejected: string[],
): LabArtifact {
  return {
    format: "drop7-fair-phase-combination",
    version: 1,
    seedPolicy: {
      trainingSeedStart: TRAINING_SEED_START,
      calibrationSeedStart: CALIBRATION_SEED_START,
      validationSeedStart: VALIDATION_SEED_START,
      reservedFinalSeedStart: RESERVED_FINAL_SEED_START,
    },
    search: {
      depth: 3,
      samples: 5,
      policySeed: POLICY_SEED,
      maxWork: options.maxWork,
      maxCacheEntries: options.maxCacheEntries,
      terminalUtility: options.terminalUtility,
      maxMoves: options.maxMoves,
    },
    theory: {
      fairWeights: FAIR_PHASE_BASELINE_WEIGHTS,
      phaseWeights: RELEASE_DOUBLE_PHASE_WEIGHTS,
      residualScales: RESIDUAL_SCALES,
    },
    pilot,
    frozenScale,
    confirmation,
    qualified,
    calibration,
    rejected,
  };
}

function evaluateCandidate(
  name: string,
  residualScale: number,
  seeds: readonly number[],
  options: Arguments,
  cache: Map<string, GameResult>,
  stage: string,
  baseline?: Summary,
): CandidateResult {
  const results = seeds.map((seed, index) => {
    const key = `${residualScale}:${seed}`;
    const cached = cache.get(key);
    if (cached) return cached;
    const result = runGame(seed, residualScale, options);
    cache.set(key, result);
    process.stderr.write(
      `${stage} ${name} ${index + 1}/${seeds.length} ${formatSeed(seed)} · ${result.score.toLocaleString("en-US")} · ${result.moves} moves${result.censored ? " capped" : ""}\n`,
    );
    return result;
  });
  const summary = summarize(results);
  return {
    name,
    residualScale,
    summary,
    pairedMeanScoreDelta: baseline
      ? mean(
          results.map(
            (result, index) => result.score - baseline.results[index].score,
          ),
        )
      : 0,
    pairedMeanMoveDelta: baseline
      ? mean(
          results.map(
            (result, index) => result.moves - baseline.results[index].moves,
          ),
        )
      : 0,
  };
}

function runGame(
  seed: number,
  residualScale: number,
  options: Arguments,
): GameResult {
  assertAllowedSeed(seed);
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  const evaluator = createFairPhaseEvaluator(residualScale);
  let numberedCleared = 0;
  let coversRevealed = 0;
  let coversCracked = 0;
  let maxChain = 0;
  let depth = 0;
  let work = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const evaluation = evaluateSparseExpectimaxMoves(state, {
      maxDepth: 3,
      chanceSamples: 5,
      maxWork: options.maxWork,
      maxCacheEntries: options.maxCacheEntries,
      seed: POLICY_SEED,
      terminalUtility: options.terminalUtility,
      evaluator,
    });
    if (evaluation.bestColumn === null) {
      throw new Error("Fair + phase lab found no move in a live game");
    }
    depth += evaluation.depth;
    work += evaluation.work;
    const revealSeed = mix32(
      seed ^
        Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(
      state,
      evaluation.bestColumn,
      seededRandom(revealSeed),
      { captureAnimation: true },
    );
    if (!move) throw new Error("Fair + phase lab selected an illegal move");
    for (const wave of move.waves) {
      numberedCleared += wave.cleared;
      coversRevealed += wave.revealed;
    }
    for (const frame of move.animation) {
      if (frame.kind !== "impact") continue;
      for (const index of frame.indexes) {
        if (frame.board[index] === CRACKED) coversCracked += 1;
      }
    }
    maxChain = Math.max(maxChain, move.waves.length);
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }

  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    numberedCleared,
    coversRevealed,
    coversCracked,
    maxChain,
    meanDepth: state.movesPlayed === 0 ? 0 : depth / state.movesPlayed,
    workPerMove: state.movesPlayed === 0 ? 0 : work / state.movesPlayed,
  };
}

function summarize(results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    games: results.length,
    meanScore: mean(scores),
    medianScore: percentile(scores, 0.5),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censoredGames: results.filter((result) => result.censored).length,
    meanNumberedCleared: mean(
      results.map((result) => result.numberedCleared),
    ),
    meanCoversRevealed: mean(
      results.map((result) => result.coversRevealed),
    ),
    meanCoversCracked: mean(results.map((result) => result.coversCracked)),
    meanMaxChain: mean(results.map((result) => result.maxChain)),
    meanDepth: mean(results.map((result) => result.meanDepth)),
    meanWorkPerMove: mean(results.map((result) => result.workPerMove)),
    results,
  };
}

function printLeaderboard(label: string, results: readonly CandidateResult[]) {
  process.stdout.write(`\n${label}\n`);
  for (const result of [...results].sort(compareCandidate)) {
    process.stdout.write(
      `${result.name.padEnd(17)} mean ${Math.round(result.summary.meanScore).toLocaleString("en-US")} · moves ${result.summary.meanMoves.toFixed(1)} · clears ${result.summary.meanNumberedCleared.toFixed(1)} · reveals ${result.summary.meanCoversRevealed.toFixed(1)} · chain ${result.summary.meanMaxChain.toFixed(2)} · ${signed(result.pairedMeanScoreDelta)} vs fair-only\n`,
    );
  }
}

function compareCandidate(first: CandidateResult, second: CandidateResult) {
  return (
    second.summary.meanScore - first.summary.meanScore ||
    second.summary.meanMoves - first.summary.meanMoves ||
    first.name.localeCompare(second.name)
  );
}

function seedRange(start: number, count: number) {
  const seeds = Array.from(
    { length: count },
    (_, index) => (start + index) >>> 0,
  );
  for (const seed of seeds) assertAllowedSeed(seed);
  return seeds;
}

function assertAllowedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("fair phase game seed must be a uint32");
  }
  const training = seed >= TRAINING_SEED_START && seed < CALIBRATION_SEED_START;
  const calibration =
    seed >= CALIBRATION_SEED_START && seed < VALIDATION_SEED_START;
  if (!training && !calibration) {
    throw new Error(
      `seed ${formatSeed(seed)} is outside training/calibration; validation and final ranges are forbidden`,
    );
  }
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function percentile(sorted: readonly number[], fraction: number) {
  if (sorted.length === 1) return sorted[0];
  const position = fraction * (sorted.length - 1);
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  const mix = position - lower;
  return sorted[lower] * (1 - mix) + sorted[upper] * mix;
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function signed(value: number) {
  const rounded = Math.round(value).toLocaleString("en-US");
  return `${value >= 0 ? "+" : ""}${rounded}`;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const options: Arguments = {
    pilotGames: DEFAULT_PILOT_GAMES,
    confirmGames: DEFAULT_CONFIRM_GAMES,
    calibrationGames: DEFAULT_CALIBRATION_GAMES,
    maxMoves: DEFAULT_MAX_MOVES,
    maxWork: DEFAULT_MAX_WORK,
    maxCacheEntries: DEFAULT_MAX_CACHE,
    terminalUtility: DEFAULT_TERMINAL_UTILITY,
    pilotOnly: false,
    selfTest: false,
  };
  const numeric = new Map<string, keyof Arguments>([
    ["--pilot-games", "pilotGames"],
    ["--confirm-games", "confirmGames"],
    ["--calibration-games", "calibrationGames"],
    ["--max-moves", "maxMoves"],
    ["--max-work", "maxWork"],
    ["--max-cache", "maxCacheEntries"],
    ["--terminal-utility", "terminalUtility"],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--pilot-only") {
      options.pilotOnly = true;
      continue;
    }
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
    (options as unknown as Record<string, number>)[key] = Number(
      requiredValue(arguments_, ++index, flag),
    );
  }
  for (const key of [
    "pilotGames",
    "confirmGames",
    "calibrationGames",
    "maxMoves",
    "maxWork",
    "maxCacheEntries",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  if (!Number.isFinite(options.terminalUtility)) {
    throw new Error("terminalUtility must be finite");
  }
  return options;
}

function requiredValue(arguments_: readonly string[], index: number, flag: string) {
  const value = arguments_[index];
  if (value === undefined) throw new Error(`${flag} needs a value`);
  return value;
}

export function runSelfTest() {
  const parsed = parseArguments(["--pilot-games", "1", "--max-moves", "20"]);
  if (parsed.pilotGames !== 1 || parsed.maxMoves !== 20) {
    throw new Error("fair phase argument parser failed");
  }
  assertAllowedSeed(TRAINING_SEED_START);
  assertAllowedSeed(CALIBRATION_SEED_START);
  for (const seed of [VALIDATION_SEED_START, RESERVED_FINAL_SEED_START]) {
    let threw = false;
    try {
      assertAllowedSeed(seed);
    } catch {
      threw = true;
    }
    if (!threw) throw new Error("reserved seed guard failed");
  }
  const evaluator = createFairPhaseEvaluator(0.5);
  const initial: GameState = {
    board: createInitialBoard(),
    nextDisc: 4,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  if (!Number.isFinite(evaluator(initial))) {
    throw new Error("fair phase evaluator produced a non-finite value");
  }
  process.stdout.write("drop7 fair + phase combination self-test passed\n");
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  process.stdout.write(
    `fair + phase sparse d3/s5 · training ${formatSeed(TRAINING_SEED_START)}+ · calibration ${formatSeed(CALIBRATION_SEED_START)}+ gated · validation ${formatSeed(VALIDATION_SEED_START)}+ untouched · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  const result = await runCombinationLab(options);
  if (options.outputPath) {
    await writeFile(
      options.outputPath,
      `${JSON.stringify(result, null, 2)}\n`,
      "utf8",
    );
    process.stdout.write(`report ${resolve(options.outputPath)}\n`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await runCli(process.argv.slice(2));
}
