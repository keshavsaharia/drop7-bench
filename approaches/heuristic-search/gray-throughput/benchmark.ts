import { readFileSync } from "node:fs";

import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  DEFAULT_GRAY_THROUGHPUT_WEIGHTS,
  evaluateGrayThroughputMoves,
  type GrayThroughputWeights,
} from "../../../src/core/typescript/gray-throughput-policy.ts";
import { evaluateGrayRolloutMoves } from "../../../src/core/typescript/gray-throughput-rollout.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateRobustOpenLoopBeam } from "../../../src/core/typescript/robust-open-loop-beam.ts";
import { evaluateMoves } from "../../../src/core/typescript/solver.ts";
import { evaluateTunnelingState } from "../../../src/core/typescript/tunneling-heuristic.ts";

type Profile = "gray" | "rollout" | "combined" | "tunneling";

interface Options {
  profile: Profile;
  seed: number;
  maxMoves: number;
  samples: number;
  continuationSamples: number;
  depth: 1 | 2;
  rolloutScenarios: number;
  rolloutHorizon: number;
  rolloutRisk: number;
  policySeed: number;
  weights: GrayThroughputWeights;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  maxChain: number;
  reveals: number;
  cleared: number;
  meanOccupied: number;
  meanCovers: number;
  meanPeak: number;
  maximumPeak: number;
  work: number;
  elapsedMs: number;
}

const REVEAL_DOMAIN = 0x5245_564c;
const TUNNELING_POLICY_SEED = 0xd707_5eed;

function runGame(options: Options): GameResult {
  const startedAt = performance.now();
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(options.seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let maxChain = 0;
  let reveals = 0;
  let cleared = 0;
  let occupiedTotal = 0;
  let coverTotal = 0;
  let peakTotal = 0;
  let maximumPeak = 0;
  let work = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const decision = chooseMove(state, options);
    if (decision.column === null) {
      throw new Error(`${options.profile} returned no move for a live game`);
    }
    work += decision.work;
    const move = playMove(
      state,
      decision.column,
      seededRandom(
        mix32(
          options.seed ^
            Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`Illegal policy move ${decision.column}`);
    maxChain = Math.max(maxChain, move.waves.length);
    for (const wave of move.waves) {
      reveals += wave.revealed;
      cleared += wave.cleared;
    }
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(options.seed, move.state.movesPlayed),
        };
    const snapshot = boardLoad(state);
    occupiedTotal += snapshot.occupied;
    coverTotal += snapshot.covers;
    peakTotal += snapshot.peak;
    maximumPeak = Math.max(maximumPeak, snapshot.peak);
  }

  return {
    seed: options.seed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    maxChain,
    reveals,
    cleared,
    meanOccupied:
      state.movesPlayed === 0 ? 0 : occupiedTotal / state.movesPlayed,
    meanCovers: state.movesPlayed === 0 ? 0 : coverTotal / state.movesPlayed,
    meanPeak: state.movesPlayed === 0 ? 0 : peakTotal / state.movesPlayed,
    maximumPeak,
    work,
    elapsedMs: Math.max(0, performance.now() - startedAt),
  };
}

function chooseMove(state: GameState, options: Options) {
  if (options.profile === "rollout") {
    const result = evaluateGrayRolloutMoves(state, {
      scenarios: options.rolloutScenarios,
      horizon: options.rolloutHorizon,
      guideSamples: options.samples,
      policySeed: options.policySeed,
      riskAversion: options.rolloutRisk,
      weights: options.weights,
    });
    return { column: result.bestColumn, work: result.work };
  }
  if (options.profile === "gray") {
    const result = evaluateGrayThroughputMoves(state, {
      samples: options.samples,
      continuationSamples: options.continuationSamples,
      depth: options.depth,
      policySeed: options.policySeed,
      weights: options.weights,
    });
    return { column: result.bestColumn, work: result.work };
  }
  if (options.profile === "tunneling") {
    const result = evaluateRobustOpenLoopBeam(state, {
      scenarios: 16,
      depth: 4,
      beamWidth: 32,
      riskAversion: 1,
      maxWork: 2_000_000,
      timeLimitMs: 60_000,
      seed: TUNNELING_POLICY_SEED,
      evaluator: (position) => evaluateTunnelingState(position, 0.25),
    });
    return { column: result.bestColumn, work: result.work.total };
  }
  const result = evaluateMoves(state, {
    maxDepth: 1,
    timeLimitMs: Number.POSITIVE_INFINITY,
    maxWork: 2_000_000,
    heuristicProfile: "combined",
  });
  return { column: result.bestColumn, work: result.work };
}

function boardLoad(state: GameState) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  let occupied = 0;
  let covers = 0;
  for (let index = 0; index < state.board.length; index += 1) {
    const cell = state.board[index];
    if (cell === EMPTY) continue;
    occupied += 1;
    heights[index % BOARD_SIZE] += 1;
    if (cell === SOLID || cell === CRACKED) covers += 1;
  }
  return { occupied, covers, peak: Math.max(...heights) };
}

