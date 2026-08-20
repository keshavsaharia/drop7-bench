import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import {
  extractVirtualIgnitionFeatures,
  scoreVirtualIgnitionFeatures,
} from "../../../src/core/typescript/virtual-ignition.ts";
import {
  evaluateFairPosition,
  initialFairPolicyWeights,
  type FairPolicyWeights,
} from "../../fair-expectimax/fair-policy/tune.ts";

const TRAINING_SEED_START = 0x1d70_0000;
const REVEAL_DOMAIN = 0x5245_564c;
const POLICY_SEED = 0xd707_5eed;
const MAX_EVALUATOR_CACHE = 20_000;

interface Options {
  depth: number;
  samples: number;
  scenarios: number;
  maxWork: number;
  maxCache: number;
  maxMoves: number;
}

interface GameResult {
  seed: number;
  scale: number;
  score: number;
  moves: number;
  censored: boolean;
  work: number;
  incomplete: number;
  evaluatorCalls: number;
  evaluatorHits: number;
  elapsedMs: number;
}

const TUNED_FAIR_WEIGHTS: FairPolicyWeights = {
  ...initialFairPolicyWeights(),
  directPotential: 1_600,
  latentChainPotential: 700,
  roughness: 0,
  revealedCoverValue: 300,
  heightLoad: -20,
};

function runGame(
  seed: number,
  scale: number,
  options: Options,
): GameResult {
  const startedAt = performance.now();
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let work = 0;
  let incomplete = 0;
  let evaluatorCalls = 0;
  let evaluatorHits = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const cache = new Map<string, number>();
    const evaluator = (position: GameState) => {
      evaluatorCalls += 1;
      const baseline = evaluateFairPosition(position, TUNED_FAIR_WEIGHTS);
      if (scale === 0 || position.gameOver) return baseline;
      const key = `${position.board.join("")}:${position.movesRemaining}:${position.level}`;
      const previous = cache.get(key);
      if (previous !== undefined) {
        evaluatorHits += 1;
        return baseline + scale * previous;
      }
      const residual = scoreVirtualIgnitionFeatures(
        extractVirtualIgnitionFeatures(position, {
          scenarios: options.scenarios,
        }),
      );
      if (cache.size >= MAX_EVALUATOR_CACHE) cache.clear();
      cache.set(key, residual);
      return baseline + scale * residual;
    };
    const decision = evaluateSparseExpectimaxMoves(state, {
      maxDepth: options.depth,
      chanceSamples: options.samples,
      maxWork: options.maxWork,
      maxCacheEntries: options.maxCache,
      seed: POLICY_SEED,
      terminalUtility: -2_500_000,
      evaluator,
    });
    if (decision.bestColumn === null) {
      throw new Error("virtual-ignition sparse search found no live move");
    }
    work += decision.work;
    if (!decision.complete) incomplete += 1;
    const move = playMove(
      state,
      decision.bestColumn,
      seededRandom(
        mix32(
          seed ^
            Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error("virtual-ignition search chose an illegal move");
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }
  return {
    seed,
    scale,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    work,
    incomplete,
    evaluatorCalls,
    evaluatorHits,
    elapsedMs: performance.now() - startedAt,
  };
}

const seedStart = uint32("--seed", TRAINING_SEED_START);
const games = integer("--games", 4);
if (seedStart < TRAINING_SEED_START || seedStart + games - 1 > 0x1d70_ffff) {
  throw new Error("pilot seeds must remain inside 0x1d70xxxx");
}
const options: Options = {
  depth: integer("--depth", 3),
  samples: integer("--samples", 5),
  scenarios: integer("--scenarios", 3),
  maxWork: integer("--max-work", 1_000_000),
  maxCache: integer("--max-cache", 40_000),
  maxMoves: integer("--max-moves", 1_000),
};
if (options.depth !== 3) throw new Error("this pilot is fixed to depth 3");
const scales = scaleList("--scales", [0, 0.05, 0.1, 0.25, 0.5]);
const details = process.argv.includes("--details");
const byScale = new Map<number, readonly GameResult[]>();

for (const scale of scales) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame(seedStart + offset, scale, options),
  );
  byScale.set(scale, results);
  const summary = summarize(results);
  process.stdout.write(
    `scale ${scale.toFixed(3).padStart(6)} · mean ${Math.round(summary.meanScore).toLocaleString()} · median ${Math.round(summary.medianScore).toLocaleString()} · moves ${summary.meanMoves.toFixed(1)} · censored ${summary.censored}/${games} · work/move ${Math.round(summary.workPerMove).toLocaleString()} · incomplete ${summary.incomplete} · evaluator hit ${(summary.hitRate * 100).toFixed(1)}% · runtime ${(summary.elapsedMs / 1_000).toFixed(1)}s\n`,
  );
  if (details) process.stdout.write(`DETAIL ${JSON.stringify({ scale, results })}\n`);
}

const baseline = byScale.get(scales[0]);
if (baseline) {
  for (const scale of scales.slice(1)) {
    const candidate = byScale.get(scale)!;
    const deltas = candidate.map(
      (result, index) => result.score - baseline[index].score,
    );
    const interval = bootstrap(deltas, 10_000);
    process.stdout.write(
      `scale ${scale.toFixed(3).padStart(6)} vs ${scales[0].toFixed(3)} ${signed(mean(deltas))} · median ${signed(median(deltas))} · W/T/L ${deltas.filter((value) => value > 0).length}/${deltas.filter((value) => value === 0).length}/${deltas.filter((value) => value < 0).length} · bootstrap95 [${signed(interval[0])}, ${signed(interval[1])}]\n`,
    );
  }
}

function summarize(results: readonly GameResult[]) {
  const totalMoves = results.reduce((sum, result) => sum + result.moves, 0);
  const calls = results.reduce((sum, result) => sum + result.evaluatorCalls, 0);
  return {
    meanScore: mean(results.map((result) => result.score)),
    medianScore: median(results.map((result) => result.score)),
    meanMoves: mean(results.map((result) => result.moves)),
    censored: results.filter((result) => result.censored).length,
    workPerMove:
      results.reduce((sum, result) => sum + result.work, 0) /
      Math.max(1, totalMoves),
    incomplete: results.reduce((sum, result) => sum + result.incomplete, 0),
    hitRate:
      results.reduce((sum, result) => sum + result.evaluatorHits, 0) /
      Math.max(1, calls),
    elapsedMs: results.reduce((sum, result) => sum + result.elapsedMs, 0),
  };
}

function bootstrap(values: readonly number[], samples: number) {
  const random = seededRandom(0x7669_626f);
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

function scaleList(flag: string, fallback: readonly number[]) {
  const index = process.argv.indexOf(flag);
  if (index < 0) return [...fallback];
  const values = process.argv[index + 1]?.split(",").map(Number) ?? [];
  if (
    values.length === 0 ||
    values.some((value) => !Number.isFinite(value) || value < 0)
  ) {
    throw new Error(`${flag} must be comma-separated non-negative numbers`);
  }
  return [...new Set(values)];
}

function integer(flag: string, fallback: number) {
  const index = process.argv.indexOf(flag);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new Error(`${flag} must be a positive integer`);
  }
  return value;
}

function uint32(flag: string, fallback: number) {
  const index = process.argv.indexOf(flag);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error(`${flag} must be a uint32`);
  }
  return value >>> 0;
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

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}
