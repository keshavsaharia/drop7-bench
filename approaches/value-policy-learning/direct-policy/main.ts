import { mkdir, rename, writeFile } from "node:fs/promises";
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
  contiguousLineLength,
  createInitialBoard,
  placeDisc,
  playMove,
  seededRandom,
  type Board,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";

/**
 * Experimental direct policy. Planner samples depend only on observable state
 * and a fixed solver seed; the actual headless seed is never passed to it.
 */

const TRAINING_SEED_START = 0x1d70_0000;
const VALIDATION_SEED_START = 0x7d70_0000;
const RESERVED_FINAL_SEED_START = 0xd700_0000;
const DEFAULT_GENERATIONS = 10;
const DEFAULT_POPULATION = 24;
const DEFAULT_ELITES = 6;
const DEFAULT_GAMES = 64;
const DEFAULT_SAMPLES = 2;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_TUNER_SEED = 0xd1ec_2026;
const DEFAULT_POLICY_SEED = 0xd1ec_d707;
const DEFAULT_OUTPUT = "drop7-direct-policy.json";
const TERMINAL_UTILITY = -2_500_000;
const SCORE_TARGET = 1_000_000;
const CEM_RATE = 0.72;
const MINIMUM_STD_FRACTION = 0.07;
const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const POLICY_REVEAL_DOMAIN = 0x4452_564c;
const POLICY_DISC_DOMAIN = 0x4444_4953;
const CANDIDATE_DOMAIN = 0x4443_454d;
const GENERATION_MULTIPLIER = 0x9e37_79b9;
const CANDIDATE_MULTIPLIER = 0xc2b2_ae35;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;

const PARAMETERS = [
  parameter("immediateScore", 1, 0.35, 0.1, 3),
  parameter("clearedDiscs", 80, 140, -500, 1_500),
  parameter("revealedCovers", 500, 350, -500, 3_000),
  parameter("chainDepth", 400, 450, -1_000, 4_000),
  parameter("emptyCells", 70, 100, -400, 800),
  parameter("topLoad", -12, 8, -80, 20),
  parameter("coverEnergy", -100, 70, -600, 100),
  parameter("solidEnergy", -55, 55, -400, 100),
  parameter("edgeCoverEnergy", -90, 75, -600, 100),
  parameter("highestCover", -240, 180, -1_500, 200),
  parameter("lowCaps", -550, 350, -3_000, 300),
  parameter("adjacentLowCaps", -500, 350, -3_000, 300),
  parameter("trenchDepth", 260, 220, -600, 2_000),
  parameter("topTwoCliffs", 180, 180, -600, 1_500),
  parameter("excessCliffs", -120, 140, -1_200, 500),
  parameter("highNumberFoundation", 160, 180, -600, 1_500),
  parameter("highNumberVerticalPotential", 220, 220, -800, 1_800),
  parameter("dangerCoverEnergy", -100, 80, -700, 100),
  parameter("dangerPeak", -180, 140, -1_200, 100),
  parameter("risePressure", -45, 40, -350, 100),
  parameter("adjacentCoverAtLanding", 420, 300, -800, 2_500),
  parameter("edgeCoverAtLanding", 350, 300, -800, 2_500),
  parameter("triggerReadiness", 260, 250, -800, 2_000),
  parameter("highDiscTrenchFit", 300, 280, -800, 2_500),
  parameter("landingHeight", -80, 100, -800, 500),
] as const;

type ParameterName = (typeof PARAMETERS)[number]["name"];
type Weights = Readonly<Record<ParameterName, number>>;

interface Parameter<Name extends string = string> {
  name: Name;
  mean: number;
  standardDeviation: number;
  minimum: number;
  maximum: number;
}

interface Arguments {
  generations: number;
  population: number;
  elites: number;
  trainingGames: number;
  validationGames: number;
  samples: number;
  maxMoves: number;
  tunerSeed: number;
  policySeed: number;
  outputPath: string;
  selfTest: boolean;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  clears: number;
  maxChain: number;
}

