import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  MC_RETURN_FEATURE_SIZE,
  MC_RETURN_FORMAT,
  MC_RETURN_VERSION,
  TrainableMcReturnNetwork,
  chooseMcReturnMove,
  encodeMcReturnAction,
  type McReturnNetworkSnapshot,
  type McReturnTrainingSample,
} from "../../../src/core/typescript/mc-return-policy.ts";
import { planOracleMove } from "../../oracle-curriculum/perfect-information-oracle/main.ts";

const TRAINING_SEED_START = 0x1d70_0000;
const ORACLE_SEED_START = 0x1d70_8000;
const SELECTION_SEED_START = 0x1d70_f000;
const CALIBRATION_SEED_START = 0x5d70_0000;
const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const ITERATION_SEED_STRIDE = 0x100;
const REQUIRED_FIVE_MOVE_CLEARS = 12;
const REQUIRED_FIVE_MOVE_REVEALS = 7;
const CLEAR_SURPLUS_VALUE = 0.25;
const REVEAL_SURPLUS_VALUE = 0.35;
const TERMINAL_PENALTY = 12;

interface Arguments {
  bootstrap: string;
  output: string;
  iterations: number;
  episodes: number;
  oracleEpisodes: number;
  oracleCopies: number;
  oracleDepth: number;
  oracleBeam: number;
  selectionGames: number;
  maxMoves: number;
  capacity: number;
  updates: number;
  batchSize: number;
  learningRate: number;
  epsilonStart: number;
  epsilonEnd: number;
  trainerSeed: number;
  calibrate: boolean;
  selfTest: boolean;
}

interface BootstrapArtifact {
  format: string;
  version: number;
  observableOnly: boolean;
  options: {
    policySamples: number;
    policySeed: number;
  };
  network: McReturnNetworkSnapshot;
}

interface TrajectoryStep {
  input: Float64Array;
  cleared: number;
  revealed: number;
}

interface EpisodeResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  cleared: number;
  revealed: number;
  maxChain: number;
  targets: readonly number[];
  trajectory: readonly TrajectoryStep[];
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  cleared: number;
  revealed: number;
  maxChain: number;
}

interface Summary {
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censored: number;
  clearRate: number;
  revealRate: number;
  results: readonly GameResult[];
}

interface CurvePoint {
  iteration: number;
  epsilon: number;
  episodes: number;
  transitions: number;
  replaySize: number;
  replayBytes: number;
  networkBytes: number;
  meanLoss: number;
  collected: Omit<Summary, "results">;
  candidate: Omit<Summary, "results">;
  champion: Omit<Summary, "results">;
  accepted: boolean;
}

class ReturnDataset {
  readonly capacity: number;
  size = 0;
  private cursor = 0;
  private readonly inputs: Float32Array;
  private readonly targets: Float32Array;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.inputs = new Float32Array(capacity * MC_RETURN_FEATURE_SIZE);
    this.targets = new Float32Array(capacity);
  }

  add(input: ArrayLike<number>, target: number, copies = 1) {
    if (input.length !== MC_RETURN_FEATURE_SIZE) {
      throw new Error("return dataset received a malformed action input");
    }
    if (!Number.isFinite(target)) throw new Error("return target is not finite");
    for (let copy = 0; copy < copies; copy += 1) {
      const offset = this.cursor * MC_RETURN_FEATURE_SIZE;
      for (let index = 0; index < MC_RETURN_FEATURE_SIZE; index += 1) {
        this.inputs[offset + index] = input[index];
      }
      this.targets[this.cursor] = target;
      this.cursor = (this.cursor + 1) % this.capacity;
      this.size = Math.min(this.capacity, this.size + 1);
    }
  }

  sampleBatch(random: () => number, size: number): McReturnTrainingSample[] {
    if (this.size === 0) throw new Error("cannot sample an empty return dataset");
    return Array.from({ length: size }, () => {
      const index = Math.floor(random() * this.size);
      const offset = index * MC_RETURN_FEATURE_SIZE;
      return {
        input: this.inputs.subarray(offset, offset + MC_RETURN_FEATURE_SIZE),
        target: this.targets[index],
      };
    });
  }

  byteLength() {
    return this.inputs.byteLength + this.targets.byteLength;
  }
}

