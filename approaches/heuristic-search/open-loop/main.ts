import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateRecursivePotential } from "../../../src/core/typescript/recursive-potential.ts";
import {
  MAX_OPEN_LOOP_TIME_MS,
  evaluateRobustOpenLoopBeam,
} from "../../../src/core/typescript/robust-open-loop-beam.ts";
import { evaluateTunnelingState } from "../../../src/core/typescript/tunneling-heuristic.ts";

type Profile = "combined" | "recursive" | "tunneling";

interface LabOptions {
  profile: Profile;
  seed: number;
  plannerSeed: number;
  scenarios: number;
  depth: number;
  beamWidth: number;
  riskAversion: number;
  maxWork: number;
  timeLimitMs: number;
  recursiveScale: number;
  tunnelingScale: number;
  maxMoves: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  maxChain: number;
  reveals: number;
  work: number;
  meanDepth: number;
  incomplete: number;
  gameOver: boolean;
  elapsedMs: number;
}

const REVEAL_DOMAIN = 0x5245564c;

function evaluator(profile: Profile, options: LabOptions) {
  if (profile === "recursive") {
    return (state: GameState) =>
      evaluateRecursivePotential(state, options.recursiveScale);
  }
  if (profile === "tunneling") {
    return (state: GameState) =>
      evaluateTunnelingState(state, options.tunnelingScale);
  }
  return (state: GameState) => evaluateHeuristic(state, "combined");
}

function runGame(options: LabOptions): GameResult {
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
  const evaluate = evaluator(options.profile, options);
  let maxChain = 0;
  let reveals = 0;
  let work = 0;
  let completedDepthTotal = 0;
  let incomplete = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const result = evaluateRobustOpenLoopBeam(state, {
      scenarios: options.scenarios,
      depth: options.depth,
      beamWidth: options.beamWidth,
      riskAversion: options.riskAversion,
      maxWork: options.maxWork,
      timeLimitMs: options.timeLimitMs,
      seed: options.plannerSeed,
      evaluator: evaluate,
    });
    if (result.bestColumn === null) {
      throw new Error("Open-loop planner returned no move for a live game");
    }
    work += result.work.total;
    completedDepthTotal += result.completedDepth;
    if (!result.complete) incomplete += 1;

    const move = playMove(
      state,
      result.bestColumn,
      seededRandom(
        mix32(
          options.seed ^
            Math.imul(state.movesPlayed + 1, 0x85ebca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`Illegal plan move ${result.bestColumn}`);
    maxChain = Math.max(maxChain, move.waves.length);
    for (const wave of move.waves) reveals += wave.revealed;
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(options.seed, move.state.movesPlayed),
        };
  }

  return {
    seed: options.seed,
    score: state.score,
    moves: state.movesPlayed,
    maxChain,
    reveals,
    work,
    meanDepth:
      state.movesPlayed === 0 ? 0 : completedDepthTotal / state.movesPlayed,
    incomplete,
    gameOver: state.gameOver,
    elapsedMs: Math.max(0, performance.now() - startedAt),
  };
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846ca68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
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

const seedStart = uint32("--seed", 0x1d70_0000);
const games = integer("--games", 4);
if (seedStart + games - 1 > 0xffff_ffff) {
  throw new Error("seed range exceeds uint32");
}
const profileText = process.argv.includes("--profiles")
  ? process.argv[process.argv.indexOf("--profiles") + 1]
  : "combined,recursive,tunneling";
const profiles = [...new Set(profileText.split(","))] as Profile[];
if (
  profiles.length === 0 ||
  profiles.some(
    (profile) =>
      profile !== "combined" &&
      profile !== "recursive" &&
      profile !== "tunneling",
  )
) {
  throw new Error("--profiles accepts combined, recursive, and tunneling");
}
const shared = {
  plannerSeed: uint32("--planner-seed", 0xd707_5eed),
  scenarios: integer("--scenarios", 16),
  depth: integer("--depth", 4),
  beamWidth: integer("--beam-width", 32),
  riskAversion: finite("--risk-aversion", 0),
  maxWork: integer("--max-work", 2_000_000),
  timeLimitMs: integer("--time-limit-ms", MAX_OPEN_LOOP_TIME_MS),
  recursiveScale: finite("--recursive-scale", 1),
  tunnelingScale: finite("--tunneling-scale", 0.25),
  maxMoves: integer("--max-moves", 500),
};
const details = process.argv.includes("--details");
const resultsByProfile = new Map<Profile, readonly GameResult[]>();

for (const profile of profiles) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame({
      ...shared,
      profile,
      seed: seedStart + offset,
    }),
  );
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  const totalMoves = results.reduce((sum, result) => sum + result.moves, 0);
  const elapsedMs = results.reduce(
    (sum, result) => sum + result.elapsedMs,
    0,
  );
  resultsByProfile.set(profile, results);
  process.stdout.write(
    `${profile.padEnd(10)} mean ${Math.round(mean(scores)).toLocaleString()} · median ${scores[Math.floor(scores.length / 2)].toLocaleString()} · moves ${mean(results.map((result) => result.moves)).toFixed(1)} · chain ${mean(results.map((result) => result.maxChain)).toFixed(2)} · reveals ${mean(results.map((result) => result.reveals)).toFixed(1)} · depth ${mean(results.map((result) => result.meanDepth)).toFixed(2)} · work/move ${Math.round(results.reduce((sum, result) => sum + result.work, 0) / Math.max(1, totalMoves)).toLocaleString()} · runtime ${(elapsedMs / 1_000).toFixed(1)}s · incomplete ${results.reduce((sum, result) => sum + result.incomplete, 0)}/${totalMoves} · d=${shared.depth} s=${shared.scenarios} w=${shared.beamWidth} risk=${shared.riskAversion} maxWork=${shared.maxWork}\n`,
  );
  if (details) {
    process.stdout.write(`details ${JSON.stringify({ profile, results })}\n`);
  }
}

