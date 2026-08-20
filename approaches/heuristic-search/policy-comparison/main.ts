import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type Board,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import {
  evaluateHeuristic,
  type HeuristicProfileName,
} from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateRolloutMoves } from "../../../src/core/typescript/rollout-solver.ts";
import { evaluateSampledBeamMoves } from "../../../src/core/typescript/sampled-beam-solver.ts";
import { evaluateRecursivePotential } from "../../../src/core/typescript/recursive-potential.ts";
import {
  compileLearnedEvaluatorWeights,
  evaluateLearnedPosition,
  type CompiledLearnedEvaluatorWeights,
} from "../../../src/core/typescript/learned-evaluator.ts";

type LabProfile =
  | HeuristicProfileName
  | "fertile"
  | "learned"
  | "recursive";

let learnedModel: CompiledLearnedEvaluatorWeights | undefined;
let recursivePotentialScale = 1;

interface LabOptions {
  profile: LabProfile;
  seed: number;
  plannerSeed: number;
  samples: number;
  rollouts: number;
  horizon: number;
  continuationSamples: number;
  riskAversion: number;
  beamScenarios: number;
  beamDepth: number;
  beamPolicyDepth: number;
  beamPolicyScenarios: number;
  beamWidth: number;
  beamMaxWork: number;
  terminalUtility: number;
  maxMoves: number;
}

interface LabResult {
  seed: number;
  score: number;
  moves: number;
  maxChain: number;
  clears: number;
  gameOver: boolean;
  plannerWork: number;
  incompleteDecisions: number;
}

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const REVEAL_DOMAIN = 0x5245564c;
const POLICY_DOMAIN = 0x504f4c59;

function evaluateFertileState(state: GameState) {
  if (state.gameOver) return -500_000;

  let coveredHeightRisk = 0;
  let lowNumberHeightRisk = 0;
  const heights = columnHeights(state.board);

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const cell = state.board[row * BOARD_SIZE + column];
      const edgeMultiplier = column === 0 || column === 6 ? 1.65 : 1;
      if (cell === SOLID) {
        coveredHeightRisk += elevation ** 2 * edgeMultiplier;
      } else if (cell === CRACKED) {
        coveredHeightRisk += elevation ** 2 * edgeMultiplier * 0.72;
      } else if (cell === 1 || cell === 2) {
        lowNumberHeightRisk += Math.max(0, elevation - 2) ** 2;
      }
    }
  }

  const maximumHeight = Math.max(...heights);
  const dangerHeight = Math.max(0, maximumHeight - 4);
  let roughness = 0;
  for (let column = 1; column < BOARD_SIZE; column += 1) {
    roughness += Math.abs(heights[column] - heights[column - 1]);
  }

  // Penalize covered discs that are close to rising, with extra weight on the
  // weakly connected edge columns.  Keep these terms isolated in this
  // comparison rather than adding them to the shared heuristic.
  return (
    evaluateHeuristic(state, "combined") -
    coveredHeightRisk * 95 -
    lowNumberHeightRisk * 85 -
    dangerHeight ** 2 * 1_250 -
    roughness * 90
  );
}

function stateValue(state: GameState, profile: LabProfile) {
  if (profile === "learned") {
    if (!learnedModel) throw new Error("--model is required for learned");
    return state.gameOver
      ? -250_000
      : evaluateLearnedPosition(state, learnedModel);
  }
  if (profile === "recursive") {
    return evaluateRecursivePotential(state, recursivePotentialScale);
  }
  return profile === "fertile"
    ? evaluateFertileState(state)
    : evaluateHeuristic(state, profile);
}