/**
 * Label every action with an undiscounted complete-episode return.
 *
 * Each five-move block begins with five units of survival value, then earns or
 * loses value according to throughput surplus relative to the 12 clears and
 * seven reveals required to offset Hardcore's five-move row arrival. That
 * block reward is spread over its actions before a gamma=1 backward return.
 */
export function labelUndiscountedReturns(
  steps: readonly Pick<TrajectoryStep, "cleared" | "revealed">[],
  terminal: boolean,
) {
  const rewards = Array<number>(steps.length).fill(0);
  for (let start = 0; start < steps.length; start += MOVES_PER_LEVEL) {
    const end = Math.min(steps.length, start + MOVES_PER_LEVEL);
    const length = end - start;
    const cleared = steps
      .slice(start, end)
      .reduce((sum, step) => sum + step.cleared, 0);
    const revealed = steps
      .slice(start, end)
      .reduce((sum, step) => sum + step.revealed, 0);
    const targetClears =
      (REQUIRED_FIVE_MOVE_CLEARS * length) / MOVES_PER_LEVEL;
    const targetReveals =
      (REQUIRED_FIVE_MOVE_REVEALS * length) / MOVES_PER_LEVEL;
    let windowReturn =
      length +
      (cleared - targetClears) * CLEAR_SURPLUS_VALUE +
      (revealed - targetReveals) * REVEAL_SURPLUS_VALUE;
    if (terminal && end === steps.length) windowReturn -= TERMINAL_PENALTY;
    const reward = windowReturn / length;
    for (let index = start; index < end; index += 1) rewards[index] = reward;
  }
  const returns = Array<number>(steps.length).fill(0);
  let remaining = 0;
  for (let index = steps.length - 1; index >= 0; index -= 1) {
    remaining += rewards[index];
    returns[index] = remaining;
  }
  return returns;
}

