import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type Board,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateRolloutMoves } from "../../../src/core/typescript/rollout-solver.ts";
import {
  DEFAULT_TUNNELING_ACTION_SCALE,
  DEFAULT_TUNNELING_STATE_SCALE,
  evaluateTunnelingAction,
  evaluateTunnelingState,
} from "../../../src/core/typescript/tunneling-heuristic.ts";

type Profile = "combined" | "tunneling";

interface Options {
  profile: Profile;
  seed: number;
  plannerSeed: number;
  samples: number;
  rollouts: number;
  horizon: number;
  continuationSamples: number;
  stateScale: number;
  actionScale: number;
  maxMoves: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  maxChain: number;
  revealed: number;
  gameOver: boolean;
  work: number;
}

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const POLICY_DOMAIN = 0x504f4c59;
const REVEAL_DOMAIN = 0x5245564c;

function stateValue(state: GameState, profile: Profile, scale: number) {
  return profile === "tunneling"
    ? evaluateTunnelingState(state, scale)
    : evaluateHeuristic(state, "combined");
}

function chooseMove(state: GameState, options: Options) {
  if (options.rollouts > 0) {
    const evaluation = evaluateRolloutMoves(state, {
      rollouts: options.rollouts,
      horizon: options.horizon,
      continuationSamples: options.continuationSamples,
      seed: decisionSeed(state, options.plannerSeed),
      evaluator: (position) =>
        stateValue(position, options.profile, options.stateScale),
    });
    let bestColumn: number | null = null;
    let bestValue = Number.NEGATIVE_INFINITY;
    for (const column of COLUMN_ORDER) {
      const candidate = evaluation.columns.find(
        (item) => item.column === column,
      );
      if (!candidate) continue;
      const value =
        candidate.mean +
        averageRootActionBonus(state, column, options);
      if (value > bestValue) {
        bestValue = value;
        bestColumn = column;
      }
    }
    if (bestColumn === null) throw new Error("No rollout move in live game");
    const actionWork =
      options.profile === "tunneling" && options.actionScale > 0
        ? evaluation.columns.length * options.rollouts
        : 0;
    return { column: bestColumn, work: evaluation.work + actionWork };
  }

  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  let work = 0;
  for (const column of COLUMN_ORDER) {
    if (!legalColumns(state.board).includes(column)) continue;
    let total = 0;
    for (let sample = 0; sample < options.samples; sample += 1) {
      const move = playMove(
        state,
        column,
        seededRandom(candidateRevealSeed(state, column, sample)),
        {
          captureAnimation:
            options.profile === "tunneling" && options.actionScale > 0,
        },
      );
      work += 1;
      if (!move) continue;
      total +=
        move.scoreDelta +
        stateValue(move.state, options.profile, options.stateScale);
      if (options.profile === "tunneling" && options.actionScale > 0) {
        total += evaluateTunnelingAction(
          state,
          column,
          move,
          options.actionScale,
        );
      }
      work += 1;
    }
    const value = total / options.samples;
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  if (bestColumn === null) throw new Error("No greedy move in live game");
  return { column: bestColumn, work };
}

function averageRootActionBonus(
  state: GameState,
  column: number,
  options: Options,
) {
  if (options.profile !== "tunneling" || options.actionScale === 0) return 0;
  let total = 0;
  for (let sample = 0; sample < options.rollouts; sample += 1) {
    const move = playMove(
      state,
      column,
      seededRandom(candidateRevealSeed(state, column, sample)),
      { captureAnimation: true },
    );
    if (move) {
      total += evaluateTunnelingAction(
        state,
        column,
        move,
        options.actionScale,
      );
    }
  }
  return total / options.rollouts;
}

function runGame(options: Options): GameResult {
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
  let revealed = 0;
  let work = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const decision = chooseMove(state, options);
    work += decision.work;
    const move = playMove(
      state,
      decision.column,
      seededRandom(
        mix32(
          options.seed ^
            Math.imul(state.movesPlayed + 1, 0x85ebca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`Illegal policy move ${decision.column}`);
    maxChain = Math.max(maxChain, move.waves.length);
    for (const wave of move.waves) revealed += wave.revealed;
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
    revealed,
    gameOver: state.gameOver,
    work,
  };
}

function candidateRevealSeed(
  state: GameState,
  column: number,
  sample: number,
) {
  return mix32(
    hashBoard(state.board) ^
      Math.imul(state.movesPlayed + 1, 0x9e3779b9) ^
      Math.imul(column + 1, 0x85ebca6b) ^
      Math.imul(sample + 1, 0xc2b2ae35) ^
      POLICY_DOMAIN,
  );
}

function decisionSeed(state: GameState, plannerSeed: number) {
  return mix32(
    plannerSeed ^
      hashBoard(state.board) ^
      Math.imul(state.movesPlayed + 1, 0x9e3779b9),
  );
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

function integerArgument(name: string, fallback: number, minimum = 1) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isSafeInteger(value) || value < minimum) {
    throw new Error(`${name} must be an integer >= ${minimum}`);
  }
  return value;
}

function finiteArgument(name: string, fallback: number) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = Number(process.argv[index + 1]);
  if (!Number.isFinite(value) || value < 0) {
    throw new Error(`${name} must be a non-negative finite number`);
  }
  return value;
}

function uint32Argument(name: string, fallback: number) {
  const value = integerArgument(name, fallback, 0);
  if (value > 0xffff_ffff) throw new Error(`${name} must be a uint32`);
  return value >>> 0;
}

const seedStart = uint32Argument("--seed", 0x1d70_0000);
const games = integerArgument("--games", 16);
if (seedStart + games - 1 > 0xffff_ffff) {
  throw new Error("seed range exceeds uint32");
}
const profilesValue = process.argv.includes("--profiles")
  ? process.argv[process.argv.indexOf("--profiles") + 1]
  : "combined,tunneling";
const profiles = [...new Set(profilesValue.split(","))] as Profile[];
if (
  profiles.length === 0 ||
  profiles.some((profile) => profile !== "combined" && profile !== "tunneling")
) {
  throw new Error("--profiles accepts combined and tunneling");
}
const baseOptions = {
  plannerSeed: uint32Argument("--planner-seed", 0xd707_5eed),
  samples: integerArgument("--samples", 4),
  rollouts: integerArgument("--rollouts", 0, 0),
  horizon: integerArgument("--horizon", 10),
  continuationSamples: integerArgument("--continuation-samples", 1),
  stateScale: finiteArgument(
    "--state-scale",
    DEFAULT_TUNNELING_STATE_SCALE,
  ),
  actionScale: finiteArgument(
    "--action-scale",
    DEFAULT_TUNNELING_ACTION_SCALE,
  ),
  maxMoves: integerArgument("--max-moves", 500),
};
const details = process.argv.includes("--details");
const resultsByProfile = new Map<Profile, readonly GameResult[]>();

for (const profile of profiles) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame({
      ...baseOptions,
      profile,
      seed: seedStart + offset,
    }),
  );
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  const meanMoves = mean(results.map((result) => result.moves));
  const meanWork = mean(
    results.map((result) => result.work / Math.max(1, result.moves)),
  );
  const censored = results.filter((result) => !result.gameOver).length;
  resultsByProfile.set(profile, results);
  process.stdout.write(
    `${profile.padEnd(10)} mean ${Math.round(mean(scores)).toLocaleString()} · median ${scores[Math.floor(scores.length / 2)].toLocaleString()} · moves ${meanMoves.toFixed(1)} · chain ${mean(results.map((result) => result.maxChain)).toFixed(2)} · reveals ${mean(results.map((result) => result.revealed)).toFixed(1)} · work/move ${Math.round(meanWork).toLocaleString()} · censored ${censored}/${games} · ${baseOptions.rollouts > 0 ? `rollout r=${baseOptions.rollouts} h=${baseOptions.horizon}` : `greedy samples=${baseOptions.samples}`} · stateScale=${baseOptions.stateScale} actionScale=${baseOptions.actionScale}\n`,
  );
  if (details) {
    process.stdout.write(`details ${JSON.stringify({ profile, results })}\n`);
  }
}

