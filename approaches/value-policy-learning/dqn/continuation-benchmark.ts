import { pathToFileURL } from "node:url";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import {
  DEFAULT_PHASE_HORIZON_WEIGHTS,
  createPhaseHorizonEvaluator,
} from "../../../src/core/typescript/phase-horizon-evaluator.ts";
import { evaluateRolloutMoves } from "../../../src/core/typescript/rollout-solver.ts";
import type { RolloutEvaluator } from "../../../src/core/typescript/rollout-solver.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import {
  loadDqnCheckpoint,
  type CompiledDqnPolicy,
} from "./train.ts";

const DEFAULT_CHECKPOINT = "/tmp/drop7-dqn-360k.json";
const DEFAULT_SEED = 0x3d70_0000;
const DEFAULT_GAMES = 16;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_HORIZON = 6;
const DEFAULT_ROLLOUTS = 16;
const DEFAULT_TERMINAL_UTILITY = -1_000_000;
const DEFAULT_Q_POINT_SCALE = 3_200;
const DEFAULT_PRIOR_WINDOW = 5_000;
const PLANNER_SEED = 0xd707_5eed;
const REVEAL_DOMAIN = 0x5245_564c;

type Mode = "rollout" | "direct" | "sparse" | "sparse-q-leaf" | "sparse-q-prior";
type Leaf = "combined" | "phase-center" | "phase-release-double";

interface Arguments {
  checkpoint: string;
  seed: number;
  games: number;
  maxMoves: number;
  horizon: number;
  rollouts: number;
  terminalUtility: number;
  mode: Mode;
  leaf: Leaf;
  sparseDepth: number;
  sparseSamples: number;
  sparseMaxWork: number;
  qPointScale: number;
  priorWindow: number;
  quiet: boolean;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  gameOver: boolean;
  maxChain: number;
  clears: number;
  plannerWork: number;
  incomplete: number;
  elapsedMs: number;
}

interface Summary {
  mode: Mode;
  leaf: Leaf;
  horizon: number;
  rollouts: number;
  games: number;
  seedStart: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanMaxChain: number;
  meanClears: number;
  meanWorkPerMove: number;
  incompleteDecisions: number;
  elapsedMs: number;
  results: readonly GameResult[];
}

export async function runDqnContinuationBenchmark(options: Arguments) {
  const policy = await loadDqnCheckpoint(options.checkpoint, {
    cacheEntries: 32_768,
  });
  const leafEvaluator = createLeafEvaluator(options.leaf);
  const startedAt = performance.now();
  const results: GameResult[] = [];
  for (let offset = 0; offset < options.games; offset += 1) {
    const result = runGame(
      (options.seed + offset) >>> 0,
      policy,
      leafEvaluator,
      options,
    );
    results.push(result);
    if (!options.quiet) {
      process.stderr.write(
        `${offset + 1}/${options.games} ${formatSeed(result.seed)} · ${formatInteger(result.score)} · ${result.moves} moves · ${(result.elapsedMs / 1_000).toFixed(1)}s\n`,
      );
    }
  }
  const summary = summarize(results, options, performance.now() - startedAt);
  process.stdout.write(`RESULT ${JSON.stringify(summary)}\n`);
  return summary;
}