function buildWeights(scales: {
  height: number;
  cover: number;
  clog: number;
  topology: number;
  throughput: number;
  score: number;
  continuation: number;
}): GrayThroughputWeights {
  const state = { ...DEFAULT_GRAY_THROUGHPUT_WEIGHTS.state };
  for (const key of [
    "aboveBandLoad",
    "peakExcess",
    "meanExcess",
    "risePressure",
  ] as const) {
    state[key] *= scales.height;
  }
  for (const key of ["covers", "solids", "cracked", "highCoverLoad"] as const) {
    state[key] *= scales.cover;
  }
  for (const key of ["lowCaps", "adjacentLowCaps"] as const) {
    state[key] *= scales.clog;
  }
  for (const key of ["exposedCoverTopology", "liveNumberTopology"] as const) {
    state[key] *= scales.topology;
  }
  const transition = { ...DEFAULT_GRAY_THROUGHPUT_WEIGHTS.transition };
  for (const key of [
    "clearSurplus",
    "revealSurplus",
    "crackedCovers",
    "revealedCovers",
    "clearedDiscs",
    "chainDepth",
  ] as const) {
    transition[key] *= scales.throughput;
  }
  transition.score *= scales.score;
  return {
    state,
    transition,
    continuationWeight: scales.continuation,
  };
}

function integer(name: string, fallback: number, minimum = 1) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isSafeInteger(value) || value < minimum) {
    throw new Error(`${name} must be an integer >= ${minimum}`);
  }
  return value;
}

function finite(name: string, fallback: number) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isFinite(value) || value < 0) {
    throw new Error(`${name} must be a non-negative finite number`);
  }
  return value;
}