interface Summary {
  games: number;
  objective: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanClears: number;
  meanMaxChain: number;
  results: readonly GameResult[];
}

interface Distribution {
  means: number[];
  standardDeviations: number[];
}

interface Candidate {
  vector: number[];
  weights: Weights;
  summary: Summary;
}

interface ActionFeatures {
  values: number[];
  fixedUtility: number;
}

function parameter<Name extends string>(
  name: Name,
  mean: number,
  standardDeviation: number,
  minimum: number,
  maximum: number,
): Parameter<Name> {
  return { name, mean, standardDeviation, minimum, maximum };
}

function initialWeights() {
  return vectorToWeights(PARAMETERS.map((item) => item.mean));
}

function runGame(
  seed: number,
  weights: Weights,
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
  let clears = 0;
  let maxChain = 0;

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = chooseMove(state, weights, samples, policySeed);
    if (column === null) throw new Error("Direct policy found no live move");
    const revealSeed = mix32(
      seed ^
        Math.imul(state.movesPlayed + 1, ACTUAL_MOVE_MULTIPLIER) ^
        ACTUAL_REVEAL_DOMAIN,
    );
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Direct policy chose illegal column ${column}`);
    clears += clearCount(move);
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
    clears,
    maxChain,
  };
}

function chooseMove(
  state: GameState,
  weights: Weights,
  samples: number,
  policySeed: number,
) {
  const canonical = canonicalObservable(state);
  const weightVector = weightsToVector(weights);
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;

  for (const column of columnOrder(canonical.mirrored)) {
    if (state.board[column] !== EMPTY) continue;
    const features = actionFeatures(
      state,
      column,
      samples,
      policySeed,
      canonical.hash,
    );
    const value = features.fixedUtility + dot(features.values, weightVector);
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function actionFeatures(
  state: GameState,
  column: number,
  samples: number,
  policySeed: number,
  observableHash: number,
): ActionFeatures {
  const values = rootActionFeatures(state, column);
  let fixedUtility = 0;

  for (let sample = 0; sample < samples; sample += 1) {
    const reveal = stratifiedSample(
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
    add(values, transitionFeatures(move), 1 / samples);
    if (move.state.gameOver) {
      fixedUtility += TERMINAL_UTILITY / samples;
      continue;
    }
    const nextState: GameState = {
      ...move.state,
      score: 0,
      nextDisc: sampledDisc(
        observableHash,
        policySeed,
        sample,
        samples,
        POLICY_DISC_DOMAIN,
      ),
    };
    add(values, boardFeatures(nextState), 1 / samples);
  }
  return { values, fixedUtility };
}

function transitionFeatures(move: MoveResult) {
  let cleared = 0;
  let revealed = 0;
  for (const wave of move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  const values = zeroVector();
  values[0] = move.scoreDelta;
  values[1] = cleared;
  values[2] = revealed;
  values[3] = Math.max(0, move.waves.length - 1) ** 2;
  return values;
}

function boardFeatures(state: GameState) {
  const values = zeroVector();
  const heights = columnHeights(state.board);
  const maximumHeight = Math.max(...heights);
  const phase = Math.max(
    0,
    Math.min(
      1,
      (maximumHeight - 3) / 3 + (state.movesRemaining <= 2 ? 0.25 : 0),
    ),
  );
  let emptyCells = 0;
  let topLoad = 0;
  let coverEnergy = 0;
  let solidEnergy = 0;
  let edgeCoverEnergy = 0;
  let highestCover = 0;
  let highNumberFoundation = 0;
  let highNumberVerticalPotential = 0;

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = state.board[row * BOARD_SIZE + column];
      if (cell === EMPTY) {
        emptyCells += 1;
        continue;
      }
      topLoad += elevation ** 2;
      if (cell === SOLID || cell === CRACKED) {
        const energy = elevation ** 2 * (cell === SOLID ? 1 : 0.72);
        coverEnergy += energy;
        if (cell === SOLID) solidEnergy += elevation ** 2;
        if (column === 0 || column === BOARD_SIZE - 1) {
          edgeCoverEnergy += energy;
        }
        highestCover = Math.max(highestCover, elevation);
      } else if (cell >= 5 && cell <= 7) {
        const bottomness = (row + 1) / BOARD_SIZE;
        highNumberFoundation += (cell - 4) * bottomness;
        const columnHeight = heights[column];
        if (columnHeight < cell) {
          highNumberVerticalPotential +=
            (cell - 4) / Math.max(1, cell - columnHeight);
        }
      }
    }
  }

  let lowCaps = 0;
  let adjacentLowCaps = 0;
  const capValues = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (heights[column] === 0) continue;
    const topRow = BOARD_SIZE - heights[column];
    const cap = state.board[topRow * BOARD_SIZE + column];
    if (cap === 1 || cap === 2) {
      capValues[column] = cap;
      lowCaps += heights[column] ** 2 * (cap === 1 ? 1.5 : 1);
    }
    if (column > 0 && capValues[column - 1] > 0 && capValues[column] > 0) {
      adjacentLowCaps += Math.min(heights[column - 1], heights[column]) ** 2;
    }
  }

  const cliffs = heights
    .slice(1)
    .map((height, index) => Math.abs(height - heights[index]))
    .sort((first, second) => second - first);
  const topTwoCliffs = (cliffs[0] ?? 0) ** 2 + (cliffs[1] ?? 0) ** 2;
  const excessCliffs = cliffs.slice(2).reduce((sum, depth) => sum + depth ** 2, 0);
  const trenches: number[] = [];
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const neighbors = [
      ...(column > 0 ? [heights[column - 1]] : []),
      ...(column + 1 < BOARD_SIZE ? [heights[column + 1]] : []),
    ];
    const depth = Math.min(...neighbors) - heights[column];
    if (depth > 0) trenches.push(depth);
  }
  trenches.sort((first, second) => second - first);
  const trenchDepth = (trenches[0] ?? 0) ** 2 + (trenches[1] ?? 0) ** 2;

  values[4] = emptyCells;
  values[5] = topLoad;
  values[6] = coverEnergy;
  values[7] = solidEnergy;
  values[8] = edgeCoverEnergy;
  values[9] = highestCover ** 2;
  values[10] = lowCaps;
  values[11] = adjacentLowCaps;
  values[12] = trenchDepth;
  values[13] = topTwoCliffs;
  values[14] = excessCliffs;
  values[15] = highNumberFoundation;
  values[16] = highNumberVerticalPotential;
  values[17] = coverEnergy * phase;
  values[18] = maximumHeight ** 3 * phase;
  values[19] = topLoad / state.movesRemaining;
  return values;
}

function rootActionFeatures(state: GameState, column: number) {
  const values = zeroVector();
  const heights = columnHeights(state.board);
  const landingHeight = heights[column] + 1;
  const landingRow = BOARD_SIZE - landingHeight;
  let adjacentCover = 0;
  let edgeCover = 0;
  for (const [rowDelta, columnDelta] of [
    [-1, 0],
    [1, 0],
    [0, -1],
    [0, 1],
  ] as const) {
    const row = landingRow + rowDelta;
    const neighborColumn = column + columnDelta;
    if (
      row < 0 ||
      row >= BOARD_SIZE ||
      neighborColumn < 0 ||
      neighborColumn >= BOARD_SIZE
    ) {
      continue;
    }
    const cell = state.board[row * BOARD_SIZE + neighborColumn];
    if (cell !== SOLID && cell !== CRACKED) continue;
    const energy = (BOARD_SIZE - row) ** 2 * (cell === SOLID ? 1 : 0.72);
    adjacentCover += energy;
    if (neighborColumn === 0 || neighborColumn === BOARD_SIZE - 1) {
      edgeCover += energy;
    }
  }

  const placed = placeDisc(state.board, column, state.nextDisc);
  const horizontalLength = placed
    ? contiguousLineLength(placed, landingRow, column, "row")
    : 0;
  const triggerDistance = Math.min(
    Math.abs(state.nextDisc - landingHeight),
    Math.abs(state.nextDisc - horizontalLength),
  );
  const leftHeight = column > 0 ? heights[column - 1] : landingHeight;
  const rightHeight =
    column + 1 < BOARD_SIZE ? heights[column + 1] : landingHeight;
  const trenchDepth = Math.max(
    0,
    Math.min(leftHeight, rightHeight) - heights[column],
  );

  values[20] = adjacentCover;
  values[21] = edgeCover;
  values[22] = 1 / (1 + triggerDistance);
  values[23] = state.nextDisc >= 5 ? trenchDepth * (state.nextDisc - 4) : 0;
  values[24] = landingHeight;
  return values;
}

function evaluate(
  weights: Weights,
  seeds: readonly number[],
  samples: number,
  policySeed: number,
  maxMoves: number,
) {
  const results = seeds.map((seed) =>
    runGame(seed, weights, samples, policySeed, maxMoves),
  );
  return summarize(results, maxMoves);
}

function summarize(results: readonly GameResult[], maxMoves: number): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    games: results.length,
    objective: mean(results.map((result) => objective(result, maxMoves))),
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

function objective(result: GameResult, maxMoves: number) {
  return (
    (result.moves / maxMoves) * 0.55 +
    (Math.min(result.score, SCORE_TARGET) / SCORE_TARGET) * 0.25 +
    (result.censored ? 0.15 : 0) +
    (Math.min(result.maxChain, 20) / 20) * 0.05
  );
}

async function tune(options: Arguments) {
  const trainingSeeds = consecutiveSeeds(TRAINING_SEED_START, options.trainingGames);
  const validationSeeds = consecutiveSeeds(
    VALIDATION_SEED_START,
    options.validationGames,
  );
  const baselineWeights = initialWeights();
  const baseline = evaluate(
    baselineWeights,
    trainingSeeds,
    options.samples,
    options.policySeed,
    options.maxMoves,
  );
  let distribution: Distribution = {
    means: PARAMETERS.map((item) => item.mean),
    standardDeviations: PARAMETERS.map((item) => item.standardDeviation),
  };
  let champion: Candidate = {
    vector: weightsToVector(baselineWeights),
    weights: baselineWeights,
    summary: baseline,
  };
  const cache = new Map<string, Summary>();
  cache.set(vectorKey(champion.vector), baseline);

  process.stdout.write(
    `direct phase-aware policy · train ${seedRange(trainingSeeds)} · validate ${seedRange(validationSeeds)} · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  process.stdout.write(`baseline · ${formatSummary(baseline)}\n`);

  for (let generation = 0; generation < options.generations; generation += 1) {
    const vectors = population(
      distribution,
      champion.vector,
      options.population,
      options.tunerSeed,
      generation,
    );
    const candidates = vectors.map((vector): Candidate => {
      const key = vectorKey(vector);
      let summary = cache.get(key);
      if (!summary) {
        summary = evaluate(
          vectorToWeights(vector),
          trainingSeeds,
          options.samples,
          options.policySeed,
          options.maxMoves,
        );
        cache.set(key, summary);
      }
      return { vector, weights: vectorToWeights(vector), summary };
    });
    candidates.sort(compareCandidates);
    const elites = candidates.slice(0, options.elites);
    if (compareCandidates(elites[0], champion) < 0) champion = elites[0];
    distribution = updateDistribution(distribution, elites);
    process.stdout.write(
      `generation ${(generation + 1).toString().padStart(2)} · ${formatSummary(elites[0].summary)} · champion ${champion.summary.objective.toFixed(5)}\n`,
    );
    await writeCheckpoint(options.outputPath, {
      generation: generation + 1,
      options: serializableOptions(options),
      distribution,
      champion: {
        weights: champion.weights,
        training: omitResults(champion.summary),
      },
    });
  }

  const validationBaseline = evaluate(
    baselineWeights,
    validationSeeds,
    options.samples,
    options.policySeed,
    options.maxMoves,
  );
  const validationWinner = evaluate(
    champion.weights,
    validationSeeds,
    options.samples,
    options.policySeed,
    options.maxMoves,
  );
  const deltas = validationWinner.results.map(
    (result, index) => result.score - validationBaseline.results[index].score,
  );
  const validation = {
    baseline: omitResults(validationBaseline),
    winner: omitResults(validationWinner),
    pairedMeanScoreDelta: mean(deltas),
    pairedMeanMoveDelta: mean(
      validationWinner.results.map(
        (result, index) => result.moves - validationBaseline.results[index].moves,
      ),
    ),
    wins: deltas.filter((delta) => delta > 0).length,
    ties: deltas.filter((delta) => delta === 0).length,
    losses: deltas.filter((delta) => delta < 0).length,
  };
  await writeCheckpoint(options.outputPath, {
    generation: options.generations,
    options: serializableOptions(options),
    distribution,
    champion: {
      weights: champion.weights,
      training: omitResults(champion.summary),
    },
    validation,
  });

  process.stdout.write(`validation baseline · ${formatSummary(validationBaseline)}\n`);
  process.stdout.write(`validation winner   · ${formatSummary(validationWinner)}\n`);
  process.stdout.write(
    `paired ${signedInteger(validation.pairedMeanScoreDelta)} points · ${signedNumber(validation.pairedMeanMoveDelta, 1)} moves · W/T/L ${validation.wins}/${validation.ties}/${validation.losses}\n`,
  );
  process.stdout.write(`checkpoint ${resolve(options.outputPath)}\n`);
}