function runGame(
  seed: number,
  policy: CompiledDqnPolicy,
  leafEvaluator: RolloutEvaluator,
  options: Arguments,
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
  let maxChain = 0;
  let clears = 0;
  let plannerWork = 0;
  let incomplete = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const decision = chooseMove(state, policy, leafEvaluator, options);
    plannerWork += decision.work;
    if (!decision.complete) incomplete += 1;
    const revealSeed = mix32(
      seed ^
        Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(state, decision.column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Planner chose illegal column ${decision.column}`);
    if (move.clearedBoard) clears += 1;
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
    gameOver: state.gameOver,
    maxChain,
    clears,
    plannerWork,
    incomplete,
    elapsedMs: performance.now() - startedAt,
  };
}

function chooseMove(
  state: GameState,
  policy: CompiledDqnPolicy,
  leafEvaluator: RolloutEvaluator,
  options: Arguments,
) {
  if (options.mode === "direct") {
    const column = policy.chooseMove(state);
    if (column === null) throw new Error("DQN found no move in a live game");
    return { column, work: 0, complete: true };
  }

  const plannerSeed = mix32(PLANNER_SEED ^ hashObservableState(state));
  if (options.mode === "rollout") {
    const result = evaluateRolloutMoves(state, {
      rollouts: options.rollouts,
      horizon: options.horizon,
      seed: plannerSeed,
      stratifiedSamples: true,
      terminalUtility: options.terminalUtility,
      continuationPolicy: (position) => policy.chooseMove(position),
      evaluator: leafEvaluator,
    });
    if (result.bestColumn === null) {
      throw new Error("Rollout planner found no move in a live game");
    }
    return { column: result.bestColumn, work: result.work, complete: true };
  }

  const evaluator =
    options.mode === "sparse-q-leaf"
      ? (position: GameState) =>
          policy.evaluateState(position) * options.qPointScale
      : (position: GameState) => evaluateHeuristic(position, "combined");
  const sparse = evaluateSparseExpectimaxMoves(state, {
    maxDepth: options.sparseDepth,
    chanceSamples: options.sparseSamples,
    maxWork: options.sparseMaxWork,
    seed: plannerSeed,
    terminalUtility: options.terminalUtility,
    evaluator,
  });
  let column = sparse.bestColumn;
  if (options.mode === "sparse-q-prior") {
    column = selectSparseWithDqnPrior(
      state,
      sparse.columns,
      policy,
      options.priorWindow,
      column,
    );
  }
  if (column === null) throw new Error("Sparse planner found no move");
  return {
    column,
    work: sparse.work,
    complete: sparse.complete,
  };
}

/** Let Q break only decisions inside a small point-valued sparse window. */
function selectSparseWithDqnPrior(
  state: GameState,
  columns: readonly { column: number; value: number }[],
  policy: CompiledDqnPolicy,
  window: number,
  fallback: number | null,
) {
  if (fallback === null) return null;
  const bestSparse = columns.find((candidate) => candidate.column === fallback);
  if (!bestSparse) return fallback;
  const eligible = new Set(
    columns
      .filter((candidate) => bestSparse.value - candidate.value <= window)
      .map((candidate) => candidate.column),
  );
  let selected = fallback;
  let bestQ = Number.NEGATIVE_INFINITY;
  for (const action of policy.evaluateActions(state)) {
    if (eligible.has(action.column) && action.value > bestQ) {
      bestQ = action.value;
      selected = action.column;
    }
  }
  return selected;
}

function summarize(
  results: readonly GameResult[],
  options: Arguments,
  elapsedMs: number,
): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    mode: options.mode,
    leaf: options.leaf,
    horizon: options.horizon,
    rollouts: options.rollouts,
    games: results.length,
    seedStart: options.seed,
    meanScore: mean(scores),
    medianScore: median(scores),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censoredGames: results.filter((result) => !result.gameOver).length,
    meanMaxChain: mean(results.map((result) => result.maxChain)),
    meanClears: mean(results.map((result) => result.clears)),
    meanWorkPerMove: mean(
      results.map((result) =>
        result.moves === 0 ? 0 : result.plannerWork / result.moves,
      ),
    ),
    incompleteDecisions: results.reduce(
      (sum, result) => sum + result.incomplete,
      0,
    ),
    elapsedMs,
    results,
  };
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const value = (flag: string) => {
    const index = arguments_.indexOf(flag);
    return index < 0 ? undefined : arguments_[index + 1];
  };
  const integer = (flag: string, fallback: number, minimum = 1) => {
    const raw = value(flag);
    const parsed = raw === undefined ? fallback : Number(raw);
    if (!Number.isSafeInteger(parsed) || parsed < minimum) {
      throw new Error(`${flag} must be an integer of at least ${minimum}`);
    }
    return parsed;
  };
  const finite = (flag: string, fallback: number) => {
    const raw = value(flag);
    const parsed = raw === undefined ? fallback : Number(raw);
    if (!Number.isFinite(parsed)) throw new Error(`${flag} must be finite`);
    return parsed;
  };
  const mode = (value("--mode") ?? "rollout") as Mode;
  if (!new Set<Mode>(["rollout", "direct", "sparse", "sparse-q-leaf", "sparse-q-prior"]).has(mode)) {
    throw new Error(`Unknown --mode ${mode}`);
  }
  const leaf = (value("--leaf") ?? "combined") as Leaf;
  if (!new Set<Leaf>(["combined", "phase-center", "phase-release-double"]).has(leaf)) {
    throw new Error(`Unknown --leaf ${leaf}`);
  }
  const seed = integer("--seed", DEFAULT_SEED, 0);
  if (seed > 0xffff_ffff) throw new Error("--seed must be a uint32");
  const terminalUtility = finite("--terminal-utility", DEFAULT_TERMINAL_UTILITY);
  const qPointScale = finite("--q-point-scale", DEFAULT_Q_POINT_SCALE);
  const priorWindow = finite("--prior-window", DEFAULT_PRIOR_WINDOW);
  if (qPointScale <= 0) throw new Error("--q-point-scale must be positive");
  if (priorWindow < 0) throw new Error("--prior-window cannot be negative");
  return {
    checkpoint: value("--checkpoint") ?? DEFAULT_CHECKPOINT,
    seed,
    games: integer("--games", DEFAULT_GAMES),
    maxMoves: integer("--max-moves", DEFAULT_MAX_MOVES),
    horizon: integer("--horizon", DEFAULT_HORIZON),
    rollouts: integer("--rollouts", DEFAULT_ROLLOUTS),
    terminalUtility,
    mode,
    leaf,
    sparseDepth: integer("--sparse-depth", 3),
    sparseSamples: integer("--sparse-samples", 5),
    sparseMaxWork: integer("--sparse-max-work", 250_000),
    qPointScale,
    priorWindow,
    quiet: arguments_.includes("--quiet"),
  };
}

function createLeafEvaluator(leaf: Leaf): RolloutEvaluator {
  if (leaf === "combined") {
    return (state) => evaluateHeuristic(state, "combined");
  }
  if (leaf === "phase-center") return createPhaseHorizonEvaluator();
  return createPhaseHorizonEvaluator({
    weights: {
      ...DEFAULT_PHASE_HORIZON_WEIGHTS,
      triggerReadiness:
        DEFAULT_PHASE_HORIZON_WEIGHTS.triggerReadiness * 2,
      releaseReadiness:
        DEFAULT_PHASE_HORIZON_WEIGHTS.releaseReadiness * 2,
    },
  });
}

function hashObservableState(state: GameState) {
  let hash = 0x811c_9dc5;
  for (const cell of state.board) {
    hash ^= cell + 1;
    hash = Math.imul(hash, 0x0100_0193);
  }
  hash ^= state.nextDisc;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.movesRemaining;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.level;
  return hash >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
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

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function formatSeed(value: number) {
  return `0x${value.toString(16).padStart(8, "0")}`;
}

export async function runCli(arguments_: readonly string[]) {
  await runDqnContinuationBenchmark(parseArguments(arguments_));
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await runCli(process.argv.slice(2));
}