async function train(options: Arguments) {
  const bootstrap = await readBootstrap(options.bootstrap);
  const samples = bootstrap.options.policySamples;
  const policySeed = bootstrap.options.policySeed;
  const baselineNetwork = new TrainableMcReturnNetwork(bootstrap.network);
  let championSnapshot = baselineNetwork.snapshot();
  const dataset = new ReturnDataset(options.capacity);
  const random = seededRandom(options.trainerSeed);
  const selectionSeeds = consecutiveSeeds(
    SELECTION_SEED_START,
    options.selectionGames,
  );
  const baseline = evaluateNetwork(
    baselineNetwork,
    selectionSeeds,
    samples,
    policySeed,
    options.maxMoves,
  );
  const curves: CurvePoint[] = [];
  let champion = baseline;
  let episodesCollected = 0;

  process.stdout.write(
    `MC-return policy iteration · collection ${formatSeed(TRAINING_SEED_START)}+ · selection ${seedRange(selectionSeeds)} · calibration ${formatSeed(CALIBRATION_SEED_START)}+ reserved\n`,
  );
  process.stdout.write(
    `warm start ${resolve(options.bootstrap)} · network ${bootstrap.network.inputSize}→${bootstrap.network.hiddenOne}→${bootstrap.network.hiddenTwo}→1 · replay ${formatBytes(dataset.byteLength())} fixed\n`,
  );
  process.stdout.write(`DQN warm-start · ${formatSummary(baseline)}\n`);

  if (options.oracleEpisodes > 0) {
    const oracleNetwork = new TrainableMcReturnNetwork(championSnapshot);
    const oracleResults: GameResult[] = [];
    for (let offset = 0; offset < options.oracleEpisodes; offset += 1) {
      const episode = collectEpisode(
        ORACLE_SEED_START + offset,
        oracleNetwork,
        samples,
        policySeed,
        options.maxMoves,
        random,
        0,
        { depth: options.oracleDepth, beamWidth: options.oracleBeam },
      );
      addEpisode(dataset, episode, options.oracleCopies);
      oracleResults.push(episode);
      episodesCollected += 1;
      process.stderr.write(
        `oracle ${offset + 1}/${options.oracleEpisodes} · ${formatInteger(episode.score)} · ${episode.moves} moves\n`,
      );
    }
    process.stdout.write(
      `privileged oracle corpus · ${formatSummary(summarize(oracleResults))} · ${dataset.size.toLocaleString()} weighted samples\n`,
    );
  }

  for (let iteration = 0; iteration < options.iterations; iteration += 1) {
    const behavior = new TrainableMcReturnNetwork(championSnapshot);
    const epsilon = annealed(
      options.epsilonStart,
      options.epsilonEnd,
      iteration,
      options.iterations,
    );
    const collectionResults: GameResult[] = [];
    let transitions = 0;
    const seedStart = TRAINING_SEED_START + iteration * ITERATION_SEED_STRIDE;
    for (let offset = 0; offset < options.episodes; offset += 1) {
      const episode = collectEpisode(
        seedStart + offset,
        behavior,
        samples,
        policySeed,
        options.maxMoves,
        random,
        epsilon,
      );
      addEpisode(dataset, episode, 1);
      collectionResults.push(episode);
      transitions += episode.moves;
      episodesCollected += 1;
    }

    const candidateNetwork = new TrainableMcReturnNetwork(championSnapshot);
    let loss = 0;
    for (let update = 0; update < options.updates; update += 1) {
      loss += candidateNetwork.trainBatch(
        dataset.sampleBatch(random, options.batchSize),
        options.learningRate,
      );
    }
    loss /= options.updates;
    const candidate = evaluateNetwork(
      candidateNetwork,
      selectionSeeds,
      samples,
      policySeed,
      options.maxMoves,
    );
    const accepted = compareSummaries(candidate, champion) > 0;
    if (accepted) {
      championSnapshot = candidateNetwork.snapshot();
      champion = candidate;
    }
    const point: CurvePoint = {
      iteration: iteration + 1,
      epsilon,
      episodes: episodesCollected,
      transitions,
      replaySize: dataset.size,
      replayBytes: dataset.byteLength(),
      networkBytes: candidateNetwork.byteLength(),
      meanLoss: loss,
      collected: omitResults(summarize(collectionResults)),
      candidate: omitResults(candidate),
      champion: omitResults(champion),
      accepted,
    };
    curves.push(point);
    process.stdout.write(
      `iteration ${(iteration + 1).toString().padStart(2)} · ε ${epsilon.toFixed(3)} · collect ${Math.round(point.collected.meanScore).toLocaleString()}/${point.collected.meanMoves.toFixed(1)} · candidate ${Math.round(candidate.meanScore).toLocaleString()}/${candidate.meanMoves.toFixed(1)} · champion ${Math.round(champion.meanScore).toLocaleString()}/${champion.meanMoves.toFixed(1)}${accepted ? " ✓" : ""} · loss ${loss.toFixed(4)} · replay ${dataset.size.toLocaleString()}\n`,
    );
    await writeCheckpoint(options.output, {
      options,
      samples,
      policySeed,
      baseline: omitResults(baseline),
      champion: omitResults(champion),
      curves,
      network: championSnapshot,
      datasetBytes: dataset.byteLength(),
    });
  }

  const paired = pairedComparison(baseline.results, champion.results);
  process.stdout.write(`final DQN     · ${formatSummary(baseline)}\n`);
  process.stdout.write(`final MC      · ${formatSummary(champion)}\n`);
  process.stdout.write(`MC vs DQN     · ${formatPaired(paired)}\n`);

  let calibration: Summary | undefined;
  if (options.calibrate && champion.meanScore >= 300_000) {
    const calibrationSeeds = consecutiveSeeds(
      CALIBRATION_SEED_START,
      options.selectionGames,
    );
    calibration = evaluateNetwork(
      new TrainableMcReturnNetwork(championSnapshot),
      calibrationSeeds,
      samples,
      policySeed,
      options.maxMoves,
    );
    process.stdout.write(`calibration MC · ${formatSummary(calibration)}\n`);
  } else if (options.calibrate) {
    process.stdout.write(
      "calibration withheld: MC-return champion did not clear the 300k training gate\n",
    );
  }
  await writeCheckpoint(options.output, {
    options,
    samples,
    policySeed,
    baseline: omitResults(baseline),
    champion: omitResults(champion),
    paired,
    calibration: calibration ? omitResults(calibration) : undefined,
    curves,
    network: championSnapshot,
    datasetBytes: dataset.byteLength(),
  });
  process.stdout.write(`checkpoint ${resolve(options.output)}\n`);
}

