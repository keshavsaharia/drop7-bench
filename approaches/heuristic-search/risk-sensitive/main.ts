import { pathToFileURL } from "node:url";

import {
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
import { evaluateRiskSensitiveMoves } from "../../../src/core/typescript/risk-sensitive-planner.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";

/**
 * Compares training-only root CVaR with the reference fair search.
 *
 * Actual game chance uses the headless game seed. Planner chance accepts only
 * a fixed policy seed and the observable GameState, so neither the environment
 * seed nor its future disc/reveal tape can leak into action selection.
 */

const PILOT_SEED_START = 0x3d70_0000;
const PROBE_SEED_START = 0x4d70_0000;
const FORBIDDEN_SEED_START = 0x5d70_0000;
const POLICY_SEED = 0x72a1_5e55;
const REVEAL_DOMAIN = 0x5245_564c;
const DEFAULT_GAMES = 4;
const DEFAULT_CONFIRM_GAMES = 16;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_RISK_WORK = 500_000;
const DEFAULT_BASELINE_WORK = 1_000_000;
const DEFAULT_CACHE_ENTRIES = 40_000;
const DEFAULT_SCENARIOS = 7;
const DEFAULT_CONTINUATION_DEPTH = 2;
const DEFAULT_CHANCE_SAMPLES = 3;
const DEFAULT_TAIL_FRACTION = 0.25;
const DEFAULT_RISK_WEIGHTS = [0, 0.5, 1] as const;
const REQUIRED_MEAN = 300_000;

export const RISK_PHASE_SAFETY_WEIGHTS: PhaseHorizonWeights = {
  ...DEFAULT_PHASE_HORIZON_WEIGHTS,
  projectedOccupancyDebt:
    DEFAULT_PHASE_HORIZON_WEIGHTS.projectedOccupancyDebt * 2,
  residualCoverDebt:
    DEFAULT_PHASE_HORIZON_WEIGHTS.residualCoverDebt * 2,
  coverAltitudeDebt: DEFAULT_PHASE_HORIZON_WEIGHTS.coverAltitudeDebt * 2,
  imminentCoverAltitudeDebt:
    DEFAULT_PHASE_HORIZON_WEIGHTS.imminentCoverAltitudeDebt * 2,
  peakHeightRisk: DEFAULT_PHASE_HORIZON_WEIGHTS.peakHeightRisk * 2,
  triggerReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.triggerReadiness * 2,
  releaseReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.releaseReadiness * 2,
};

interface Arguments {
  games: number;
  confirmGames: number;
  maxMoves: number;
  riskWork: number;
  baselineWork: number;
  maxCacheEntries: number;
  scenarios: number;
  continuationDepth: number;
  chanceSamples: number;
  tailFraction: number;
  riskWeights: number[];
  confirm: boolean;
  selfTest: boolean;
}

interface Policy {
  name: string;
  riskWeight: number | null;
}

interface GameResult {
  policy: string;
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  meanWork: number;
  incompleteSearches: number;
}

interface Summary {
  policy: string;
  riskWeight: number | null;
  games: number;
  meanScore: number;
  p25Score: number;
  medianScore: number;
  minimumScore: number;
  meanMoves: number;
  p25Moves: number;
  minimumMoves: number;
  censoredGames: number;
  meanWork: number;
  incompleteSearches: number;
  results: readonly GameResult[];
}

interface PilotResult {
  pilot: Summary[];
  frozenChampion: Policy | null;
  qualified: boolean;
  confirmation: Summary[] | null;
}

export function runRiskLab(options: Arguments): PilotResult {
  const policies: Policy[] = [
    { name: "phase-sparse-d3-s5", riskWeight: null },
    ...options.riskWeights.map((riskWeight) => ({
      name: `root-cvar-${riskWeight}`,
      riskWeight,
    })),
  ];
  const pilotSeeds = seedRange(PILOT_SEED_START, options.games);
  const pilot = evaluatePolicies(policies, pilotSeeds, options, "pilot");
  printSummary("training pilot", pilot);

  const baseline = pilot.find((item) => item.riskWeight === null)!;
  const riskNeutral = pilot.find((item) => item.riskWeight === 0);
  const champion = [...pilot]
    .filter((item) => item.riskWeight !== null && item.riskWeight > 0)
    .sort(compareTailFirst)[0];
  const lowerTailImproved =
    champion !== undefined &&
    champion.p25Score >= baseline.p25Score * 1.05 &&
    champion.p25Moves >= baseline.p25Moves &&
    champion.minimumMoves >= baseline.minimumMoves &&
    (!riskNeutral ||
      champion.p25Score >= riskNeutral.p25Score * 1.05 ||
      champion.minimumMoves >= riskNeutral.minimumMoves + 5);
  const qualified =
    champion !== undefined &&
    champion.meanScore >= REQUIRED_MEAN &&
    lowerTailImproved;
  const frozenChampion = qualified
    ? policies.find((policy) => policy.name === champion.policy)!
    : null;

  process.stdout.write(
    qualified
      ? `frozen ${frozenChampion!.name}: cleared ${REQUIRED_MEAN.toLocaleString("en-US")} mean and the predeclared lower-tail gate\n`
      : `confirmation skipped: no risk-sensitive policy cleared both ${REQUIRED_MEAN.toLocaleString("en-US")} mean and the predeclared lower-tail gate\n`,
  );
  if (!qualified || !options.confirm) {
    return { pilot, frozenChampion, qualified, confirmation: null };
  }

  const confirmationPolicies = [
    policies[0],
    policies.find((policy) => policy.riskWeight === 0)!,
    frozenChampion!,
  ];
  const confirmation = evaluatePolicies(
    confirmationPolicies,
    seedRange(PROBE_SEED_START, options.confirmGames),
    options,
    "probe confirmation",
  );
  printSummary("frozen probe confirmation", confirmation);
  return { pilot, frozenChampion, qualified, confirmation };
}

function evaluatePolicies(
  policies: readonly Policy[],
  seeds: readonly number[],
  options: Arguments,
  stage: string,
) {
  const results = new Map<string, GameResult[]>();
  for (let seedIndex = 0; seedIndex < seeds.length; seedIndex += 1) {
    for (let offset = 0; offset < policies.length; offset += 1) {
      const policy = policies[(seedIndex + offset) % policies.length];
      const result = runGame(policy, seeds[seedIndex], options);
      const list = results.get(policy.name) ?? [];
      list.push(result);
      results.set(policy.name, list);
      process.stderr.write(
        `${stage} ${policy.name} ${seedIndex + 1}/${seeds.length} ${formatSeed(result.seed)} · ${result.score.toLocaleString("en-US")} · ${result.moves} moves${result.censored ? " capped" : ""} · ${Math.round(result.meanWork).toLocaleString("en-US")} work/move\n`,
      );
    }
  }
  return policies.map((policy) =>
    summarize(policy, results.get(policy.name) ?? []),
  );
}

function runGame(
  policy: Policy,
  gameSeed: number,
  options: Arguments,
): GameResult {
  assertTrainingSeed(gameSeed);
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(gameSeed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let work = 0;
  let incompleteSearches = 0;
  const evaluator = (position: GameState) =>
    evaluatePhaseHorizon(position, RISK_PHASE_SAFETY_WEIGHTS);

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    let column: number | null;
    if (policy.riskWeight === null) {
      const result = evaluateSparseExpectimaxMoves(state, {
        maxDepth: 3,
        chanceSamples: 5,
        maxWork: options.baselineWork,
        maxCacheEntries: options.maxCacheEntries,
        seed: POLICY_SEED,
        evaluator,
        terminalUtility: -1_000_000,
      });
      column = result.bestColumn;
      work += result.work;
      if (!result.complete) incompleteSearches += 1;
    } else {
      const result = evaluateRiskSensitiveMoves(state, {
        scenarios: options.scenarios,
        continuationDepth: options.continuationDepth,
        chanceSamples: options.chanceSamples,
        tailFraction: options.tailFraction,
        riskWeight: policy.riskWeight,
        seed: POLICY_SEED,
        evaluator,
        terminalUtility: -1_000_000,
        maxWork: options.riskWork,
        maxCacheEntries: options.maxCacheEntries,
      });
      column = result.bestColumn;
      work += result.work;
      if (!result.complete) incompleteSearches += 1;
    }
    if (column === null) {
      throw new Error("risk lab found no move for a live game");
    }
    const revealSeed = mix32(
      gameSeed ^
        Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`risk lab selected illegal column ${column}`);
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(gameSeed, move.state.movesPlayed),
        };
  }

  return {
    policy: policy.name,
    seed: gameSeed,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    meanWork: state.movesPlayed === 0 ? 0 : work / state.movesPlayed,
    incompleteSearches,
  };
}