function uint32(name: string, fallback: number) {
  const value = integer(name, fallback, 0);
  if (value > 0xffff_ffff) throw new Error(`${name} must be a uint32`);
  return value >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

const seedStart = uint32("--seed", 0x1d70_0000);
const games = integer("--games", 64);
if (seedStart + games - 1 > 0xffff_ffff) {
  throw new Error("seed range exceeds uint32");
}
const profileText = process.argv.includes("--profiles")
  ? process.argv[process.argv.indexOf("--profiles") + 1]
  : "gray";
const profiles = [...new Set(profileText.split(","))] as Profile[];
if (
  profiles.length === 0 ||
  profiles.some(
    (profile) =>
      profile !== "gray" &&
      profile !== "rollout" &&
      profile !== "combined" &&
      profile !== "tunneling",
  )
) {
  throw new Error("--profiles accepts gray, rollout, combined, and tunneling");
}
const depthValue = integer("--depth", 2);
if (depthValue !== 1 && depthValue !== 2) {
  throw new Error("--depth must be 1 or 2");
}
const scales = {
  height: finite("--height-scale", 1),
  cover: finite("--cover-scale", 1),
  clog: finite("--clog-scale", 1),
  topology: finite("--topology-scale", 1),
  throughput: finite("--throughput-scale", 1),
  score: finite("--score-scale", 1),
  continuation: finite("--continuation-weight", 0.72),
};
const weightsPath = process.argv.includes("--weights")
  ? process.argv[process.argv.indexOf("--weights") + 1]
  : null;
const weights = weightsPath
  ? checkpointWeights(weightsPath)
  : buildWeights(scales);
const shared = {
  maxMoves: integer("--max-moves", 1_000),
  samples: integer("--samples", 4),
  continuationSamples: integer("--continuation-samples", 2),
  depth: depthValue as 1 | 2,
  rolloutScenarios: integer("--rollout-scenarios", 4),
  rolloutHorizon: integer("--rollout-horizon", 20),
  rolloutRisk: finite("--rollout-risk", 0.35),
  policySeed: uint32("--policy-seed", 0x6772_6179),
  weights,
};
const details = process.argv.includes("--details");
const resultsByProfile = new Map<Profile, readonly GameResult[]>();

for (const profile of profiles) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame({ ...shared, profile, seed: seedStart + offset }),
  );
  resultsByProfile.set(profile, results);
  const scores = results.map((result) => result.score).sort(numberOrder);
  const moves = results.map((result) => result.moves);
  const totalMoves = moves.reduce((sum, value) => sum + value, 0);
  process.stdout.write(
    `${profile.padEnd(10)} mean ${Math.round(mean(scores)).toLocaleString()} · median ${Math.round(median(scores)).toLocaleString()} · moves ${mean(moves).toFixed(1)} · censored ${results.filter((result) => result.censored).length}/${games} · peak ${mean(results.map((result) => result.meanPeak)).toFixed(2)}/${Math.max(...results.map((result) => result.maximumPeak))} · occupied ${mean(results.map((result) => result.meanOccupied)).toFixed(1)} · covers ${mean(results.map((result) => result.meanCovers)).toFixed(1)} · reveals/move ${(results.reduce((sum, result) => sum + result.reveals, 0) / Math.max(1, totalMoves)).toFixed(2)} · clears/move ${(results.reduce((sum, result) => sum + result.cleared, 0) / Math.max(1, totalMoves)).toFixed(2)} · work/move ${Math.round(results.reduce((sum, result) => sum + result.work, 0) / Math.max(1, totalMoves)).toLocaleString()} · runtime ${(results.reduce((sum, result) => sum + result.elapsedMs, 0) / 1_000).toFixed(1)}s\n`,
  );
  if (details) process.stdout.write(`details ${JSON.stringify({ profile, results })}\n`);
}

const reference = resultsByProfile.get(profiles[0]);
if (reference) {
  for (const profile of profiles.slice(1)) {
    const candidate = resultsByProfile.get(profile)!;
    const deltas = candidate.map(
      (result, index) => result.score - reference[index].score,
    );
    const interval = bootstrap(deltas, 10_000);
    process.stdout.write(
      `${profile.padEnd(10)} vs ${profiles[0]} ${signed(mean(deltas))} · median ${signed(median(deltas))} · W/T/L ${deltas.filter((value) => value > 0).length}/${deltas.filter((value) => value === 0).length}/${deltas.filter((value) => value < 0).length} · bootstrap95 [${signed(interval[0])}, ${signed(interval[1])}]\n`,
    );
  }
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

function bootstrap(values: readonly number[], samples: number) {
  let state = 0x6772_6179;
  const random = () => {
    state = (Math.imul(state, 1_664_525) + 1_013_904_223) >>> 0;
    return state / 4_294_967_296;
  };
  const means = Array<number>(samples);
  for (let sample = 0; sample < samples; sample += 1) {
    let total = 0;
    for (let index = 0; index < values.length; index += 1) {
      total += values[Math.floor(random() * values.length)];
    }
    means[sample] = total / values.length;
  }
  means.sort(numberOrder);
  return [means[Math.floor(samples * 0.025)], means[Math.floor(samples * 0.975)]] as const;
}

function signed(value: number) {
  const rounded = Math.round(value);
  return `${rounded >= 0 ? "+" : ""}${rounded.toLocaleString()}`;
}

function checkpointWeights(path: string): GrayThroughputWeights {
  const parsed = JSON.parse(readFileSync(path, "utf8")) as {
    champion?: { weights?: GrayThroughputWeights };
  };
  const weights = parsed.champion?.weights;
  if (!weights) throw new Error(`${path} has no champion.weights`);
  return weights;
}
