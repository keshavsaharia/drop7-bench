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
  evaluatePhaseHorizon,
  type PhaseHorizonWeights,
} from "../../../src/core/typescript/phase-horizon-evaluator.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";

/**
 * Training-only coordinate sweep for the phase-aware sparse horizon evaluator.
 *
 * The sweep is intentionally small: each weight family is halved or doubled
 * around one fixed center. The selected weights are locked before calibration
 * is allowed. The 0x7d70 validation and 0xd700 final ranges are hard-rejected.
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

interface Arguments {
  pilotGames: number;
  confirmGames: number;
  calibrationGames: number;
  maxMoves: number;
  maxWork: number;
  maxCacheEntries: number;
  terminalUtility: number;
  presetNames?: string[];
  outputPath?: string;
  pilotOnly: boolean;
  selfTest: boolean;
}

interface Scales {
  queue: number;
  altitude: number;
  clog: number;
  build: number;
  release: number;
}

interface Preset {
  name: string;
  scales: Scales;
  weights: PhaseHorizonWeights;
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
  scales: Scales | null;
  weights: PhaseHorizonWeights | null;
  summary: Summary;
  pairedMeanScoreDelta: number;
  pairedMeanMoveDelta: number;
}

interface LabArtifact {
  format: "drop7-phase-horizon-sweep";
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
  pilot: CandidateResult[];
  frozenChampion: string | null;
  confirmation: CandidateResult[] | null;
  qualified: boolean;
  calibration: CandidateResult[] | null;
  rejected: readonly string[];
}

const CENTER_SCALES: Scales = {
  queue: 1,
  altitude: 1,
  clog: 1,
  build: 1,
  release: 1,
};

const PRESETS: readonly Preset[] = [
  preset("center", {}),
  preset("queue-half", { queue: 0.5 }),
  preset("queue-double", { queue: 2 }),
  preset("altitude-half", { altitude: 0.5 }),
  preset("altitude-double", { altitude: 2 }),
  preset("clog-half", { clog: 0.5 }),
  preset("clog-double", { clog: 2 }),
  preset("build-half", { build: 0.5 }),
  preset("build-double", { build: 2 }),
  preset("release-half", { release: 0.5 }),
  preset("release-double", { release: 2 }),
  preset("release-quad", { release: 4 }),
  preset("release2-altitude-half", { release: 2, altitude: 0.5 }),
  preset("release2-build-half", { release: 2, build: 0.5 }),
  preset("release2-clog-double", { release: 2, clog: 2 }),
  preset("release2-safety-double", {
    release: 2,
    queue: 2,
    altitude: 2,
  }),
];

function preset(name: string, overrides: Partial<Scales>): Preset {
  const scales = { ...CENTER_SCALES, ...overrides };
  return { name, scales, weights: scaledWeights(scales) };
}

function scaledWeights(scales: Scales): PhaseHorizonWeights {
  const center = DEFAULT_PHASE_HORIZON_WEIGHTS;
  return {
    baselineScale: center.baselineScale,
    projectedOccupancyDebt:
      center.projectedOccupancyDebt * scales.queue,
    residualCoverDebt: center.residualCoverDebt * scales.queue,
    coverAltitudeDebt: center.coverAltitudeDebt * scales.altitude,
    imminentCoverAltitudeDebt:
      center.imminentCoverAltitudeDebt * scales.altitude,
    peakHeightRisk: center.peakHeightRisk * scales.altitude,
    lowCapLoad: center.lowCapLoad * scales.clog,
    adjacentLowCapLoad: center.adjacentLowCapLoad * scales.clog,
    directBuildInventory: center.directBuildInventory * scales.build,
    quietBuildOptions: center.quietBuildOptions * scales.build,
    quietDirectGain: center.quietDirectGain * scales.build,
    triggerReadiness: center.triggerReadiness * scales.release,
    releaseReadiness: center.releaseReadiness * scales.release,
  };
}

export async function runSweep(options: Arguments): Promise<LabArtifact> {
  const selected = selectPresets(options.presetNames);
  const cache = new Map<string, GameResult>();
  const trainingPilotSeeds = seedRange(
    TRAINING_SEED_START,
    options.pilotGames,
  );
  const baselinePilot = evaluateCandidate(
    "combined",
    null,
    null,
    trainingPilotSeeds,
    options,
    cache,
    "pilot",
  );
  const pilot = [
    baselinePilot,
    ...selected.map((candidate) =>
      evaluateCandidate(
        candidate.name,
        candidate.scales,
        candidate.weights,
        trainingPilotSeeds,
        options,
        cache,
        "pilot",
        baselinePilot.summary,
      ),
    ),
  ];
  pilot.sort(compareCandidate);
  printLeaderboard("training pilot", pilot);

  const bestPhase = pilot.find((candidate) => candidate.name !== "combined");
  const pilotMaterial =
    bestPhase !== undefined &&
    bestPhase.summary.meanScore >=
      baselinePilot.summary.meanScore * MATERIAL_IMPROVEMENT;
  const rejected = pilot
    .filter(
      (candidate) =>
        candidate.name !== "combined" && candidate.name !== bestPhase?.name,
    )
    .map((candidate) =>
      `${candidate.name}: pilot mean ${Math.round(candidate.summary.meanScore)} (${signed(candidate.pairedMeanScoreDelta)} vs combined)`,
    );

  if (!bestPhase || options.pilotOnly || !pilotMaterial) {
    const reason = !bestPhase
      ? "no phase candidate was evaluated"
      : options.pilotOnly
        ? "pilot-only mode"
        : `best pilot failed the ${Math.round((MATERIAL_IMPROVEMENT - 1) * 100)}% material-improvement gate`;
    process.stdout.write(`calibration skipped: ${reason}\n`);
    return artifact(options, pilot, null, null, false, null, rejected);
  }

  // Lock the preset name and exact weights before expanding the training
  // sample; confirmation and calibration cannot change candidate selection.
  const frozen = selected.find((candidate) => candidate.name === bestPhase.name)!;
  const confirmationSeeds = seedRange(
    TRAINING_SEED_START,
    options.confirmGames,
  );
  const baselineConfirmation = evaluateCandidate(
    "combined",
    null,
    null,
    confirmationSeeds,
    options,
    cache,
    "confirmation",
  );
  const championConfirmation = evaluateCandidate(
    frozen.name,
    frozen.scales,
    frozen.weights,
    confirmationSeeds,
    options,
    cache,
    "confirmation",
    baselineConfirmation.summary,
  );
  const confirmation = [baselineConfirmation, championConfirmation];
  printLeaderboard("training confirmation", confirmation);
  const qualified =
    options.confirmGames >= 16 &&
    championConfirmation.summary.meanScore >= REQUIRED_TRAINING_MEAN &&
    championConfirmation.summary.meanScore >=
      baselineConfirmation.summary.meanScore * MATERIAL_IMPROVEMENT;
  if (!qualified) {
    rejected.push(
      `${frozen.name}: confirmation mean ${Math.round(championConfirmation.summary.meanScore)} did not clear both ${REQUIRED_TRAINING_MEAN} and the material baseline gate`,
    );
    process.stdout.write(
      `calibration skipped: frozen ${frozen.name} did not qualify on ${options.confirmGames} training games\n`,
    );
    return artifact(
      options,
      pilot,
      frozen.name,
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
    "combined",
    null,
    null,
    calibrationSeeds,
    options,
    cache,
    "calibration",
  );
  const championCalibration = evaluateCandidate(
    frozen.name,
    frozen.scales,
    frozen.weights,
    calibrationSeeds,
    options,
    cache,
    "calibration",
    baselineCalibration.summary,
  );
  const calibration = [baselineCalibration, championCalibration];
  printLeaderboard("frozen calibration", calibration);
  return artifact(
    options,
    pilot,
    frozen.name,
    confirmation,
    true,
    calibration,
    rejected,
  );
}

function artifact(
  options: Arguments,
  pilot: CandidateResult[],
  frozenChampion: string | null,
  confirmation: CandidateResult[] | null,
  qualified: boolean,
  calibration: CandidateResult[] | null,
  rejected: string[],
): LabArtifact {
  return {
    format: "drop7-phase-horizon-sweep",
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
    pilot,
    frozenChampion,
    confirmation,
    qualified,
    calibration,
    rejected,
  };
}

function evaluateCandidate(
  name: string,
  scales: Scales | null,
  weights: PhaseHorizonWeights | null,
  seeds: readonly number[],
  options: Arguments,
  cache: Map<string, GameResult>,
  stage: string,
  baseline?: Summary,
): CandidateResult {
  const results = seeds.map((seed, index) => {
    const key = `${name}:${seed}`;
    const cached = cache.get(key);
    if (cached) return cached;
    const result = runGame(seed, weights, options);
    cache.set(key, result);
    process.stderr.write(
      `${stage} ${name} ${index + 1}/${seeds.length} ${formatSeed(seed)} · ${result.score.toLocaleString("en-US")} · ${result.moves} moves${result.censored ? " capped" : ""}\n`,
    );
    return result;
  });
  const summary = summarize(results);
  const pairedMeanScoreDelta = baseline
    ? mean(
        results.map(
          (result, index) => result.score - baseline.results[index].score,
        ),
      )
    : 0;
  const pairedMeanMoveDelta = baseline
    ? mean(
        results.map(
          (result, index) => result.moves - baseline.results[index].moves,
        ),
      )
    : 0;
  return {
    name,
    scales,
    weights,
    summary,
    pairedMeanScoreDelta,
    pairedMeanMoveDelta,
  };
}

function runGame(
  seed: number,
  weights: PhaseHorizonWeights | null,
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
  const evaluator = weights
    ? (position: GameState) => evaluatePhaseHorizon(position, weights)
    : undefined;
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
      heuristicProfile: "combined",
      ...(evaluator ? { evaluator } : {}),
    });
    if (evaluation.bestColumn === null) {
      throw new Error("Sparse phase lab found no move in a live game");
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
    if (!move) throw new Error("Sparse phase lab selected an illegal move");
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
      `${result.name.padEnd(16)} mean ${Math.round(result.summary.meanScore).toLocaleString("en-US")} · moves ${result.summary.meanMoves.toFixed(1)} · clears ${result.summary.meanNumberedCleared.toFixed(1)} · reveals ${result.summary.meanCoversRevealed.toFixed(1)} · chain ${result.summary.meanMaxChain.toFixed(2)} · ${signed(result.pairedMeanScoreDelta)} vs combined\n`,
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

function selectPresets(names?: readonly string[]) {
  if (!names) return [...PRESETS];
  const selected = names.map((name) => {
    const candidate = PRESETS.find((item) => item.name === name);
    if (!candidate) {
      throw new Error(
        `Unknown preset ${name}; choose ${PRESETS.map((item) => item.name).join(", ")}`,
      );
    }
    return candidate;
  });
  return [...new Map(selected.map((item) => [item.name, item])).values()];
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
    throw new Error("phase horizon game seed must be a uint32");
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
    if (flag === "--presets") {
      options.presetNames = requiredValue(arguments_, ++index, flag)
        .split(",")
        .filter(Boolean);
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
  selectPresets(options.presetNames);
  return options;
}

function requiredValue(arguments_: readonly string[], index: number, flag: string) {
  const value = arguments_[index];
  if (value === undefined) throw new Error(`${flag} needs a value`);
  return value;
}

export function runSelfTest() {
  const parsed = parseArguments([
    "--pilot-games",
    "1",
    "--presets",
    "center,build-double",
  ]);
  if (parsed.pilotGames !== 1 || parsed.presetNames?.length !== 2) {
    throw new Error("phase horizon argument parser failed");
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
  if (PRESETS.some((candidate) => !Number.isFinite(candidate.weights.lowCapLoad))) {
    throw new Error("phase horizon preset contains invalid weights");
  }
  process.stdout.write("drop7 phase horizon lab self-test passed\n");
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  process.stdout.write(
    `phase horizon sparse d3/s5 · training ${formatSeed(TRAINING_SEED_START)}+ · calibration ${formatSeed(CALIBRATION_SEED_START)}+ only after qualification · validation ${formatSeed(VALIDATION_SEED_START)}+ untouched · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  const result = await runSweep(options);
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