function summarize(policy: Policy, results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  const moves = results.map((result) => result.moves).sort(numberOrder);
  return {
    policy: policy.name,
    riskWeight: policy.riskWeight,
    games: results.length,
    meanScore: mean(scores),
    p25Score: percentile(scores, 0.25),
    medianScore: percentile(scores, 0.5),
    minimumScore: scores[0],
    meanMoves: mean(moves),
    p25Moves: percentile(moves, 0.25),
    minimumMoves: moves[0],
    censoredGames: results.filter((result) => result.censored).length,
    meanWork: mean(results.map((result) => result.meanWork)),
    incompleteSearches: results.reduce(
      (sum, result) => sum + result.incompleteSearches,
      0,
    ),
    results,
  };
}

function printSummary(label: string, summaries: readonly Summary[]) {
  process.stdout.write(`\n${label}\n`);
  for (const summary of summaries) {
    process.stdout.write(
      `${summary.policy.padEnd(20)} mean ${rounded(summary.meanScore)} · p25 ${rounded(summary.p25Score)} · min ${rounded(summary.minimumScore)} · moves p25/min ${summary.p25Moves.toFixed(1)}/${summary.minimumMoves} · work ${rounded(summary.meanWork)} · incomplete ${summary.incompleteSearches}\n`,
    );
  }
}