const baseline = resultsByProfile.get("combined");
if (baseline) {
  for (const profile of profiles) {
    if (profile === "combined") continue;
    const candidate = resultsByProfile.get(profile);
    if (!candidate) continue;
    const deltas = baseline.map(
      (result, index) => candidate[index].score - result.score,
    );
    const wins = deltas.filter((delta) => delta > 0).length;
    const ties = deltas.filter((delta) => delta === 0).length;
    const losses = deltas.length - wins - ties;
    const interval = bootstrapMeanInterval(deltas, 10_000);
    process.stdout.write(
      `${profile.padEnd(10)} paired ${signed(average(deltas), 0)} · median ${signed(median(deltas), 0)} · W/T/L ${wins}/${ties}/${losses} · bootstrap95 [${signed(interval[0], 0)}, ${signed(interval[1], 0)}]\n`,
    );
  }
}

function average(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function median(values: readonly number[]) {
  const sorted = [...values].sort((first, second) => first - second);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function bootstrapMeanInterval(
  values: readonly number[],
  samples: number,
): readonly [number, number] {
  let randomState = 0x0b07_5eed;
  const random = () => {
    randomState =
      (Math.imul(randomState, 1_664_525) + 1_013_904_223) >>> 0;
    return randomState / 4_294_967_296;
  };
  const means = Array<number>(samples);
  for (let sample = 0; sample < samples; sample += 1) {
    let total = 0;
    for (let index = 0; index < values.length; index += 1) {
      total += values[Math.floor(random() * values.length)];
    }
    means[sample] = total / values.length;
  }
  means.sort((first, second) => first - second);
  return [
    means[Math.floor(samples * 0.025)],
    means[Math.floor(samples * 0.975)],
  ];
}

function signed(value: number, digits: number) {
  return `${value >= 0 ? "+" : ""}${value.toLocaleString(undefined, {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  })}`;
}