const baseline = resultsByProfile.get("combined");
const candidate = resultsByProfile.get("tunneling");
if (baseline && candidate) {
  const scoreDeltas = baseline.map(
    (result, index) => candidate[index].score - result.score,
  );
  const moveDeltas = baseline.map(
    (result, index) => candidate[index].moves - result.moves,
  );
  const revealDeltas = baseline.map(
    (result, index) => candidate[index].revealed - result.revealed,
  );
  const wins = scoreDeltas.filter((delta) => delta > 0).length;
  const ties = scoreDeltas.filter((delta) => delta === 0).length;
  const losses = scoreDeltas.length - wins - ties;
  const interval = bootstrapMeanInterval(scoreDeltas, 20_000);
  process.stdout.write(
    `paired     score ${signed(mean(scoreDeltas), 0)} · median ${signed(median(scoreDeltas), 0)} · moves ${signed(mean(moveDeltas), 1)} · reveals ${signed(mean(revealDeltas), 1)} · W/T/L ${wins}/${ties}/${losses} · bootstrap95 [${signed(interval[0], 0)}, ${signed(interval[1], 0)}]\n`,
  );
}

function mean(values: readonly number[]) {
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
  let randomState = 0xd707_5eed;
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
  const rounded = digits === 0 ? Math.round(value) : value;
  return `${rounded >= 0 ? "+" : ""}${rounded.toLocaleString(undefined, {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  })}`;
}