function compareTailFirst(first: Summary, second: Summary) {
  return (
    second.p25Score - first.p25Score ||
    second.minimumScore - first.minimumScore ||
    second.meanScore - first.meanScore
  );
}

function seedRange(start: number, count: number) {
  const seeds = Array.from(
    { length: count },
    (_, index) => (start + index) >>> 0,
  );
  for (const seed of seeds) assertTrainingSeed(seed);
  return seeds;
}

function assertTrainingSeed(seed: number) {
  if (
    !Number.isSafeInteger(seed) ||
    seed < PILOT_SEED_START ||
    seed >= FORBIDDEN_SEED_START
  ) {
    throw new Error(
      `seed ${formatSeed(seed)} is outside the 0x3d70/0x4d70 training ranges`,
    );
  }
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const options: Arguments = {
    games: DEFAULT_GAMES,
    confirmGames: DEFAULT_CONFIRM_GAMES,
    maxMoves: DEFAULT_MAX_MOVES,
    riskWork: DEFAULT_RISK_WORK,
    baselineWork: DEFAULT_BASELINE_WORK,
    maxCacheEntries: DEFAULT_CACHE_ENTRIES,
    scenarios: DEFAULT_SCENARIOS,
    continuationDepth: DEFAULT_CONTINUATION_DEPTH,
    chanceSamples: DEFAULT_CHANCE_SAMPLES,
    tailFraction: DEFAULT_TAIL_FRACTION,
    riskWeights: [...DEFAULT_RISK_WEIGHTS],
    confirm: false,
    selfTest: false,
  };
  const numeric = new Map<string, keyof Arguments>([
    ["--games", "games"],
    ["--confirm-games", "confirmGames"],
    ["--max-moves", "maxMoves"],
    ["--risk-work", "riskWork"],
    ["--baseline-work", "baselineWork"],
    ["--max-cache", "maxCacheEntries"],
    ["--scenarios", "scenarios"],
    ["--continuation-depth", "continuationDepth"],
    ["--chance-samples", "chanceSamples"],
    ["--tail-fraction", "tailFraction"],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--confirm") {
      options.confirm = true;
      continue;
    }
    if (flag === "--self-test") {
      options.selfTest = true;
      continue;
    }
    if (flag === "--risk-weights") {
      options.riskWeights = requiredValue(arguments_, ++index, flag)
        .split(",")
        .map(Number);
      continue;
    }
    const key = numeric.get(flag);
    if (!key) throw new Error(`unknown argument ${flag}`);
    (options as unknown as Record<string, number>)[key] = Number(
      requiredValue(arguments_, ++index, flag),
    );
  }
  for (const key of [
    "games",
    "confirmGames",
    "maxMoves",
    "riskWork",
    "baselineWork",
    "maxCacheEntries",
    "scenarios",
    "chanceSamples",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  if (
    !Number.isSafeInteger(options.continuationDepth) ||
    options.continuationDepth < 0
  ) {
    throw new Error("continuationDepth must be nonnegative");
  }
  if (
    !Number.isFinite(options.tailFraction) ||
    options.tailFraction <= 0 ||
    options.tailFraction > 1
  ) {
    throw new Error("tailFraction must be greater than 0 and at most 1");
  }
  if (
    options.riskWeights.length === 0 ||
    options.riskWeights.some(
      (weight) => !Number.isFinite(weight) || weight < 0 || weight > 2,
    )
  ) {
    throw new Error("risk weights must be finite values from 0 through 2");
  }
  options.riskWeights = [...new Set(options.riskWeights)];
  return options;
}

export function runSelfTest() {
  const parsed = parseArguments([
    "--games",
    "1",
    "--risk-weights",
    "0,0.5,1",
  ]);
  if (parsed.games !== 1 || parsed.riskWeights.length !== 3) {
    throw new Error("risk lab argument parser self-test failed");
  }
  assertTrainingSeed(PILOT_SEED_START);
  assertTrainingSeed(PROBE_SEED_START);
  for (const seed of [0x1d70_0000, 0x5d70_0000, 0x7d70_0000, 0xd700_0000]) {
    let threw = false;
    try {
      assertTrainingSeed(seed);
    } catch {
      threw = true;
    }
    if (!threw) throw new Error("risk lab reserved-seed guard failed");
  }
  process.stdout.write("drop7 risk-sensitive lab self-test passed\n");
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

function rounded(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function requiredValue(arguments_: readonly string[], index: number, flag: string) {
  const value = arguments_[index];
  if (value === undefined) throw new Error(`${flag} requires a value`);
  return value;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  process.stdout.write(
    `root CVaR d${options.continuationDepth + 1}/s${options.scenarios} inner-s${options.chanceSamples} · pilot ${formatSeed(PILOT_SEED_START)}+ · probe ${formatSeed(PROBE_SEED_START)}+ only after qualification · all seeds >= ${formatSeed(FORBIDDEN_SEED_START)} forbidden\n`,
  );
  runRiskLab(options);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await runCli(process.argv.slice(2));
}
