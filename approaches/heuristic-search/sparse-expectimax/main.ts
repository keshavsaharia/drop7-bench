import { readFileSync } from "node:fs";
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
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import { evaluateTunnelingAction } from "../../../src/core/typescript/tunneling-heuristic.ts";
import {
  evaluateFairPosition,
  initialFairPolicyWeights,
  type FairPolicyWeights,
} from "../../fair-expectimax/fair-policy/tune.ts";

interface Arguments {
  seed: number;
  games: number;
  depth: number;
  samples: number;
  maxWork: number;
  maxCacheEntries: number;
  terminalUtility: number;
  maxMoves: number;
  fairWeights?: FairPolicyWeights;
  phaseSafety: boolean;
  dangerRollouts: number;
  dangerHorizon: number;
  dangerHeight: number;
  dangerRisk: number;
  tunnelingActionScale: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  maxChain: number;
  clears: number;
  gameOver: boolean;
  work: number;
  meanDepth: number;
  incomplete: number;
  dangerDecisions: number;
}

const REVEAL_DOMAIN = 0x5245_564c;
const POLICY_SEED = 0xd707_5eed;

export function runSparseGame(options: Arguments, seed: number): GameResult {
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
  let work = 0;
  let depth = 0;
  let incomplete = 0;
  let dangerDecisions = 0;
  const evaluator = options.phaseSafety
    ? createPhaseHorizonEvaluator({
        weights: {
          ...DEFAULT_PHASE_HORIZON_WEIGHTS,
          projectedOccupancyDebt:
            DEFAULT_PHASE_HORIZON_WEIGHTS.projectedOccupancyDebt * 2,
          residualCoverDebt:
            DEFAULT_PHASE_HORIZON_WEIGHTS.residualCoverDebt * 2,
          coverAltitudeDebt:
            DEFAULT_PHASE_HORIZON_WEIGHTS.coverAltitudeDebt * 2,
          imminentCoverAltitudeDebt:
            DEFAULT_PHASE_HORIZON_WEIGHTS.imminentCoverAltitudeDebt * 2,
          peakHeightRisk:
            DEFAULT_PHASE_HORIZON_WEIGHTS.peakHeightRisk * 2,
          triggerReadiness:
            DEFAULT_PHASE_HORIZON_WEIGHTS.triggerReadiness * 2,
          releaseReadiness:
            DEFAULT_PHASE_HORIZON_WEIGHTS.releaseReadiness * 2,
        },
        maxCacheEntries: options.maxCacheEntries,
      })
    : options.fairWeights
      ? (position: GameState) =>
          evaluateFairPosition(position, options.fairWeights!)
      : (position: GameState) => evaluateHeuristic(position, "combined");

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const maximumHeight = Math.max(...columnHeights(state));
    const useDangerRollout =
      options.dangerRollouts > 0 && maximumHeight >= options.dangerHeight;
    const sparse = useDangerRollout
      ? undefined
      : evaluateSparseExpectimaxMoves(state, {
          maxDepth: options.depth,
          chanceSamples: options.samples,
          maxWork: options.maxWork,
          maxCacheEntries: options.maxCacheEntries,
          seed: POLICY_SEED,
          terminalUtility: options.terminalUtility,
          evaluator,
        });
    const rollout = useDangerRollout
      ? evaluateRolloutMoves(state, {
          rollouts: options.dangerRollouts,
          horizon: options.dangerHorizon,
          continuationSamples: 2,
          riskAversion: options.dangerRisk,
          seed: mix32(POLICY_SEED ^ hashObservableState(state)),
          terminalUtility: options.terminalUtility,
          evaluator,
        })
      : undefined;
    const bestColumn = sparse
      ? selectSparseColumnWithActionBonus(
          state,
          sparse.columns,
          options.samples,
          options.tunnelingActionScale,
          sparse.bestColumn,
        )
      : (rollout?.bestColumn ?? null);
    if (bestColumn === null) {
      throw new Error("Hybrid planner returned no move for a live game");
    }
    if (sparse) {
      work += sparse.work;
      if (options.tunnelingActionScale > 0) {
        work += sparse.columns.length * options.samples;
      }
      depth += sparse.depth;
      if (!sparse.complete) incomplete += 1;
    } else {
      work += rollout!.work;
      depth += options.depth;
      dangerDecisions += 1;
    }

    const revealSeed = mix32(
      seed ^
        Math.imul((state.movesPlayed + 1) >>> 0, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(
      state,
      bestColumn,
      seededRandom(revealSeed),
      { captureAnimation: false },
    );
    if (!move) throw new Error("Sparse expectimax selected an illegal move");
    maxChain = Math.max(maxChain, move.waves.length);
    if (move.clearedBoard) clears += 1;
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
    maxChain,
    clears,
    gameOver: state.gameOver,
    work,
    meanDepth: state.movesPlayed === 0 ? 0 : depth / state.movesPlayed,
    incomplete,
    dangerDecisions,
  };
}

export function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  const results: GameResult[] = [];
  for (let offset = 0; offset < options.games; offset += 1) {
    const result = runSparseGame(options, (options.seed + offset) >>> 0);
    results.push(result);
    process.stderr.write(
      `${offset + 1}/${options.games} seed ${result.seed.toString(16)} ` +
        `${result.score.toLocaleString()} (${result.moves} moves)\n`,
    );
  }
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  process.stdout.write(
    [
      `sparse d${options.depth}/s${options.samples}${options.phaseSafety ? "/phase-safety" : options.fairWeights ? "/fair" : ""}`,
      `mean ${Math.round(mean(scores)).toLocaleString()}`,
      `median ${scores[Math.floor(scores.length / 2)].toLocaleString()}`,
      `moves ${mean(results.map((result) => result.moves)).toFixed(1)}`,
      `depth ${mean(results.map((result) => result.meanDepth)).toFixed(2)}`,
      `chain ${mean(results.map((result) => result.maxChain)).toFixed(2)}`,
      `clears ${mean(results.map((result) => result.clears)).toFixed(2)}`,
      `max ${scores.at(-1)!.toLocaleString()}`,
      `censored ${results.filter((result) => !result.gameOver).length}/${results.length}`,
      `incomplete ${results.reduce((sum, result) => sum + result.incomplete, 0)}`,
      `danger ${results.reduce((sum, result) => sum + result.dangerDecisions, 0)}`,
      `work/move ${Math.round(
        mean(
          results.map((result) =>
            result.moves === 0 ? 0 : result.work / result.moves,
          ),
        ),
      ).toLocaleString()}`,
    ].join(" · ") + "\n",
  );
}