function population(
  distribution: Distribution,
  champion: readonly number[],
  size: number,
  tunerSeed: number,
  generation: number,
) {
  const vectors = [[...champion]];
  const means = clipVector(distribution.means);
  if (vectorKey(means) !== vectorKey(champion)) vectors.push(means);
  while (vectors.length < size) {
    const candidate = vectors.length;
    const random = seededRandom(
      mix32(
        tunerSeed ^
          Math.imul(generation + 1, GENERATION_MULTIPLIER) ^
          Math.imul(candidate + 1, CANDIDATE_MULTIPLIER) ^
          CANDIDATE_DOMAIN,
      ),
    );
    vectors.push(
      PARAMETERS.map((_, index) =>
        clip(
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
  elites: readonly Candidate[],
): Distribution {
  const means = PARAMETERS.map((_, index) =>
    mean(elites.map((elite) => elite.vector[index])),
  );
  const deviations = PARAMETERS.map((item, index) => {
    const variance = mean(
      elites.map((elite) => (elite.vector[index] - means[index]) ** 2),
    );
    return Math.max(
      item.standardDeviation * MINIMUM_STD_FRACTION,
      Math.sqrt(variance),
    );
  });
  return {
    means: PARAMETERS.map((_, index) =>
      clip(
        index,
        previous.means[index] * (1 - CEM_RATE) + means[index] * CEM_RATE,
      ),
    ),
    standardDeviations: PARAMETERS.map((item, index) =>
      Math.max(
        item.standardDeviation * MINIMUM_STD_FRACTION,
        previous.standardDeviations[index] * (1 - CEM_RATE) +
          deviations[index] * CEM_RATE,
      ),
    ),
  };
}

function compareCandidates(first: Candidate, second: Candidate) {
  return (
    second.summary.objective - first.summary.objective ||
    second.summary.meanMoves - first.summary.meanMoves ||
    second.summary.meanScore - first.summary.meanScore ||
    vectorKey(first.vector).localeCompare(vectorKey(second.vector))
  );
}

function canonicalObservable(state: GameState) {
  const mirrored = mirrorIsSmaller(state.board);
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

function mirrorIsSmaller(board: Board) {
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

function stratifiedSample(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
  domain: number,
) {
  const offset = mix32(observableHash ^ policySeed ^ domain) % BOARD_SIZE;
  const stratum = Math.floor(((sample + 0.5) * BOARD_SIZE) / samples);
  const disc = ((offset + stratum) % BOARD_SIZE) + 1;
  return (disc - 0.5) / BOARD_SIZE;
}

function sampledDisc(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
  domain: number,
) {
  return (
    Math.floor(
      stratifiedSample(
        observableHash,
        policySeed,
        sample,
        samples,
        domain,
      ) * BOARD_SIZE,
    ) + 1
  ) as 1 | 2 | 3 | 4 | 5 | 6 | 7;
}

function clearCount(move: MoveResult) {
  const wavePoints = move.waves.reduce((sum, wave) => sum + wave.points, 0);
  const bonus =
    move.scoreDelta - wavePoints - (move.levelAdvanced ? LEVEL_BONUS : 0);
  const clears = bonus / CLEAR_BONUS;
  if (!Number.isInteger(clears) || clears < 0) {
    throw new Error("Could not decompose direct-policy move score");
  }
  return clears;
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

function zeroVector() {
  return Array<number>(PARAMETERS.length).fill(0);
}

function add(target: number[], source: readonly number[], scale: number) {
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

function vectorToWeights(vector: readonly number[]) {
  return Object.fromEntries(
    PARAMETERS.map((item, index) => [item.name, clip(index, vector[index])]),
  ) as Weights;
}

function weightsToVector(weights: Weights) {
  return PARAMETERS.map((item) => weights[item.name]);
}

function clipVector(vector: readonly number[]) {
  return vector.map((value, index) => clip(index, value));
}

function clip(index: number, value: number) {
  const item = PARAMETERS[index];
  return Math.max(item.minimum, Math.min(item.maximum, value));
}

function vectorKey(vector: readonly number[]) {
  return vector.map((value) => value.toPrecision(14)).join(",");
}

function gaussian(random: () => number) {
  const first = Math.max(Number.EPSILON, random());
  return (
    Math.sqrt(-2 * Math.log(first)) *
    Math.cos(2 * Math.PI * random())
  );
}

function consecutiveSeeds(start: number, count: number) {
  if (start + count > RESERVED_FINAL_SEED_START) {
    throw new Error("Seed range overlaps the reserved final range");
  }
  return Array.from({ length: count }, (_, index) => (start + index) >>> 0);
}

function omitResults(summary: Summary) {
  return {
    games: summary.games,
    objective: summary.objective,
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

function serializableOptions(options: Arguments) {
  return {
    generations: options.generations,
    population: options.population,
    elites: options.elites,
    trainingGames: options.trainingGames,
    validationGames: options.validationGames,
    samples: options.samples,
    maxMoves: options.maxMoves,
    tunerSeed: options.tunerSeed,
    policySeed: options.policySeed,
    outputPath: options.outputPath,
  };
}

async function writeCheckpoint(path: string, value: unknown) {
  const absolute = resolve(path);
  await mkdir(dirname(absolute), { recursive: true });
  const temporary = `${absolute}.tmp`;
  await writeFile(temporary, `${JSON.stringify(value, null, 2)}\n`);
  await rename(temporary, absolute);
}

function parseArguments(arguments_: readonly string[]): Arguments | null {
  let generations = DEFAULT_GENERATIONS;
  let population = DEFAULT_POPULATION;
  let elites = DEFAULT_ELITES;
  let trainingGames = DEFAULT_GAMES;
  let validationGames = DEFAULT_GAMES;
  let samples = DEFAULT_SAMPLES;
  let maxMoves = DEFAULT_MAX_MOVES;
  let tunerSeed = DEFAULT_TUNER_SEED;
  let policySeed = DEFAULT_POLICY_SEED;
  let outputPath = DEFAULT_OUTPUT;
  let selfTest = false;

  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--self-test") {
      selfTest = true;
      continue;
    }
    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`Missing value after ${flag}`);
    index += 1;
    switch (flag) {
      case "--generations":
        generations = positiveInteger(value, flag, 1_000);
        break;
      case "--population":
        population = positiveInteger(value, flag, 1_000);
        break;
      case "--elites":
        elites = positiveInteger(value, flag, 1_000);
        break;
      case "--games":
      case "--training-games":
        trainingGames = positiveInteger(value, flag, 10_000);
        break;
      case "--validation-games":
        validationGames = positiveInteger(value, flag, 10_000);
        break;
      case "--samples":
        samples = positiveInteger(value, flag, 32);
        break;
      case "--max-moves":
        maxMoves = positiveInteger(value, flag, 10_000);
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
    samples,
    maxMoves,
    tunerSeed,
    policySeed,
    outputPath,
    selfTest,
  };
}

async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options === null) {
    process.stdout.write(helpText());
    return;
  }
  if (options.selfTest) {
    const settings = {
      weights: initialWeights(),
      samples: 2,
      policySeed: DEFAULT_POLICY_SEED,
    };
    const first = runGame(
      TRAINING_SEED_START,
      settings.weights,
      settings.samples,
      settings.policySeed,
      10,
    );
    const second = runGame(
      TRAINING_SEED_START,
      settings.weights,
      settings.samples,
      settings.policySeed,
      10,
    );
    if (JSON.stringify(first) !== JSON.stringify(second)) {
      throw new Error("Direct policy is not deterministic");
    }
    process.stdout.write(
      `self-test ok · ${formatInteger(first.score)} points · ${first.moves} moves\n`,
    );
    return;
  }
  await tune(options);
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await runCli(process.argv.slice(2));
}

function helpText() {
  return `Drop7 direct phase-aware policy lab

Options:
  --generations <n>      CEM generations (default: ${DEFAULT_GENERATIONS})
  --population <n>       Candidates per generation (default: ${DEFAULT_POPULATION})
  --elites <n>           Distribution elites (default: ${DEFAULT_ELITES})
  --games <n>            Fixed training seeds (default: ${DEFAULT_GAMES})
  --validation-games <n> Fixed validation seeds (default: ${DEFAULT_GAMES})
  --samples <n>          Seed-blind one-ply samples (default: ${DEFAULT_SAMPLES})
  --max-moves <n>        Censoring cap (default: ${DEFAULT_MAX_MOVES})
  --tuner-seed <uint32>  CEM seed
  --policy-seed <uint32> Runtime planner seed
  --output <path>        Atomic checkpoint path
  --self-test            Determinism check
`;
}

function positiveInteger(value: string, flag: string, maximum: number) {
  const number = Number(value);
  if (!Number.isSafeInteger(number) || number < 1 || number > maximum) {
    throw new Error(`${flag} must be an integer between 1 and ${maximum}`);
  }
  return number;
}

function parseSeed(value: string, flag: string) {
  const number = Number(value);
  if (!Number.isSafeInteger(number) || number < 0 || number > 0xffff_ffff) {
    throw new Error(`${flag} must be a uint32 integer`);
  }
  return number >>> 0;
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

function formatSummary(summary: Summary) {
  return [
    `mean ${formatInteger(summary.meanScore)}`,
    `median ${formatInteger(summary.medianScore)}`,
    `moves ${summary.meanMoves.toFixed(1)}`,
    `capped ${summary.censoredGames}/${summary.games}`,
    `clears ${summary.meanClears.toFixed(2)}`,
    `chain ${summary.meanMaxChain.toFixed(2)}`,
    `objective ${summary.objective.toFixed(5)}`,
  ].join(" · ");
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

function seedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}
