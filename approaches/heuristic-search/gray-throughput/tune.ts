import { writeFile } from "node:fs/promises";
import { resolve } from "node:path";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS,
  evaluateGrayThroughputMoves,
  type GrayStateFeatures,
  type GrayThroughputWeights,
  type GrayTransitionFeatures,
} from "../../../src/core/typescript/gray-throughput-policy.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";

const TRAINING_SEED_START = 0x1d70_0000;
const CALIBRATION_SEED_START = 0x5d70_0000;
const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const DEFAULT_TUNER_SEED = 0x6772_7475;
const DEFAULT_POLICY_SEED = 0x6772_6179;
const CEM_RATE = 0.7;
const MINIMUM_DEVIATION = 0.08;
const MINIMUM_LOG_MULTIPLIER = -4;
const MAXIMUM_LOG_MULTIPLIER = 3;

const STATE_KEYS = Object.keys(
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS.state,
) as (keyof GrayStateFeatures)[];
const TRANSITION_KEYS = Object.keys(
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS.transition,
) as (keyof GrayTransitionFeatures)[];
const PARAMETER_COUNT = STATE_KEYS.length + TRANSITION_KEYS.length;

interface Arguments {
  generations: number;
  population: number;
  elites: number;
  games: number;
  finalGames: number;
  samples: number;
  maxMoves: number;
  tunerSeed: number;
  policySeed: number;
  output: string;
  calibrate: boolean;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
}

interface Summary {
  objective: number;
  meanScore: number;
  medianScore: number;
  meanMoves: number;
  censored: number;
  results: readonly GameResult[];
}

interface Candidate {
  vector: number[];
  summary: Summary;
}

interface Distribution {
  means: number[];
  deviations: number[];
}

function runGame(
  seed: number,
  weights: GrayThroughputWeights,
  samples: number,
  policySeed: number,
  maxMoves: number,
): GameResult {
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
    const decision = evaluateGrayThroughputMoves(state, {
      samples,
      continuationSamples: 1,
      depth: 1,
      policySeed,
      weights,
    });
    if (decision.bestColumn === null) {
      throw new Error("gray tuner found no move for a live state");
    }
    const move = playMove(
      state,
      decision.bestColumn,
      seededRandom(
        mix32(
          seed ^
            Math.imul(state.movesPlayed + 1, ACTUAL_MOVE_MULTIPLIER) ^
            ACTUAL_REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`illegal gray move ${decision.bestColumn}`);
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
  };
}

function evaluate(
  vector: readonly number[],
  seeds: readonly number[],
  options: Arguments,
) {
  const weights = vectorToWeights(vector);
  const results = seeds.map((seed) =>
    runGame(
      seed,
      weights,
      options.samples,
      options.policySeed,
      options.maxMoves,
    ),
  );
  return summarize(results, options.maxMoves);
}

function summarize(
  results: readonly GameResult[],
  maxMoves: number,
): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  const normalizedLogs = results.map(
    (result) => Math.log1p(result.score) / Math.log1p(1_000_000),
  );
  const normalizedMoves = results.map((result) => result.moves / maxMoves);
  return {
    // Log score makes one lucky chain unable to dominate a generation. Moves
    // and censoring keep the optimizer focused on long-run survival.
    objective:
      mean(normalizedLogs) * 0.65 +
      mean(normalizedMoves) * 0.25 +
      (results.filter((result) => result.censored).length / results.length) *
        0.1,
    meanScore: mean(scores),
    medianScore: median(scores),
    meanMoves: mean(results.map((result) => result.moves)),
    censored: results.filter((result) => result.censored).length,
    results,
  };
}