function parseArguments(arguments_: readonly string[]): Arguments {
  const integer = (flag: string, fallback: number, minimum = 1) => {
    const index = arguments_.indexOf(flag);
    const value = index < 0 ? fallback : Number(arguments_[index + 1]);
    if (!Number.isSafeInteger(value) || value < minimum) {
      throw new Error(`${flag} must be an integer of at least ${minimum}`);
    }
    return value;
  };
  const finite = (flag: string, fallback: number) => {
    const index = arguments_.indexOf(flag);
    const value = index < 0 ? fallback : Number(arguments_[index + 1]);
    if (!Number.isFinite(value)) throw new Error(`${flag} must be finite`);
    return value;
  };
  const seed = integer("--seed", 0x1d70_0000, 0);
  if (seed > 0xffff_ffff) throw new Error("--seed must be a uint32");
  const dangerRiskIndex = arguments_.indexOf("--danger-risk");
  const dangerRisk =
    dangerRiskIndex < 0 ? 0.1 : Number(arguments_[dangerRiskIndex + 1]);
  if (!Number.isFinite(dangerRisk) || dangerRisk < 0) {
    throw new Error("--danger-risk must be a non-negative finite number");
  }
  const actionScale = finite(
    "--tunneling-action-scale",
    0,
  );
  if (actionScale < 0) {
    throw new Error("--tunneling-action-scale must be non-negative");
  }
  const phaseSafety = arguments_.includes("--phase-safety");
  let fairWeights: FairPolicyWeights | undefined;
  const modelIndex = arguments_.indexOf("--fair-model");
  if (modelIndex >= 0) {
    const path = arguments_[modelIndex + 1];
    if (!path) throw new Error("--fair-model requires a path");
    const parsed = JSON.parse(readFileSync(path, "utf8")) as {
      champion?: { weights?: FairPolicyWeights };
    };
    if (!parsed.champion?.weights) {
      throw new Error("--fair-model must be a fair-tuner artifact");
    }
    fairWeights = {
      ...initialFairPolicyWeights(),
      ...parsed.champion.weights,
    };
  }
  const setIndex = arguments_.indexOf("--set");
  if (setIndex >= 0) {
    const assignments = arguments_[setIndex + 1];
    if (!assignments) throw new Error("--set requires assignments");
    const known = Object.keys(initialFairPolicyWeights());
    const overrides: Record<string, number> = {};
    for (const assignment of assignments.split(",")) {
      const [name, rawValue] = assignment.split("=");
      const value = Number(rawValue);
      if (!known.includes(name) || !Number.isFinite(value)) {
        throw new Error(`Invalid --set assignment ${assignment}`);
      }
      overrides[name] = value;
    }
    fairWeights = {
      ...(fairWeights ?? initialFairPolicyWeights()),
      ...overrides,
    } as FairPolicyWeights;
  }
  return {
    seed,
    games: integer("--games", 4),
    depth: integer("--depth", 3),
    samples: integer("--samples", 2),
    maxWork: integer("--max-work", 1_000_000),
    maxCacheEntries: integer("--max-cache", 40_000),
    terminalUtility: finite("--terminal-utility", -1_000_000),
    maxMoves: integer("--max-moves", 1_000),
    fairWeights,
    phaseSafety,
    dangerRollouts: integer("--danger-rollouts", 0, 0),
    dangerHorizon: integer("--danger-horizon", 12),
    dangerHeight: integer("--danger-height", 5, 1),
    dangerRisk,
    tunnelingActionScale: actionScale,
  };
}

function selectSparseColumnWithActionBonus(
  state: GameState,
  columns: readonly { column: number; value: number }[],
  samples: number,
  actionScale: number,
  fallback: number | null,
) {
  if (actionScale === 0) return fallback;
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of [3, 2, 4, 1, 5, 0, 6]) {
    const candidate = columns.find((item) => item.column === column);
    if (!candidate) continue;
    let bonus = 0;
    for (let sample = 0; sample < samples; sample += 1) {
      const move = playMove(
        state,
        column,
        seededRandom(
          mix32(
            hashObservableState(state) ^
              Math.imul(column + 1, 0x85eb_ca6b) ^
              Math.imul(sample + 1, 0xc2b2_ae35),
          ),
        ),
        { captureAnimation: true },
      );
      if (move) {
        bonus += evaluateTunnelingAction(
          state,
          column,
          move,
          actionScale,
        );
      }
    }
    const value = candidate.value + bonus / samples;
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function columnHeights(state: GameState) {
  const heights = Array<number>(7).fill(0);
  for (let index = 0; index < state.board.length; index += 1) {
    if (state.board[index] !== 0) heights[index % 7] += 1;
  }
  return heights;
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
  return hash >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runCli(process.argv.slice(2));
}