function collectEpisode(
  seed: number,
  network: TrainableMcReturnNetwork,
  samples: number,
  policySeed: number,
  maxMoves: number,
  random: () => number,
  epsilon: number,
  oracle?: { depth: number; beamWidth: number },
): EpisodeResult {
  let state = initialState(seed);
  const trajectory: TrajectoryStep[] = [];
  let cleared = 0;
  let revealed = 0;
  let maxChain = 0;
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = oracle
      ? planOracleMove(state, seed, oracle.depth, oracle.beamWidth).column
      : chooseMcReturnMove(state, network, {
          samples,
          policySeed,
          epsilon,
          random,
        });
    if (column === null) throw new Error("episode policy returned no live move");
    // The environment seed is deliberately used only after action selection,
    // except in the explicitly privileged, training-only oracle corpus.
    const input = encodeMcReturnAction(state, column, samples, policySeed);
    const move = playActualMove(state, column, seed);
    if (!move) throw new Error(`episode policy chose illegal column ${column}`);
    const throughput = moveThroughput(move);
    trajectory.push({ input, ...throughput });
    cleared += throughput.cleared;
    revealed += throughput.revealed;
    maxChain = Math.max(maxChain, move.waves.length);
    state = move.state;
  }
  const targets = labelUndiscountedReturns(trajectory, state.gameOver);
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    cleared,
    revealed,
    maxChain,
    targets,
    trajectory,
  };
}

function addEpisode(dataset: ReturnDataset, episode: EpisodeResult, copies: number) {
  if (episode.targets.length !== episode.trajectory.length) {
    throw new Error("trajectory and MC-return labels differ in length");
  }
  for (let index = 0; index < episode.trajectory.length; index += 1) {
    dataset.add(
      episode.trajectory[index].input,
      episode.targets[index],
      copies,
    );
  }
}

function runNetworkGame(
  seed: number,
  network: TrainableMcReturnNetwork,
  samples: number,
  policySeed: number,
  maxMoves: number,
): GameResult {
  let state = initialState(seed);
  let cleared = 0;
  let revealed = 0;
  let maxChain = 0;
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = chooseMcReturnMove(state, network, { samples, policySeed });
    if (column === null) throw new Error("MC-return policy found no live move");
    const move = playActualMove(state, column, seed);
    if (!move) throw new Error(`MC-return policy chose illegal column ${column}`);
    const throughput = moveThroughput(move);
    cleared += throughput.cleared;
    revealed += throughput.revealed;
    maxChain = Math.max(maxChain, move.waves.length);
    state = move.state;
  }
  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    cleared,
    revealed,
    maxChain,
  };
}