async function tune(options: Arguments) {
  const trainingSeeds = consecutiveSeeds(TRAINING_SEED_START, options.games);
  const finalTrainingSeeds = consecutiveSeeds(
    TRAINING_SEED_START,
    options.finalGames,
  );
  let distribution: Distribution = {
    means: Array<number>(PARAMETER_COUNT).fill(0),
    deviations: Array<number>(PARAMETER_COUNT).fill(0.8),
  };
  let champion: Candidate = {
    vector: Array<number>(PARAMETER_COUNT).fill(0),
    summary: evaluate(
      Array<number>(PARAMETER_COUNT).fill(0),
      trainingSeeds,
      options,
    ),
  };

  process.stdout.write(
    `gray program search · train ${seedRange(trainingSeeds)} · calibration ${formatSeed(CALIBRATION_SEED_START)}+ ${options.calibrate ? "enabled" : "untouched"}\n`,
  );
  process.stdout.write(`baseline · ${formatSummary(champion.summary)}\n`);

  for (let generation = 0; generation < options.generations; generation += 1) {
    const candidates = population(
      distribution,
      champion.vector,
      options.population,
      options.tunerSeed,
      generation,
    ).map((vector): Candidate => ({
      vector,
      summary: evaluate(vector, trainingSeeds, options),
    }));
    candidates.sort(compareCandidates);
    const elites = candidates.slice(0, options.elites);
    if (compareCandidates(elites[0], champion) < 0) champion = elites[0];
    distribution = updateDistribution(distribution, elites);
    process.stdout.write(
      `generation ${(generation + 1).toString().padStart(2)} · ${formatSummary(elites[0].summary)} · champion ${champion.summary.objective.toFixed(5)}\n`,
    );
    await checkpoint(options.output, options, generation + 1, distribution, champion);
  }

  const baselineVector = Array<number>(PARAMETER_COUNT).fill(0);
  const finalBaseline = evaluate(baselineVector, finalTrainingSeeds, options);
  const finalChampion = evaluate(champion.vector, finalTrainingSeeds, options);
  process.stdout.write(`training baseline · ${formatSummary(finalBaseline)}\n`);
  process.stdout.write(`training champion · ${formatSummary(finalChampion)}\n`);
  process.stdout.write(
    `${formatPaired(finalBaseline.results, finalChampion.results)}\n`,
  );

  let calibration:
    | { baseline: Summary; champion: Summary }
    | undefined;
  if (options.calibrate) {
    const calibrationSeeds = consecutiveSeeds(
      CALIBRATION_SEED_START,
      options.finalGames,
    );
    calibration = {
      baseline: evaluate(baselineVector, calibrationSeeds, options),
      champion: evaluate(champion.vector, calibrationSeeds, options),
    };
    process.stdout.write(
      `calibration baseline · ${formatSummary(calibration.baseline)}\n`,
    );
    process.stdout.write(
      `calibration champion · ${formatSummary(calibration.champion)}\n`,
    );
    process.stdout.write(
      `${formatPaired(calibration.baseline.results, calibration.champion.results)}\n`,
    );
  }

  await checkpoint(
    options.output,
    options,
    options.generations,
    distribution,
    champion,
    {
      training: {
        baseline: omitResults(finalBaseline),
        champion: omitResults(finalChampion),
      },
      calibration: calibration
        ? {
            baseline: omitResults(calibration.baseline),
            champion: omitResults(calibration.champion),
          }
        : undefined,
    },
  );
  process.stdout.write(`checkpoint ${resolve(options.output)}\n`);
}

function population(
  distribution: Distribution,
  champion: readonly number[],
  size: number,
  tunerSeed: number,
  generation: number,
) {
  const vectors = [[...champion]];
  const random = seededRandom(
    mix32(tunerSeed ^ Math.imul(generation + 1, 0x9e37_79b9)),
  );
  while (vectors.length < size) {
    vectors.push(
      distribution.means.map((average, index) =>
        clip(average + gaussian(random) * distribution.deviations[index]),
      ),
    );
  }
  return vectors;
}

function updateDistribution(
  previous: Distribution,
  elites: readonly Candidate[],
): Distribution {
  const eliteMeans = previous.means.map((_, index) =>
    mean(elites.map((elite) => elite.vector[index])),
  );
  const eliteDeviations = previous.deviations.map((_, index) => {
    const average = eliteMeans[index];
    return Math.sqrt(
      mean(
        elites.map((elite) => (elite.vector[index] - average) ** 2),
      ),
    );
  });
  return {
    means: previous.means.map((average, index) =>
      clip(average * (1 - CEM_RATE) + eliteMeans[index] * CEM_RATE),
    ),
    deviations: previous.deviations.map((deviation, index) =>
      Math.max(
        MINIMUM_DEVIATION,
        deviation * (1 - CEM_RATE) + eliteDeviations[index] * CEM_RATE,
      ),
    ),
  };
}

function vectorToWeights(vector: readonly number[]): GrayThroughputWeights {
  if (vector.length !== PARAMETER_COUNT) {
    throw new Error(`expected ${PARAMETER_COUNT} gray parameters`);
  }
  const state = { ...DEFAULT_GRAY_THROUGHPUT_WEIGHTS.state };
  const transition = { ...DEFAULT_GRAY_THROUGHPUT_WEIGHTS.transition };
  let index = 0;
  for (const key of STATE_KEYS) {
    state[key] *= Math.exp(vector[index]);
    index += 1;
  }
  for (const key of TRANSITION_KEYS) {
    transition[key] *= Math.exp(vector[index]);
    index += 1;
  }
  return {
    state,
    transition,
    continuationWeight: DEFAULT_GRAY_THROUGHPUT_WEIGHTS.continuationWeight,
  };
}

function compareCandidates(first: Candidate, second: Candidate) {
  return (
    second.summary.objective - first.summary.objective ||
    second.summary.meanMoves - first.summary.meanMoves ||
    second.summary.medianScore - first.summary.medianScore
  );
}

async function checkpoint(
  path: string,
  options: Arguments,
  generation: number,
  distribution: Distribution,
  champion: Candidate,
  evaluation?: unknown,
) {
  await writeFile(
    path,
    `${JSON.stringify(
      {
        version: 1,
        generation,
        options,
        parameters: [
          ...STATE_KEYS.map((key) => `state.${key}`),
          ...TRANSITION_KEYS.map((key) => `transition.${key}`),
        ],
        distribution,
        champion: {
          vector: champion.vector,
          weights: vectorToWeights(champion.vector),
          training: omitResults(champion.summary),
        },
        evaluation,
      },
      null,
      2,
    )}\n`,
  );
}