function chooseMove(
  state: GameState,
  profile: LabProfile,
  plannerSeed: number,
  samples: number,
  rollouts: number,
  horizon: number,
  continuationSamples: number,
  riskAversion: number,
  beamScenarios: number,
  beamDepth: number,
  beamPolicyDepth: number,
  beamPolicyScenarios: number,
  beamWidth: number,
  beamMaxWork: number,
  terminalUtility: number,
) {
  if (beamScenarios > 0) {
    const result = evaluateSampledBeamMoves(state, {
      scenarios: beamScenarios,
      depth: beamDepth,
      policyDepth: beamPolicyDepth,
      policyScenarios: beamPolicyScenarios,
      beamWidth,
      maxWork: beamMaxWork,
      seed: plannerSeed,
      terminalUtility,
      ...(usesCustomEvaluator(profile)
        ? { evaluator: (position: GameState) => stateValue(position, profile) }
        : { heuristicProfile: profile }),
    });
    if (result.bestColumn === null) {
      throw new Error("No legal sampled-beam move in a live game");
    }
    return {
      column: result.bestColumn,
      work: result.work.total,
      incomplete: !result.complete,
    };
  }

  if (rollouts > 0) {
    const result = evaluateRolloutMoves(state, {
      rollouts,
      horizon,
      continuationSamples,
      riskAversion,
      seed: mix32(
        hashBoard(state.board) ^
          Math.imul(state.movesPlayed + 1, 0x9e3779b9),
      ),
      terminalUtility,
      ...(usesCustomEvaluator(profile)
        ? { evaluator: (position: GameState) => stateValue(position, profile) }
        : { heuristicProfile: profile }),
    });
    if (result.bestColumn === null) {
      throw new Error("No legal rollout move in a live game");
    }
    return { column: result.bestColumn, work: result.work, incomplete: false };
  }

  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  let work = 0;

  for (const column of COLUMN_ORDER) {
    if (!legalColumns(state.board).includes(column)) continue;
    let value = 0;
    for (let sample = 0; sample < samples; sample += 1) {
      const random = seededRandom(
        mix32(
          hashBoard(state.board) ^
            Math.imul(state.movesPlayed + 1, 0x9e3779b9) ^
            Math.imul(column + 1, 0x85ebca6b) ^
            Math.imul(sample + 1, 0xc2b2ae35) ^
            POLICY_DOMAIN,
        ),
      );
      const move = playMove(state, column, random, {
        captureAnimation: false,
      });
      work += 1;
      if (!move) continue;
      value += move.scoreDelta + stateValue(move.state, profile);
      work += 1;
    }
    value /= samples;
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }

  if (bestColumn === null) throw new Error("No legal move in a live game");
  return { column: bestColumn, work, incomplete: false };
}

function usesCustomEvaluator(profile: LabProfile) {
  return (
    profile === "fertile" ||
    profile === "learned" ||
    profile === "recursive"
  );
}