function evaluateNetwork(
  network: TrainableMcReturnNetwork,
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

function summarize(results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  const totalMoves = results.reduce((sum, result) => sum + result.moves, 0);
  return {
    meanScore: mean(scores),
    medianScore: median(scores),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censored: results.filter((result) => result.censored).length,
    clearRate:
      results.reduce((sum, result) => sum + result.cleared, 0) /
      Math.max(1, totalMoves),
    revealRate:
      results.reduce((sum, result) => sum + result.revealed, 0) /
      Math.max(1, totalMoves),
    results,
  };
}

function compareSummaries(first: Summary, second: Summary) {
  if (Math.abs(first.meanMoves - second.meanMoves) > 0.25) {
    return first.meanMoves - second.meanMoves;
  }
  return first.meanScore - second.meanScore;
}

function pairedComparison(
  baseline: readonly GameResult[],
  candidate: readonly GameResult[],
) {
  if (baseline.length !== candidate.length) {
    throw new Error("paired policies have different game counts");
  }
  const deltas = candidate.map(
    (result, index) => result.score - baseline[index].score,
  );
  const interval = bootstrap(deltas, 10_000);
  return {
    mean: mean(deltas),
    median: median(deltas),
    wins: deltas.filter((value) => value > 0).length,
    ties: deltas.filter((value) => value === 0).length,
    losses: deltas.filter((value) => value < 0).length,
    interval,
  };
}

function bootstrap(values: readonly number[], samples: number) {
  const random = seededRandom(0x6d63_626f);
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

function playActualMove(state: GameState, column: number, seed: number) {
  const revealSeed = mix32(
    seed ^
      Math.imul(state.movesPlayed + 1, ACTUAL_MOVE_MULTIPLIER) ^
      ACTUAL_REVEAL_DOMAIN,
  );
  const move = playMove(state, column, seededRandom(revealSeed), {
    captureAnimation: false,
  });
  if (!move) return null;
  return {
    ...move,
    state: move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        },
  };
}

function moveThroughput(move: MoveResult) {
  let cleared = 0;
  let revealed = 0;
  for (const wave of move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  return { cleared, revealed };
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

async function readBootstrap(path: string) {
  const parsed = JSON.parse(await readFile(path, "utf8")) as BootstrapArtifact;
  if (
    parsed.format !== "drop7-observable-double-dqn" ||
    parsed.version !== 1 ||
    parsed.observableOnly !== true
  ) {
    throw new Error("bootstrap must be an observable Double-DQN v1 checkpoint");
  }
  if (
    !Number.isSafeInteger(parsed.options.policySamples) ||
    parsed.options.policySamples < 1 ||
    parsed.options.policySamples > 16
  ) {
    throw new Error("bootstrap policySamples is invalid");
  }
  if (
    !Number.isSafeInteger(parsed.options.policySeed) ||
    parsed.options.policySeed < 0 ||
    parsed.options.policySeed > 0xffff_ffff
  ) {
    throw new Error("bootstrap policySeed is invalid");
  }
  // Constructor validates every dimension and parameter before training.
  new TrainableMcReturnNetwork(parsed.network);
  return parsed;
}

async function writeCheckpoint(
  path: string,
  data: {
    options: Arguments;
    samples: number;
    policySeed: number;
    baseline: Omit<Summary, "results">;
    champion: Omit<Summary, "results">;
    curves: readonly CurvePoint[];
    network: McReturnNetworkSnapshot;
    datasetBytes: number;
    paired?: ReturnType<typeof pairedComparison>;
    calibration?: Omit<Summary, "results">;
  },
) {
  const destination = resolve(path);
  await mkdir(dirname(destination), { recursive: true });
  const temporary = `${destination}.tmp`;
  await writeFile(
    temporary,
    `${JSON.stringify(
      {
        format: MC_RETURN_FORMAT,
        version: MC_RETURN_VERSION,
        algorithm: "undiscounted-monte-carlo-return",
        observableOnly: true,
        trainingSeedStart: TRAINING_SEED_START,
        oracleSeedStart: ORACLE_SEED_START,
        selectionSeedStart: SELECTION_SEED_START,
        options: {
          samples: data.samples,
          policySeed: data.policySeed,
          iterations: data.options.iterations,
          episodes: data.options.episodes,
          oracleEpisodes: data.options.oracleEpisodes,
          oracleCopies: data.options.oracleCopies,
          oracleDepth: data.options.oracleDepth,
          oracleBeam: data.options.oracleBeam,
          selectionGames: data.options.selectionGames,
          maxMoves: data.options.maxMoves,
          capacity: data.options.capacity,
          updates: data.options.updates,
          batchSize: data.options.batchSize,
          learningRate: data.options.learningRate,
          epsilonStart: data.options.epsilonStart,
          epsilonEnd: data.options.epsilonEnd,
          trainerSeed: data.options.trainerSeed,
        },
        returnDefinition: {
          discount: 1,
          windowMoves: MOVES_PER_LEVEL,
          requiredClears: REQUIRED_FIVE_MOVE_CLEARS,
          requiredReveals: REQUIRED_FIVE_MOVE_REVEALS,
          clearSurplusValue: CLEAR_SURPLUS_VALUE,
          revealSurplusValue: REVEAL_SURPLUS_VALUE,
          terminalPenalty: TERMINAL_PENALTY,
        },
        baseline: data.baseline,
        champion: data.champion,
        paired: data.paired,
        calibration: data.calibration,
        curves: data.curves,
        memory: {
          replayBytes: data.datasetBytes,
          networkAndOptimizerBytes:
            new TrainableMcReturnNetwork(data.network).byteLength(),
        },
        network: data.network,
      },
      null,
      2,
    )}\n`,
  );
  await rename(temporary, destination);
}

function parseArguments(arguments_: readonly string[]): Arguments | null {
  const options: Arguments = {
    bootstrap: "/tmp/drop7-dqn-360k.json",
    output: "/tmp/drop7-mc-return.json",
    iterations: 8,
    episodes: 64,
    oracleEpisodes: 8,
    oracleCopies: 4,
    oracleDepth: 4,
    oracleBeam: 128,
    selectionGames: 64,
    maxMoves: 1_000,
    capacity: 80_000,
    updates: 1_500,
    batchSize: 32,
    learningRate: 0.00015,
    epsilonStart: 0.18,
    epsilonEnd: 0.04,
    trainerSeed: 0x6d63_2026,
    calibrate: false,
    selfTest: false,
  };
  const integers = new Map<keyof Arguments, number>([
    ["iterations", 100],
    ["episodes", 10_000],
    ["oracleEpisodes", 1_000],
    ["oracleCopies", 100],
    ["oracleDepth", 40],
    ["oracleBeam", 100_000],
    ["selectionGames", 10_000],
    ["maxMoves", 10_000],
    ["capacity", 1_000_000],
    ["updates", 1_000_000],
    ["batchSize", 10_000],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--calibrate") {
      options.calibrate = true;
      continue;
    }
    if (flag === "--self-test") {
      options.selfTest = true;
      continue;
    }
    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`missing value after ${flag}`);
    index += 1;
    if (flag === "--bootstrap") {
      options.bootstrap = value;
      continue;
    }
    if (flag === "--output") {
      options.output = value;
      continue;
    }
    if (flag === "--learning-rate") {
      options.learningRate = finite(value, flag, 1, false);
      continue;
    }
    if (flag === "--epsilon-start") {
      options.epsilonStart = finite(value, flag, 1, true);
      continue;
    }
    if (flag === "--epsilon-end") {
      options.epsilonEnd = finite(value, flag, 1, true);
      continue;
    }
    if (flag === "--trainer-seed") {
      options.trainerSeed = parseSeed(value, flag);
      continue;
    }
    if (flag === "--oracle-episodes") {
      options.oracleEpisodes = nonNegativeInteger(value, flag, 1_000);
      continue;
    }
    const camel = flag
      .slice(2)
      .replace(/-([a-z])/g, (_match, letter: string) => letter.toUpperCase()) as
      keyof Arguments;
    const maximum = integers.get(camel);
    if (maximum === undefined) throw new Error(`unknown option ${flag}`);
    options[camel] = positiveInteger(value, flag, maximum) as never;
  }
  if (options.epsilonEnd > options.epsilonStart) {
    throw new Error("epsilon-end cannot exceed epsilon-start");
  }
  if (options.batchSize > options.capacity) {
    throw new Error("batch-size cannot exceed capacity");
  }
  if (
    TRAINING_SEED_START +
      (options.iterations - 1) * ITERATION_SEED_STRIDE +
      options.episodes -
      1 >=
    ORACLE_SEED_START
  ) {
    throw new Error("collection seeds overlap the oracle seed range");
  }
  if (ORACLE_SEED_START + options.oracleEpisodes - 1 >= SELECTION_SEED_START) {
    throw new Error("oracle seeds overlap the selection seed range");
  }
  if (SELECTION_SEED_START + options.selectionGames - 1 > 0x1d70_ffff) {
    throw new Error("selection seeds must remain inside 0x1d70xxxx");
  }
  return options;
}

function selfTest() {
  const labels = labelUndiscountedReturns(
    Array.from({ length: 10 }, () => ({ cleared: 2.4, revealed: 1.4 })),
    false,
  );
  if (labels.length !== 10 || labels[0] !== 10 || labels[5] !== 5) {
    throw new Error("undiscounted return labeling failed");
  }
  const poor = labelUndiscountedReturns(
    Array.from({ length: 5 }, () => ({ cleared: 0, revealed: 0 })),
    true,
  );
  if (!(poor[0] < 0 && poor[0] < poor[4])) {
    throw new Error("terminal low-throughput episode was not penalized");
  }
  const state = initialState(TRAINING_SEED_START);
  if (legalColumns(state.board).length !== 7) {
    throw new Error("self-test initial board is malformed");
  }
  process.stdout.write("MC-return self-test passed\n");
}

function helpText() {
  return `Drop7 complete-episode Monte-Carlo return policy iteration

Options:
  --bootstrap <path>       observable DQN v1 warm start
  --output <path>          MC-return checkpoint
  --iterations <n>         collection/training iterations (default 8)
  --episodes <n>           epsilon-greedy episodes/iteration (default 64)
  --oracle-episodes <n>    privileged training-only trajectories (default 8)
  --oracle-copies <n>      bounded oracle oversampling (default 4)
  --selection-games <n>    fixed 0x1d70 selection games (default 64)
  --max-moves <n>          complete-episode censoring cap (default 1000)
  --capacity <n>           fixed replay transitions (default 80000)
  --updates <n>            regression batches/iteration (default 1500)
  --batch-size <n>         regression batch size (default 32)
  --learning-rate <n>      Adam learning rate (default 0.00015)
  --epsilon-start <x>      first exploration rate (default 0.18)
  --epsilon-end <x>        final exploration rate (default 0.04)
  --calibrate              use 0x5d70 only after clearing the 300k gate
  --self-test              validate complete-return labeling
`;
}

function omitResults(summary: Summary) {
  return {
    meanScore: summary.meanScore,
    medianScore: summary.medianScore,
    minimumScore: summary.minimumScore,
    maximumScore: summary.maximumScore,
    meanMoves: summary.meanMoves,
    censored: summary.censored,
    clearRate: summary.clearRate,
    revealRate: summary.revealRate,
  };
}

function formatSummary(summary: Summary | Omit<Summary, "results">) {
  const games = "results" in summary ? summary.results.length : undefined;
  return `mean ${Math.round(summary.meanScore).toLocaleString()} · median ${Math.round(summary.medianScore).toLocaleString()} · moves ${summary.meanMoves.toFixed(1)} · censored ${summary.censored}${games === undefined ? "" : `/${games}`} · clear ${summary.clearRate.toFixed(2)} · reveal ${summary.revealRate.toFixed(2)}`;
}

function formatPaired(comparison: ReturnType<typeof pairedComparison>) {
  return `${signed(comparison.mean)} · median ${signed(comparison.median)} · W/T/L ${comparison.wins}/${comparison.ties}/${comparison.losses} · bootstrap95 [${signed(comparison.interval[0])}, ${signed(comparison.interval[1])}]`;
}

function annealed(
  start: number,
  end: number,
  iteration: number,
  iterations: number,
) {
  if (iterations <= 1) return end;
  return start + (end - start) * (iteration / (iterations - 1));
}

function positiveInteger(text: string, flag: string, maximum: number) {
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new Error(`${flag} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function nonNegativeInteger(text: string, flag: string, maximum: number) {
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw new Error(`${flag} must be an integer from 0 to ${maximum}`);
  }
  return value;
}

function finite(
  text: string,
  flag: string,
  maximum: number,
  includeZero: boolean,
) {
  const value = Number(text);
  if (
    !Number.isFinite(value) ||
    (includeZero ? value < 0 : value <= 0) ||
    value > maximum
  ) {
    throw new Error(`${flag} is out of range`);
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

function signed(value: number) {
  const rounded = Math.round(value);
  return `${rounded >= 0 ? "+" : ""}${rounded.toLocaleString()}`;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString();
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

function seedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}

function formatBytes(bytes: number) {
  return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options === null) {
    process.stdout.write(helpText());
    return;
  }
  if (options.selfTest) {
    selfTest();
    return;
  }
  await train(options);
}

void runCli(process.argv.slice(2));