function formatPaired(
  baseline: readonly GameResult[],
  champion: readonly GameResult[],
) {
  const deltas = champion.map(
    (result, index) => result.score - baseline[index].score,
  );
  const interval = bootstrap(deltas, 10_000);
  return `paired ${signed(mean(deltas))} · median ${signed(median(deltas))} · W/T/L ${deltas.filter((value) => value > 0).length}/${deltas.filter((value) => value === 0).length}/${deltas.filter((value) => value < 0).length} · bootstrap95 [${signed(interval[0])}, ${signed(interval[1])}]`;
}

function bootstrap(values: readonly number[], samples: number) {
  const random = seededRandom(0x6772_626f);
  const means = Array<number>(samples);
  for (let sample = 0; sample < samples; sample += 1) {
    let total = 0;
    for (let index = 0; index < values.length; index += 1) {
      total += values[Math.floor(random() * values.length)];
    }
    means[sample] = total / values.length;
  }
  means.sort(numberOrder);
  return [
    means[Math.floor(samples * 0.025)],
    means[Math.floor(samples * 0.975)],
  ] as const;
}

function parseArguments(arguments_: readonly string[]): Arguments | null {
  const options: Arguments = {
    generations: 12,
    population: 32,
    elites: 8,
    games: 24,
    finalGames: 64,
    samples: 2,
    maxMoves: 1_000,
    tunerSeed: DEFAULT_TUNER_SEED,
    policySeed: DEFAULT_POLICY_SEED,
    output: "/tmp/drop7-gray-throughput.json",
    calibrate: false,
  };
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--calibrate") {
      options.calibrate = true;
      continue;
    }
    const text = arguments_[index + 1];
    if (text === undefined) throw new Error(`missing value after ${flag}`);
    index += 1;
    switch (flag) {
      case "--generations":
        options.generations = positiveInteger(text, flag, 1_000);
        break;
      case "--population":
        options.population = positiveInteger(text, flag, 1_000);
        break;
      case "--elites":
        options.elites = positiveInteger(text, flag, 999);
        break;
      case "--games":
        options.games = positiveInteger(text, flag, 10_000);
        break;
      case "--final-games":
        options.finalGames = positiveInteger(text, flag, 10_000);
        break;
      case "--samples":
        options.samples = positiveInteger(text, flag, 16);
        break;
      case "--max-moves":
        options.maxMoves = positiveInteger(text, flag, 10_000);
        break;
      case "--tuner-seed":
        options.tunerSeed = parseSeed(text, flag);
        break;
      case "--policy-seed":
        options.policySeed = parseSeed(text, flag);
        break;
      case "--output":
        options.output = text;
        break;
      default:
        throw new Error(`unknown option ${flag}`);
    }
  }
  if (options.elites >= options.population) {
    throw new Error("--elites must be smaller than --population");
  }
  return options;
}

function helpText() {
  return `Drop7 gray-throughput program search

Options:
  --generations <n>  CEM generations (default 12)
  --population <n>   candidates per generation (default 32)
  --elites <n>       elites per generation (default 8)
  --games <n>        0x1d70 training games (default 24)
  --final-games <n>  full evaluation games (default 64)
  --samples <n>      seed-blind reveal samples (default 2)
  --max-moves <n>    censoring cap (default 1000)
  --calibrate        evaluate final champion on 0x5d70 seeds
  --output <path>    checkpoint path
`;
}

function positiveInteger(text: string, flag: string, maximum: number) {
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new Error(`${flag} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function parseSeed(text: string, flag: string) {
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error(`${flag} must be a uint32`);
  }
  return value >>> 0;
}

function omitResults(summary: Summary) {
  return {
    objective: summary.objective,
    meanScore: summary.meanScore,
    medianScore: summary.medianScore,
    meanMoves: summary.meanMoves,
    censored: summary.censored,
  };
}

function clip(value: number) {
  return Math.max(
    MINIMUM_LOG_MULTIPLIER,
    Math.min(MAXIMUM_LOG_MULTIPLIER, value),
  );
}

function gaussian(random: () => number) {
  const first = Math.max(Number.EPSILON, random());
  return Math.sqrt(-2 * Math.log(first)) * Math.cos(2 * Math.PI * random());
}

function consecutiveSeeds(start: number, count: number) {
  if (start + count - 1 > 0xffff_ffff) throw new Error("seed range overflow");
  return Array.from({ length: count }, (_, offset) => start + offset);
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
  return `mean ${Math.round(summary.meanScore).toLocaleString()} · median ${Math.round(summary.medianScore).toLocaleString()} · moves ${summary.meanMoves.toFixed(1)} · censored ${summary.censored}/${summary.results.length} · objective ${summary.objective.toFixed(5)}`;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

function seedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}

function signed(value: number) {
  const rounded = Math.round(value);
  return `${rounded >= 0 ? "+" : ""}${rounded.toLocaleString()}`;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options === null) process.stdout.write(helpText());
  else await tune(options);
}

void main();