function runGame(options: LabOptions): LabResult {
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
  let clears = 0;
  let plannerWork = 0;
  let incompleteDecisions = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const decision = chooseMove(
      state,
      options.profile,
      options.plannerSeed,
      options.samples,
      options.rollouts,
      options.horizon,
      options.continuationSamples,
      options.riskAversion,
      options.beamScenarios,
      options.beamDepth,
      options.beamPolicyDepth,
      options.beamPolicyScenarios,
      options.beamWidth,
      options.beamMaxWork,
      options.terminalUtility,
    );
    const { column } = decision;
    plannerWork += decision.work;
    if (decision.incomplete) incompleteDecisions += 1;
    const revealSeed = mix32(
      options.seed ^
        Math.imul(state.movesPlayed + 1, 0x85ebca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Illegal policy move ${column}`);
    maxChain = Math.max(maxChain, move.waves.length);
    if (move.clearedBoard) clears += 1;
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
    clears,
    gameOver: state.gameOver,
    plannerWork,
    incompleteDecisions,
  };
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

function hashBoard(board: Board) {
  let hash = 0x811c9dc5;
  for (const cell of board) {
    hash ^= cell + 1;
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed ^= mixed >>> 16;
  mixed = Math.imul(mixed, 0x7feb352d);
  mixed ^= mixed >>> 15;
  mixed = Math.imul(mixed, 0x846ca68b);
  mixed ^= mixed >>> 16;
  return mixed >>> 0;
}

function integerArgument(name: string, fallback: number) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new Error(`${name} must be a positive integer`);
  }
  return value;
}

function finiteArgument(name: string, fallback: number) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isFinite(value)) throw new Error(`${name} must be finite`);
  return value;
}

function uint32Argument(name: string, fallback: number) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (
    !Number.isSafeInteger(value) ||
    value < 0 ||
    value > 0xffff_ffff
  ) {
    throw new Error(`${name} must be a uint32 integer`);
  }
  return value >>> 0;
}

function profileArgument(): LabProfile[] {
  const index = process.argv.indexOf("--profiles");
  const value = index < 0 ? "combined,fertile" : process.argv[index + 1];
  const profiles = value.split(",") as LabProfile[];
  const known = new Set<LabProfile>([
    "legacy",
    "survival",
    "potential",
    "anti-clog",
    "combined",
    "fertile",
    "learned",
    "recursive",
  ]);
  for (const profile of profiles) {
    if (!known.has(profile)) {
      throw new Error(`Unknown lab profile ${profile}`);
    }
  }
  return [...new Set(profiles)];
}

const seedStart = uint32Argument("--seed", 1);
const games = integerArgument("--games", 16);
if (seedStart + games - 1 > 0xffff_ffff) {
  throw new Error("The requested game seed range exceeds uint32");
}
const plannerSeed = uint32Argument("--planner-seed", 0xd707_5eed);
const samples = integerArgument("--samples", 4);
const rollouts = process.argv.includes("--rollouts")
  ? integerArgument("--rollouts", 0)
  : 0;
const horizon = integerArgument("--horizon", 20);
const continuationSamples = integerArgument("--continuation-samples", 1);
const riskAversion = finiteArgument("--risk-aversion", 0);
if (riskAversion < 0) {
  throw new Error("--risk-aversion must be non-negative");
}
const beamScenarios = process.argv.includes("--beam-scenarios")
  ? integerArgument("--beam-scenarios", 0)
  : 0;
const beamDepth = integerArgument("--beam-depth", 10);
const beamPolicyDepth = integerArgument("--beam-policy-depth", 2);
const beamPolicyScenarios = integerArgument("--beam-policy-scenarios", 4);
const beamWidth = integerArgument("--beam-width", 8);
const beamMaxWork = integerArgument("--beam-max-work", 5_000_000);
const terminalUtility = finiteArgument("--terminal-utility", -250_000);
const maxMoves = integerArgument("--max-moves", 1_000);
recursivePotentialScale = finiteArgument("--recursive-scale", 1);
if (recursivePotentialScale < 0) {
  throw new Error("--recursive-scale must be non-negative");
}
const details = process.argv.includes("--details");
if (beamScenarios > 0 && rollouts > 0) {
  throw new Error("Choose either --beam-scenarios or --rollouts, not both");
}
const modelIndex = process.argv.indexOf("--model");
if (modelIndex >= 0) {
  const modelPath = process.argv[modelIndex + 1];
  if (!modelPath) throw new Error("Missing value after --model");
  learnedModel = compileLearnedEvaluatorWeights(
    JSON.parse(readFileSync(modelPath, "utf8")) as unknown,
  );
}

for (const profile of profileArgument()) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame({
      profile,
      seed: seedStart + offset,
      plannerSeed,
      samples,
      rollouts,
      horizon,
      continuationSamples,
      riskAversion,
      beamScenarios,
      beamDepth,
      beamPolicyDepth,
      beamPolicyScenarios,
      beamWidth,
      beamMaxWork,
      terminalUtility,
      maxMoves,
    }),
  );
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  const moves = results.map((result) => result.moves);
  const clears = results.map((result) => result.clears);
  const chains = results.map((result) => result.maxChain);
  const work = results.map((result) =>
    result.moves === 0 ? 0 : result.plannerWork / result.moves,
  );
  const censored = results.filter((result) => !result.gameOver).length;
  const incompleteDecisions = results.reduce(
    (total, result) => total + result.incompleteDecisions,
    0,
  );
  const totalMoves = moves.reduce((total, value) => total + value, 0);
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  process.stdout.write(
    `${profile.padEnd(10)} mean ${Math.round(mean(scores)).toLocaleString()} · median ${scores[Math.floor(scores.length / 2)].toLocaleString()} · moves ${mean(moves).toFixed(1)} · clears ${mean(clears).toFixed(2)} · chain ${mean(chains).toFixed(2)} · max ${scores.at(-1)!.toLocaleString()} · work/move ${Math.round(mean(work)).toLocaleString()} · incomplete ${incompleteDecisions}/${totalMoves} · censored ${censored}/${results.length} · ${plannerDescription()}${profile === "recursive" ? ` recursiveScale=${recursivePotentialScale}` : ""}\n`,
  );
  if (details) {
    process.stdout.write(
      `details ${JSON.stringify({ profile, results })}\n`,
    );
  }
}

function plannerDescription() {
  if (beamScenarios > 0) {
    return `beam scenarios=${beamScenarios} depth=${beamDepth} policyDepth=${beamPolicyDepth} policyScenarios=${beamPolicyScenarios} width=${beamWidth} maxWork=${beamMaxWork} terminal=${terminalUtility} plannerSeed=0x${plannerSeed.toString(16).padStart(8, "0")}`;
  }
  if (rollouts > 0) {
    return `rollout samples=${rollouts} horizon=${horizon} continuationSamples=${continuationSamples} riskAversion=${riskAversion} terminal=${terminalUtility}`;
  }
  return `greedy samples=${samples}`;
}
import { readFileSync } from "node:fs";
