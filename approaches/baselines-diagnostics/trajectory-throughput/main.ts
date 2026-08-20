import { readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
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
import {
  extractHeuristicFeatures,
  type HeuristicFeatures,
} from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { extractRecursivePotentialFeatures } from "../../../src/core/typescript/recursive-potential.ts";
import { evaluateMoves } from "../../../src/core/typescript/solver.ts";
import { planOracleMove } from "../../oracle-curriculum/perfect-information-oracle/main.ts";

/**
 * Read-only trajectory diagnostic for comparing long-lived Drop7 policies.
 *
 * The perfect-information oracle is an upper-bound diagnostic, not a fair
 * deployable policy. Every row is matched by environment seed and completed
 * five-move window before policy differences are compared. Training and
 * calibration/probe seeds are deliberately used; validation and reserved
 * final-evaluation seeds remain untouched.
 */

const DEFAULT_TRAINING_SEED_START = 0x2d70_0000;
const DEFAULT_CALIBRATION_SEED_START = 0x5d70_0000;
const DEFAULT_VALIDATION_SEED_START = 0x7d70_0000;
const DEFAULT_TRAINING_GAMES = 4;
const DEFAULT_CALIBRATION_GAMES = 4;
const DEFAULT_MAX_MOVES = 100;
const DEFAULT_ORACLE_DEPTH = 4;
const DEFAULT_ORACLE_BEAM = 128;
const DEFAULT_BASELINE_DEPTH = 4;
const DEFAULT_BASELINE_WORK = 20_000;
const DEFAULT_WINDOW = MOVES_PER_LEVEL;

const ACTUAL_REVEAL_DOMAIN = 0x5245_564c;
const ACTUAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const PROBE_REVEAL_DOMAIN = 0x4451_5256;
const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;
const FEATURE_SIZE = 170;

type PolicyName = "oracle" | "combined" | "dqn";

interface Arguments {
  checkpointPath?: string;
  outputPath?: string;
  trainingSeedStart?: number;
  calibrationSeedStart?: number;
  trainingGames: number;
  calibrationGames: number;
  maxMoves: number;
  oracleDepth: number;
  oracleBeam: number;
  baselineDepth: number;
  baselineWork: number;
  window: number;
  selfTest: boolean;
}

interface NetworkSnapshot {
  inputSize: number;
  hiddenOne: number;
  hiddenTwo: number;
  weightsOne: number[];
  biasesOne: number[];
  weightsTwo: number[];
  biasesTwo: number[];
  weightsThree: number[];
  biasThree: number;
}

interface DqnArtifact {
  format: "drop7-observable-double-dqn";
  version: 1;
  algorithm: "double-dqn";
  observableOnly: true;
  trainingSeedStart: number;
  validationSeedStart: number;
  reservedFinalSeedStart: number;
  options: {
    trainingGames: number;
    policySamples: number;
    policySeed: number;
  };
  network: NetworkSnapshot;
}

interface PolicyContext {
  artifact?: DqnArtifact;
  baselineDepth: number;
  baselineWork: number;
  oracleDepth: number;
  oracleBeam: number;
}

interface ChoiceDiagnostic {
  column: number | null;
  completedDepth?: number;
  searchWork?: number;
  generatedStates?: number;
  qMargin?: number;
}

interface BoardMetrics {
  occupied: number;
  solid: number;
  cracked: number;
  numbered: number;
  maxHeight: number;
  meanHeight: number;
  lowCapCount: number;
  lowCapLoad: number;
  adjacentLowCapLoad: number;
  directPotential: number;
  latentPotential: number;
  recursivePotential: number;
  recursiveBonus: number;
  crackedExposure: number;
  solidExposure: number;
  adjacentOnes: number;
  tripleTwos: number;
  deadLowNumbers: number;
}

interface WindowThroughput {
  coversCracked: number;
  coversRevealed: number;
  numberedCleared: number;
  score: number;
  chainWaves: number;
  maxChain: number;
  boardClears: number;
  quietMoves: number;
  multiWaveMoves: number;
  quietDirectGain: number;
  quietLatentGain: number;
  quietRecursiveGain: number;
  triggerPotentialSpent: number;
}

interface WindowRecord extends BoardMetrics, WindowThroughput {
  seed: number;
  seedSet: "training" | "calibration";
  policy: PolicyName;
  startMove: number;
  endMove: number;
  directDelta: number;
  latentDelta: number;
  recursiveDelta: number;
  lowCapLoadDelta: number;
  meanSearchDepth: number | null;
  meanSearchWork: number | null;
  meanGeneratedStates: number | null;
  meanQMargin: number | null;
}

interface GameRecord {
  seed: number;
  seedSet: "training" | "calibration";
  policy: PolicyName;
  score: number;
  moves: number;
  censored: boolean;
  windows: WindowRecord[];
}

interface Report {
  warning: string;
  configuration: {
    trainingSeeds: readonly number[];
    calibrationSeeds: readonly number[];
    maxMoves: number;
    window: number;
    oracle: { depth: number; beamWidth: number };
    combined: { maxDepth: number; maxWork: number };
    dqn: null | {
      checkpoint: string;
      policySamples: number;
      policySeed: number;
    };
  };
  games: GameRecord[];
  matchedWindows: WindowRecord[];
  separation: Record<string, SignalComparison[]>;
}

interface SignalSpec {
  name: string;
  kind: "state" | "throughput" | "delta";
  read: (record: WindowRecord) => number;
}

interface SignalComparison {
  signal: string;
  kind: SignalSpec["kind"];
  samples: number;
  seeds: number;
  oracleMean: number;
  otherMean: number;
  pairedDelta: number;
  pairedEffect: number | null;
}

const SIGNALS: readonly SignalSpec[] = [
  signal("occupied", "state"),
  signal("solid", "state"),
  signal("cracked", "state"),
  signal("numbered", "state"),
  signal("maxHeight", "state"),
  signal("meanHeight", "state"),
  signal("lowCapCount", "state"),
  signal("lowCapLoad", "state"),
  signal("adjacentLowCapLoad", "state"),
  signal("directPotential", "state"),
  signal("latentPotential", "state"),
  signal("recursivePotential", "state"),
  signal("recursiveBonus", "state"),
  signal("crackedExposure", "state"),
  signal("solidExposure", "state"),
  signal("adjacentOnes", "state"),
  signal("tripleTwos", "state"),
  signal("deadLowNumbers", "state"),
  signal("coversCracked", "throughput"),
  signal("coversRevealed", "throughput"),
  signal("numberedCleared", "throughput"),
  signal("score", "throughput"),
  signal("chainWaves", "throughput"),
  signal("maxChain", "throughput"),
  signal("quietMoves", "throughput"),
  signal("multiWaveMoves", "throughput"),
  signal("quietDirectGain", "throughput"),
  signal("quietLatentGain", "throughput"),
  signal("quietRecursiveGain", "throughput"),
  signal("triggerPotentialSpent", "throughput"),
  signal("directDelta", "delta"),
  signal("latentDelta", "delta"),
  signal("recursiveDelta", "delta"),
  signal("lowCapLoadDelta", "delta"),
] as const;

function signal(
  name: keyof WindowRecord & string,
  kind: SignalSpec["kind"],
): SignalSpec {
  return {
    name,
    kind,
    read: (record) => record[name] as number,
  };
}

export async function buildTrajectoryReport(options: Arguments): Promise<Report> {
  const artifact = options.checkpointPath
    ? await readDqnArtifact(options.checkpointPath)
    : undefined;
  const trainingSeedStart = normalizeSeed(
    options.trainingSeedStart ??
      artifact?.trainingSeedStart ??
      DEFAULT_TRAINING_SEED_START,
  );
  const calibrationSeedStart = normalizeSeed(
    options.calibrationSeedStart ??
      (artifact
        ? artifact.trainingSeedStart + artifact.options.trainingGames
        : DEFAULT_CALIBRATION_SEED_START),
  );
  const validationSeedStart =
    artifact?.validationSeedStart ?? DEFAULT_VALIDATION_SEED_START;
  const trainingSeeds = seedRange(trainingSeedStart, options.trainingGames);
  const calibrationSeeds = seedRange(
    calibrationSeedStart,
    options.calibrationGames,
  );
  assertDiagnosticSeedRange(trainingSeeds, validationSeedStart, "training");
  assertDiagnosticSeedRange(
    calibrationSeeds,
    validationSeedStart,
    "calibration",
  );

  const context: PolicyContext = {
    artifact,
    baselineDepth: options.baselineDepth,
    baselineWork: options.baselineWork,
    oracleDepth: options.oracleDepth,
    oracleBeam: options.oracleBeam,
  };
  const policies: PolicyName[] = ["oracle", "combined"];
  if (artifact) policies.push("dqn");
  const games: GameRecord[] = [];

  for (const [seedSet, seeds] of [
    ["training", trainingSeeds],
    ["calibration", calibrationSeeds],
  ] as const) {
    for (const seed of seeds) {
      for (const policy of policies) {
        const game = runTrajectoryGame(
          seed,
          seedSet,
          policy,
          context,
          options.maxMoves,
          options.window,
        );
        games.push(game);
        process.stderr.write(
          `${seedSet} ${formatSeed(seed)} ${policy.padEnd(8)} · ${game.score.toLocaleString("en-US")} · ${game.moves} moves${game.censored ? " capped" : ""}\n`,
        );
      }
    }
  }

  const matchedWindows = matchWindows(games, policies);
  const separation: Record<string, SignalComparison[]> = {};
  for (const other of policies.filter((policy) => policy !== "oracle")) {
    separation[`oracle-vs-${other}`] = compareSignals(
      matchedWindows,
      "oracle",
      other,
    );
  }

  return {
    warning:
      "The oracle sees future discs/reveals and is only an upper-bound diagnostic; differences are associative, not causal feature proofs.",
    configuration: {
      trainingSeeds,
      calibrationSeeds,
      maxMoves: options.maxMoves,
      window: options.window,
      oracle: { depth: options.oracleDepth, beamWidth: options.oracleBeam },
      combined: {
        maxDepth: options.baselineDepth,
        maxWork: options.baselineWork,
      },
      dqn: artifact
        ? {
            checkpoint: resolve(options.checkpointPath!),
            policySamples: artifact.options.policySamples,
            policySeed: artifact.options.policySeed,
          }
        : null,
    },
    games,
    matchedWindows,
    separation,
  };
}

function runTrajectoryGame(
  seed: number,
  seedSet: GameRecord["seedSet"],
  policy: PolicyName,
  context: PolicyContext,
  maxMoves: number,
  windowSize: number,
): GameRecord {
  let state = initialState(seed);
  let windowStartState = state;
  let throughput = emptyThroughput();
  let searchDepth = 0;
  let searchDepthSamples = 0;
  let searchWork = 0;
  let searchWorkSamples = 0;
  let generatedStates = 0;
  let generatedStateSamples = 0;
  let qMargin = 0;
  let qMarginSamples = 0;
  const windows: WindowRecord[] = [];

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const choice = chooseMove(policy, state, seed, context);
    if (choice.column === null) {
      throw new Error(`${policy} found no legal move in a live game`);
    }
    const moved = playActualMove(state, choice.column, seed);
    if (!moved) throw new Error(`${policy} chose illegal column ${choice.column}`);
    accumulateMove(throughput, state, moved);
    if (choice.completedDepth !== undefined) {
      searchDepth += choice.completedDepth;
      searchDepthSamples += 1;
    }
    if (choice.searchWork !== undefined) {
      searchWork += choice.searchWork;
      searchWorkSamples += 1;
    }
    if (choice.generatedStates !== undefined) {
      generatedStates += choice.generatedStates;
      generatedStateSamples += 1;
    }
    if (choice.qMargin !== undefined && Number.isFinite(choice.qMargin)) {
      qMargin += choice.qMargin;
      qMarginSamples += 1;
    }
    state = moved.state;

    if (state.movesPlayed % windowSize === 0) {
      const before = boardMetrics(windowStartState);
      const after = boardMetrics(state);
      windows.push({
        seed,
        seedSet,
        policy,
        startMove: state.movesPlayed - windowSize,
        endMove: state.movesPlayed,
        ...after,
        ...throughput,
        directDelta: after.directPotential - before.directPotential,
        latentDelta: after.latentPotential - before.latentPotential,
        recursiveDelta: after.recursivePotential - before.recursivePotential,
        lowCapLoadDelta: after.lowCapLoad - before.lowCapLoad,
        meanSearchDepth:
          searchDepthSamples === 0 ? null : searchDepth / searchDepthSamples,
        meanSearchWork:
          searchWorkSamples === 0 ? null : searchWork / searchWorkSamples,
        meanGeneratedStates:
          generatedStateSamples === 0
            ? null
            : generatedStates / generatedStateSamples,
        meanQMargin: qMarginSamples === 0 ? null : qMargin / qMarginSamples,
      });
      windowStartState = state;
      throughput = emptyThroughput();
      searchDepth = 0;
      searchDepthSamples = 0;
      searchWork = 0;
      searchWorkSamples = 0;
      generatedStates = 0;
      generatedStateSamples = 0;
      qMargin = 0;
      qMarginSamples = 0;
    }
  }

  return {
    seed,
    seedSet,
    policy,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
    windows,
  };
}

function chooseMove(
  policy: PolicyName,
  state: GameState,
  seed: number,
  context: PolicyContext,
): ChoiceDiagnostic {
  if (policy === "oracle") {
    const plan = planOracleMove(
      state,
      seed,
      context.oracleDepth,
      context.oracleBeam,
    );
    return {
      column: plan.column,
      generatedStates: plan.generatedStates,
    };
  }
  if (policy === "combined") {
    const evaluation = evaluateMoves(state, {
      maxDepth: context.baselineDepth,
      maxWork: context.baselineWork,
      timeLimitMs: Number.POSITIVE_INFINITY,
      heuristicProfile: "combined",
    });
    return {
      column: evaluation.bestColumn,
      completedDepth: evaluation.depth,
      searchWork: evaluation.work,
    };
  }
  if (!context.artifact) throw new Error("DQN checkpoint is unavailable");
  return chooseDqnMove(state, context.artifact);
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

function playActualMove(state: GameState, column: number, seed: number) {
  const revealSeed = mix32(
    seed ^
      Math.imul(state.movesPlayed + 1, ACTUAL_MOVE_MULTIPLIER) ^
      ACTUAL_REVEAL_DOMAIN,
  );
  const move = playMove(state, column, seededRandom(revealSeed), {
    captureAnimation: true,
  });
  if (!move) return null;
  const nextState = move.state.gameOver
    ? move.state
    : {
        ...move.state,
        nextDisc: headlessDisc(seed, move.state.movesPlayed),
      };
  return { ...move, state: nextState };
}

function emptyThroughput(): WindowThroughput {
  return {
    coversCracked: 0,
    coversRevealed: 0,
    numberedCleared: 0,
    score: 0,
    chainWaves: 0,
    maxChain: 0,
    boardClears: 0,
    quietMoves: 0,
    multiWaveMoves: 0,
    quietDirectGain: 0,
    quietLatentGain: 0,
    quietRecursiveGain: 0,
    triggerPotentialSpent: 0,
  };
}

function accumulateMove(
  target: WindowThroughput,
  beforeState: GameState,
  move: MoveResult,
) {
  for (const wave of move.waves) {
    target.coversRevealed += wave.revealed;
    target.numberedCleared += wave.cleared;
  }
  for (const frame of move.animation) {
    if (frame.kind !== "impact") continue;
    for (const index of frame.indexes) {
      if (frame.board[index] === CRACKED) target.coversCracked += 1;
    }
  }
  target.score += move.scoreDelta;
  target.chainWaves += move.waves.length;
  target.maxChain = Math.max(target.maxChain, move.waves.length);
  if (move.clearedBoard) target.boardClears += 1;
  const before = boardMetrics(beforeState);
  const after = boardMetrics(move.state);
  const directDelta = after.directPotential - before.directPotential;
  const latentDelta = after.latentPotential - before.latentPotential;
  const recursiveDelta = after.recursivePotential - before.recursivePotential;
  if (move.waves.length === 0) {
    target.quietMoves += 1;
    target.quietDirectGain += directDelta;
    target.quietLatentGain += latentDelta;
    target.quietRecursiveGain += recursiveDelta;
  } else {
    if (move.waves.length >= 2) target.multiWaveMoves += 1;
    target.triggerPotentialSpent += Math.max(
      0,
      -(directDelta + latentDelta + recursiveDelta),
    );
  }
}

function boardMetrics(state: GameState): BoardMetrics {
  const heuristic = extractHeuristicFeatures(state);
  const recursive = extractRecursivePotentialFeatures(state);
  const heights = columnHeights(state.board);
  const capValues = Array<number>(BOARD_SIZE).fill(0);
  let lowCapCount = 0;
  let lowCapLoad = 0;
  let adjacentLowCapLoad = 0;
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (heights[column] === 0) continue;
    const topRow = BOARD_SIZE - heights[column];
    const cap = state.board[topRow * BOARD_SIZE + column];
    if (cap === 1 || cap === 2) {
      capValues[column] = cap;
      lowCapCount += 1;
      lowCapLoad += heights[column] ** 2 * (cap === 1 ? 1.5 : 1);
    }
    if (column > 0 && capValues[column - 1] > 0 && capValues[column] > 0) {
      adjacentLowCapLoad += Math.min(heights[column - 1], heights[column]) ** 2;
    }
  }
  return {
    occupied: heuristic.solidCells + heuristic.crackedCells + heuristic.numberedCells,
    solid: heuristic.solidCells,
    cracked: heuristic.crackedCells,
    numbered: heuristic.numberedCells,
    maxHeight: Math.max(...heights),
    meanHeight: mean(heights),
    lowCapCount,
    lowCapLoad,
    adjacentLowCapLoad,
    directPotential: heuristic.directPotential,
    latentPotential: heuristic.latentChainPotential,
    recursivePotential: recursive.deepChainEnergy + recursive.deepCoverExposure,
    recursiveBonus:
      recursive.deepChainEnergy * 480 + recursive.deepCoverExposure * 160,
    crackedExposure: heuristic.crackedExposure,
    solidExposure: heuristic.solidExposure,
    adjacentOnes: heuristic.adjacentOnes,
    tripleTwos: heuristic.tripleTwos,
    deadLowNumbers: heuristic.deadLowNumbers,
  };
}

function matchWindows(
  games: readonly GameRecord[],
  policies: readonly PolicyName[],
) {
  const grouped = new Map<string, Map<PolicyName, WindowRecord>>();
  for (const game of games) {
    for (const window of game.windows) {
      const key = `${window.seed}:${window.endMove}`;
      let group = grouped.get(key);
      if (!group) {
        group = new Map();
        grouped.set(key, group);
      }
      group.set(window.policy, window);
    }
  }
  const result: WindowRecord[] = [];
  for (const group of grouped.values()) {
    if (!policies.every((policy) => group.has(policy))) continue;
    for (const policy of policies) result.push(group.get(policy)!);
  }
  return result.sort(
    (first, second) =>
      first.endMove - second.endMove ||
      first.seed - second.seed ||
      first.policy.localeCompare(second.policy),
  );
}

function compareSignals(
  records: readonly WindowRecord[],
  firstPolicy: PolicyName,
  secondPolicy: PolicyName,
) {
  const first = new Map<string, WindowRecord>();
  const second = new Map<string, WindowRecord>();
  for (const record of records) {
    const key = `${record.seed}:${record.endMove}`;
    if (record.policy === firstPolicy) first.set(key, record);
    if (record.policy === secondPolicy) second.set(key, record);
  }
  if (first.size === 0 || second.size === 0) return [];
  return SIGNALS.map((spec): SignalComparison => {
    const firstValues: number[] = [];
    const secondValues: number[] = [];
    const deltas: number[] = [];
    const deltasBySeed = new Map<number, number[]>();
    for (const [key, firstRecord] of first) {
      const secondRecord = second.get(key);
      if (!secondRecord) continue;
      const firstValue = spec.read(firstRecord);
      const secondValue = spec.read(secondRecord);
      firstValues.push(firstValue);
      secondValues.push(secondValue);
      const delta = firstValue - secondValue;
      deltas.push(delta);
      const seedDeltas = deltasBySeed.get(firstRecord.seed) ?? [];
      seedDeltas.push(delta);
      deltasBySeed.set(firstRecord.seed, seedDeltas);
    }
    const clusteredDeltas = [...deltasBySeed.values()].map(mean);
    return {
      signal: spec.name,
      kind: spec.kind,
      samples: deltas.length,
      seeds: deltasBySeed.size,
      oracleMean: mean(firstValues),
      otherMean: mean(secondValues),
      pairedDelta: mean(deltas),
      pairedEffect: standardizedPairedEffect(clusteredDeltas),
    };
  }).sort(
    (first, second) =>
      Math.abs(second.pairedEffect ?? 0) - Math.abs(first.pairedEffect ?? 0) ||
      first.signal.localeCompare(second.signal),
  );
}

/** Exact inference path for the v1 observable Double-DQN checkpoint. */
function chooseDqnMove(
  state: GameState,
  artifact: DqnArtifact,
): ChoiceDiagnostic {
  if (state.gameOver) return { column: null };
  const observable = canonicalObservable(state);
  const legal = columnOrder(observable.mirrored).filter(
    (column) => state.board[column] === EMPTY,
  );
  if (legal.length === 0) return { column: null };
  const ranked = legal.map((column) => ({
    column,
    value: networkValue(
      artifact.network,
      actionInput(
        state,
        column,
        artifact.options.policySamples,
        artifact.options.policySeed,
      ),
    ),
  }));
  ranked.sort((first, second) => second.value - first.value);
  return {
    column: ranked[0].column,
    qMargin:
      ranked.length === 1
        ? Number.POSITIVE_INFINITY
        : ranked[0].value - ranked[1].value,
  };
}

function networkValue(snapshot: NetworkSnapshot, input: Float64Array) {
  const hiddenOne = new Float64Array(snapshot.hiddenOne);
  for (let output = 0; output < snapshot.hiddenOne; output += 1) {
    let sum = snapshot.biasesOne[output];
    const offset = output * snapshot.inputSize;
    for (let inputIndex = 0; inputIndex < snapshot.inputSize; inputIndex += 1) {
      sum += snapshot.weightsOne[offset + inputIndex] * input[inputIndex];
    }
    hiddenOne[output] = Math.max(0, sum);
  }
  const hiddenTwo = new Float64Array(snapshot.hiddenTwo);
  for (let output = 0; output < snapshot.hiddenTwo; output += 1) {
    let sum = snapshot.biasesTwo[output];
    const offset = output * snapshot.hiddenOne;
    for (let inputIndex = 0; inputIndex < snapshot.hiddenOne; inputIndex += 1) {
      sum += snapshot.weightsTwo[offset + inputIndex] * hiddenOne[inputIndex];
    }
    hiddenTwo[output] = Math.max(0, sum);
  }
  let value = snapshot.biasThree;
  for (let index = 0; index < snapshot.hiddenTwo; index += 1) {
    value += snapshot.weightsThree[index] * hiddenTwo[index];
  }
  return value;
}

function actionInput(
  state: GameState,
  column: number,
  samples: number,
  policySeed: number,
) {
  const observable = canonicalObservable(state);
  const canonicalColumn = observable.mirrored ? BOARD_SIZE - 1 - column : column;
  const board = observable.mirrored ? mirrorBoard(state.board) : state.board;
  const values: number[] = [];

  for (const cell of board) values.push(cell >= 1 && cell <= 7 ? cell / 7 : 0);
  for (const cell of board) {
    values.push(cell === SOLID ? 1 : cell === CRACKED ? 0.5 : 0);
  }
  for (let disc = 1; disc <= 7; disc += 1) {
    values.push(state.nextDisc === disc ? 1 : 0);
  }
  values.push(state.movesRemaining / MOVES_PER_LEVEL);
  values.push(Math.min(state.level, 100) / 100);
  values.push(Math.min(state.movesPlayed, 500) / 500);
  appendHeuristic(values, extractHeuristicFeatures(state));
  const heights = columnHeights(board);
  for (const height of heights) values.push(height / BOARD_SIZE);

  for (let candidate = 0; candidate < BOARD_SIZE; candidate += 1) {
    values.push(candidate === canonicalColumn ? 1 : 0);
  }
  const actualHeights = columnHeights(state.board);
  const landingHeight = actualHeights[column] + 1;
  const leftHeight = column === 0 ? landingHeight : actualHeights[column - 1];
  const rightHeight =
    column === BOARD_SIZE - 1 ? landingHeight : actualHeights[column + 1];
  const canonicalLeftHeight = observable.mirrored ? rightHeight : leftHeight;
  const canonicalRightHeight = observable.mirrored ? leftHeight : rightHeight;
  let coveredInColumn = 0;
  let lowInColumn = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const cell = state.board[row * BOARD_SIZE + column];
    if (cell === SOLID || cell === CRACKED) coveredInColumn += 1;
    if (cell === 1 || cell === 2) lowInColumn += 1;
  }
  const placed = placeDisc(state.board, column, state.nextDisc);
  const landingRow = BOARD_SIZE - landingHeight;
  const horizontalLength = placed
    ? contiguousLineLength(placed, landingRow, column, "row")
    : 0;
  values.push(landingHeight / BOARD_SIZE);
  values.push((landingHeight / BOARD_SIZE) ** 2);
  values.push(Math.abs(canonicalColumn - 3) / 3);
  values.push((landingHeight - canonicalLeftHeight) / BOARD_SIZE);
  values.push((landingHeight - canonicalRightHeight) / BOARD_SIZE);
  values.push(coveredInColumn / BOARD_SIZE);
  values.push(lowInColumn / BOARD_SIZE);
  values.push((state.nextDisc - landingHeight) / BOARD_SIZE);
  values.push((state.nextDisc - horizontalLength) / BOARD_SIZE);
  values.push(
    ...expectedCandidateFeatures(
      state,
      column,
      samples,
      policySeed,
      observable.hash,
    ),
  );
  if (values.length !== FEATURE_SIZE) {
    throw new Error(`DQN feature size drifted: expected ${FEATURE_SIZE}, got ${values.length}`);
  }
  return Float64Array.from(values);
}

function expectedCandidateFeatures(
  state: GameState,
  column: number,
  samples: number,
  policySeed: number,
  observableHash: number,
) {
  const result = Array<number>(25).fill(0);
  for (let sample = 0; sample < samples; sample += 1) {
    const reveal = stratifiedProbe(observableHash, policySeed, sample, samples);
    const move = playMove(state, column, () => reveal, {
      captureAnimation: false,
    });
    if (!move) continue;
    let cleared = 0;
    let revealed = 0;
    for (const wave of move.waves) {
      cleared += wave.cleared;
      revealed += wave.revealed;
    }
    const scale = 1 / samples;
    result[0] += (Math.min(move.scoreDelta, 100_000) / 100_000) * scale;
    result[1] += (cleared / (BOARD_SIZE * BOARD_SIZE)) * scale;
    result[2] += (revealed / 14) * scale;
    result[3] += (Math.min(move.waves.length, 10) / 10) * scale;
    result[4] += (move.state.gameOver ? 1 : 0) * scale;
    result[5] += (move.clearedBoard ? 1 : 0) * scale;
    result[6] += (move.levelAdvanced ? 1 : 0) * scale;
    const heuristic: number[] = [];
    appendHeuristic(heuristic, extractHeuristicFeatures(move.state));
    for (let index = 0; index < heuristic.length; index += 1) {
      result[7 + index] += heuristic[index] * scale;
    }
    const heights = columnHeights(move.state.board);
    result[21] += (Math.max(...heights) / BOARD_SIZE) * scale;
    result[22] += (mean(heights) / BOARD_SIZE) * scale;
    result[23] +=
      (roughness(heights) / (BOARD_SIZE * (BOARD_SIZE - 1))) * scale;
    result[24] +=
      (heights.filter((height) => height < BOARD_SIZE).length / BOARD_SIZE) *
      scale;
  }
  return result;
}

function appendHeuristic(target: number[], feature: HeuristicFeatures) {
  target.push(
    feature.openColumns / 7,
    feature.heightLoad / 1_400,
    feature.solidCells / 49,
    feature.crackedCells / 49,
    feature.numberedCells / 49,
    feature.highLowNumbers / 20,
    feature.legacyNearMatches / 49,
    feature.directPotential / 49,
    feature.latentChainPotential / 49,
    feature.crackedExposure / 49,
    feature.solidExposure / 49,
    feature.adjacentOnes / 42,
    feature.tripleTwos / 14,
    feature.deadLowNumbers / 20,
  );
}

function canonicalObservable(state: GameState) {
  const mirrored = mirroredBoardIsSmaller(state.board);
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

function stratifiedProbe(
  observableHash: number,
  policySeed: number,
  sample: number,
  samples: number,
) {
  const offset = mix32(observableHash ^ policySeed ^ PROBE_REVEAL_DOMAIN) % 7;
  const stratum = Math.floor(((sample + 0.5) * 7) / samples);
  const disc = ((offset + stratum) % 7) + 1;
  return (disc - 0.5) / 7;
}

async function readDqnArtifact(path: string): Promise<DqnArtifact> {
  const value: unknown = JSON.parse(await readFile(path, "utf8"));
  if (!isRecord(value)) throw new Error("DQN checkpoint must be an object");
  if (
    value.format !== "drop7-observable-double-dqn" ||
    value.version !== 1 ||
    value.algorithm !== "double-dqn" ||
    value.observableOnly !== true
  ) {
    throw new Error("Unsupported or non-observable DQN checkpoint");
  }
  const options = value.options;
  const network = value.network;
  if (!isRecord(options) || !isRecord(network)) {
    throw new Error("DQN checkpoint is missing options or network");
  }
  const snapshot: NetworkSnapshot = {
    inputSize: finiteInteger(network.inputSize, "network.inputSize"),
    hiddenOne: finiteInteger(network.hiddenOne, "network.hiddenOne"),
    hiddenTwo: finiteInteger(network.hiddenTwo, "network.hiddenTwo"),
    weightsOne: finiteArray(network.weightsOne, "network.weightsOne"),
    biasesOne: finiteArray(network.biasesOne, "network.biasesOne"),
    weightsTwo: finiteArray(network.weightsTwo, "network.weightsTwo"),
    biasesTwo: finiteArray(network.biasesTwo, "network.biasesTwo"),
    weightsThree: finiteArray(network.weightsThree, "network.weightsThree"),
    biasThree: finiteNumber(network.biasThree, "network.biasThree"),
  };
  if (snapshot.inputSize !== FEATURE_SIZE) {
    throw new Error(`Expected ${FEATURE_SIZE} DQN inputs, got ${snapshot.inputSize}`);
  }
  assertLength(snapshot.weightsOne, snapshot.inputSize * snapshot.hiddenOne, "weightsOne");
  assertLength(snapshot.biasesOne, snapshot.hiddenOne, "biasesOne");
  assertLength(snapshot.weightsTwo, snapshot.hiddenOne * snapshot.hiddenTwo, "weightsTwo");
  assertLength(snapshot.biasesTwo, snapshot.hiddenTwo, "biasesTwo");
  assertLength(snapshot.weightsThree, snapshot.hiddenTwo, "weightsThree");
  return {
    format: value.format,
    version: value.version,
    algorithm: value.algorithm,
    observableOnly: value.observableOnly,
    trainingSeedStart: normalizeSeed(value.trainingSeedStart),
    validationSeedStart: normalizeSeed(value.validationSeedStart),
    reservedFinalSeedStart: normalizeSeed(value.reservedFinalSeedStart),
    options: {
      trainingGames: finiteInteger(options.trainingGames, "options.trainingGames"),
      policySamples: finiteInteger(options.policySamples, "options.policySamples"),
      policySeed: normalizeSeed(options.policySeed),
    },
    network: snapshot,
  };
}

function assertLength(values: readonly number[], expected: number, name: string) {
  if (values.length !== expected) {
    throw new Error(`${name} needs ${expected} values, got ${values.length}`);
  }
}

function finiteArray(value: unknown, name: string) {
  if (!Array.isArray(value)) throw new Error(`${name} must be an array`);
  return value.map((entry, index) => finiteNumber(entry, `${name}[${index}]`));
}

function finiteInteger(value: unknown, name: string) {
  const number = finiteNumber(value, name);
  if (!Number.isSafeInteger(number) || number < 1) {
    throw new Error(`${name} must be a positive integer`);
  }
  return number;
}

function finiteNumber(value: unknown, name: string) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`${name} must be finite`);
  }
  return value;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
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

function roughness(heights: readonly number[]) {
  let result = 0;
  for (let column = 1; column < heights.length; column += 1) {
    result += Math.abs(heights[column] - heights[column - 1]);
  }
  return result;
}

function mirroredBoardIsSmaller(board: Board) {
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

function mirrorBoard(board: Board): Board {
  const mirrored = board.slice();
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      mirrored[row * BOARD_SIZE + column] =
        board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
    }
  }
  return mirrored;
}

function columnOrder(mirrored: boolean) {
  return mirrored ? MIRRORED_COLUMN_ORDER : COLUMN_ORDER;
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
  if (values.length === 0) return Number.NaN;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function meanNonNull(values: readonly (number | null)[]) {
  return mean(values.filter((value): value is number => value !== null));
}

function standardizedPairedEffect(deltas: readonly number[]) {
  if (deltas.length < 2) return 0;
  const average = mean(deltas);
  const variance =
    deltas.reduce((sum, value) => sum + (value - average) ** 2, 0) /
    (deltas.length - 1);
  const deviation = Math.sqrt(variance);
  if (deviation <= Number.EPSILON) {
    // The raw delta still communicates a constant separation, but a
    // standardized effect is undefined when seed-to-seed variance is zero.
    return average === 0 ? 0 : null;
  }
  return average / deviation;
}

function seedRange(start: number, count: number) {
  return Array.from({ length: count }, (_, index) => (start + index) >>> 0);
}

function assertDiagnosticSeedRange(
  seeds: readonly number[],
  validationSeedStart: number,
  label: string,
) {
  if (seeds.some((seed) => seed >= validationSeedStart)) {
    throw new Error(`${label} seed range overlaps validation/final seeds`);
  }
}

function normalizeSeed(value: unknown) {
  if (
    typeof value !== "number" ||
    !Number.isSafeInteger(value) ||
    value < 0 ||
    value > 0xffff_ffff
  ) {
    throw new Error("seed must be a uint32 integer");
  }
  return value >>> 0;
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const options: Arguments = {
    trainingGames: DEFAULT_TRAINING_GAMES,
    calibrationGames: DEFAULT_CALIBRATION_GAMES,
    maxMoves: DEFAULT_MAX_MOVES,
    oracleDepth: DEFAULT_ORACLE_DEPTH,
    oracleBeam: DEFAULT_ORACLE_BEAM,
    baselineDepth: DEFAULT_BASELINE_DEPTH,
    baselineWork: DEFAULT_BASELINE_WORK,
    window: DEFAULT_WINDOW,
    selfTest: false,
  };
  const numeric = new Map<string, keyof Arguments>([
    ["--training-start", "trainingSeedStart"],
    ["--calibration-start", "calibrationSeedStart"],
    ["--training-games", "trainingGames"],
    ["--calibration-games", "calibrationGames"],
    ["--max-moves", "maxMoves"],
    ["--oracle-depth", "oracleDepth"],
    ["--oracle-beam", "oracleBeam"],
    ["--baseline-depth", "baselineDepth"],
    ["--baseline-work", "baselineWork"],
    ["--window", "window"],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--self-test") {
      options.selfTest = true;
      continue;
    }
    if (flag === "--checkpoint") {
      options.checkpointPath = requiredValue(arguments_, ++index, flag);
      continue;
    }
    if (flag === "--output") {
      options.outputPath = requiredValue(arguments_, ++index, flag);
      continue;
    }
    const key = numeric.get(flag);
    if (!key) throw new Error(`Unknown argument: ${flag}`);
    (options as unknown as Record<string, number>)[key] = Number(
      requiredValue(arguments_, ++index, flag),
    );
  }
  for (const key of [
    "trainingGames",
    "calibrationGames",
    "maxMoves",
    "oracleDepth",
    "oracleBeam",
    "baselineDepth",
    "baselineWork",
    "window",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  if (options.maxMoves < options.window) {
    throw new Error("maxMoves must include at least one complete window");
  }
  if (options.trainingSeedStart !== undefined) {
    options.trainingSeedStart = normalizeSeed(options.trainingSeedStart);
  }
  if (options.calibrationSeedStart !== undefined) {
    options.calibrationSeedStart = normalizeSeed(options.calibrationSeedStart);
  }
  return options;
}

function requiredValue(arguments_: readonly string[], index: number, flag: string) {
  const value = arguments_[index];
  if (value === undefined) throw new Error(`${flag} needs a value`);
  return value;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

function reportText(report: Report) {
  const policies = [...new Set(report.games.map((game) => game.policy))];
  const combinedWindows = report.games
    .filter((game) => game.policy === "combined")
    .flatMap((game) => game.windows);
  const oracleWindows = report.games
    .filter((game) => game.policy === "oracle")
    .flatMap((game) => game.windows);
  const lines = [
    "# Drop7 matched trajectory throughput",
    "",
    `> ${report.warning}`,
    "",
    `Seeds: training ${seedRangeText(report.configuration.trainingSeeds)}; calibration ${seedRangeText(report.configuration.calibrationSeeds)}. Window: ${report.configuration.window} moves. Cap: ${report.configuration.maxMoves} moves.`,
    `Oracle: depth ${report.configuration.oracle.depth}, beam ${report.configuration.oracle.beamWidth}. Combined: depth ${report.configuration.combined.maxDepth}, deterministic work ${report.configuration.combined.maxWork.toLocaleString("en-US")}. DQN: ${report.configuration.dqn ? report.configuration.dqn.checkpoint : "not loaded"}.`,
    `Search telemetry: combined completed mean depth ${formatNumber(meanNonNull(combinedWindows.map((window) => window.meanSearchDepth)), 2)} at ${formatNumber(meanNonNull(combinedWindows.map((window) => window.meanSearchWork)), 0)} work/move; oracle generated ${formatNumber(meanNonNull(oracleWindows.map((window) => window.meanGeneratedStates)), 0)} states/move.`,
    "",
    "## Outcomes",
    "",
    "| Policy | Games | Mean score | Mean moves | Capped |",
    "| --- | ---: | ---: | ---: | ---: |",
  ];
  for (const policy of policies) {
    const games = report.games.filter((game) => game.policy === policy);
    lines.push(
      `| ${policy} | ${games.length} | ${formatNumber(mean(games.map((game) => game.score)), 0)} | ${formatNumber(mean(games.map((game) => game.moves)), 1)} | ${games.filter((game) => game.censored).length} |`,
    );
  }

  lines.push(
    "",
    "## Matched five-move throughput",
    "",
    "Only seed/window pairs completed by every listed policy are included.",
    "",
    "| End move | N | Policy | Cracks | Reveals | Numbered cleared | Score | Chain waves | Multiwave moves | Quiet potential Δ |",
    "| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
  );
  for (const aggregate of aggregateMatched(report.matchedWindows, policies)) {
    lines.push(
      `| ${aggregate.endMove} | ${aggregate.count} | ${aggregate.policy} | ${formatNumber(aggregate.coversCracked, 2)} | ${formatNumber(aggregate.coversRevealed, 2)} | ${formatNumber(aggregate.numberedCleared, 2)} | ${formatNumber(aggregate.score, 0)} | ${formatNumber(aggregate.chainWaves, 2)} | ${formatNumber(aggregate.multiWaveMoves, 2)} | ${formatSigned(aggregate.quietDirectGain + aggregate.quietLatentGain + aggregate.quietRecursiveGain, 3)} |`,
    );
  }

  lines.push(
    "",
    "## Matched board state",
    "",
    "Low-cap load is height² for a topmost 2 and 1.5×height² for a topmost 1; adjacent low-cap load adds the squared shared height of neighboring low caps.",
    "",
    "| End move | N | Policy | Occupied | Solid | Cracked | Numbered | Max h | Low caps | Low-cap load | Direct | Latent | Recursive |",
    "| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
  );
  for (const aggregate of aggregateMatched(report.matchedWindows, policies)) {
    lines.push(
      `| ${aggregate.endMove} | ${aggregate.count} | ${aggregate.policy} | ${formatNumber(aggregate.occupied, 2)} | ${formatNumber(aggregate.solid, 2)} | ${formatNumber(aggregate.cracked, 2)} | ${formatNumber(aggregate.numbered, 2)} | ${formatNumber(aggregate.maxHeight, 2)} | ${formatNumber(aggregate.lowCapCount, 2)} | ${formatNumber(aggregate.lowCapLoad, 2)} | ${formatNumber(aggregate.directPotential, 3)} | ${formatNumber(aggregate.latentPotential, 3)} | ${formatNumber(aggregate.recursivePotential, 3)} |`,
    );
  }

  for (const [label, comparisons] of Object.entries(report.separation)) {
    lines.push(
      "",
      `## Strongest paired signals: ${label}`,
      "",
      "Effect is the mean oracle-minus-other delta divided by the standard deviation of per-seed mean deltas, so repeated windows do not pretend to be independent games. It is a ranking aid, not a causal estimate.",
      "",
      "| Signal | Kind | Windows | Seeds | Oracle | Other | Delta | Effect |",
      "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    );
    for (const comparison of comparisons.slice(0, 14)) {
      lines.push(
        `| ${comparison.signal} | ${comparison.kind} | ${comparison.samples} | ${comparison.seeds} | ${formatNumber(comparison.oracleMean, 3)} | ${formatNumber(comparison.otherMean, 3)} | ${formatSigned(comparison.pairedDelta, 3)} | ${formatOptionalSigned(comparison.pairedEffect, 2)} |`,
      );
    }
  }

  lines.push(
    "",
    "## DQN-v2 feature/reward hypotheses",
    "",
    "- Represent potential as both stock and action delta: direct, latent, recursive propagation, and cracked/solid exposure. Reward positive potential delta on quiet build moves; separately reward realized clears, cracks, reveals, and chain depth so spending stored potential is not punished.",
    "- Add explicit low-cap count/load, adjacent low-cap load, adjacent-one, locked-two, maximum-height, and cover-altitude channels. Feed both the current state and deterministic candidate post-move deltas.",
    "- Normalize every dense shaping term and clip it below the value of survival. Keep a strong terminal penalty and use multi-step returns so delayed build-and-release sequences receive credit without requiring the reward function to guess the final chain exactly.",
    "- Treat the oracle as curriculum/diagnostic data only: it sees hidden futures. Any promoted reward or feature needs a fresh fair-policy ablation on untouched validation seeds.",
    "",
  );
  return lines.join("\n");
}

type AggregateRecord = Pick<WindowRecord, "policy" | "endMove"> &
  BoardMetrics &
  WindowThroughput & { count: number };

function aggregateMatched(
  records: readonly WindowRecord[],
  policies: readonly PolicyName[],
) {
  const groups = new Map<string, WindowRecord[]>();
  for (const record of records) {
    const key = `${record.endMove}:${record.policy}`;
    const group = groups.get(key) ?? [];
    group.push(record);
    groups.set(key, group);
  }
  const result: AggregateRecord[] = [];
  for (const [key, group] of groups) {
    const [endMoveText, policy] = key.split(":") as [string, PolicyName];
    const numeric = {} as Record<keyof (BoardMetrics & WindowThroughput), number>;
    for (const name of [
      "occupied",
      "solid",
      "cracked",
      "numbered",
      "maxHeight",
      "meanHeight",
      "lowCapCount",
      "lowCapLoad",
      "adjacentLowCapLoad",
      "directPotential",
      "latentPotential",
      "recursivePotential",
      "recursiveBonus",
      "crackedExposure",
      "solidExposure",
      "adjacentOnes",
      "tripleTwos",
      "deadLowNumbers",
      "coversCracked",
      "coversRevealed",
      "numberedCleared",
      "score",
      "chainWaves",
      "maxChain",
      "boardClears",
      "quietMoves",
      "multiWaveMoves",
      "quietDirectGain",
      "quietLatentGain",
      "quietRecursiveGain",
      "triggerPotentialSpent",
    ] as const) {
      numeric[name] = mean(group.map((record) => record[name]));
    }
    result.push({
      policy,
      endMove: Number(endMoveText),
      count: group.length,
      ...numeric,
    });
  }
  return result.sort(
    (first, second) =>
      first.endMove - second.endMove ||
      policies.indexOf(first.policy) - policies.indexOf(second.policy),
  );
}

function seedRangeText(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}

function formatNumber(value: number, digits: number) {
  if (!Number.isFinite(value)) return value > 0 ? "∞" : value < 0 ? "−∞" : "n/a";
  return value.toLocaleString("en-US", {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  });
}

function formatSigned(value: number, digits: number) {
  return `${value >= 0 ? "+" : ""}${formatNumber(value, digits)}`;
}

function formatOptionalSigned(value: number | null, digits: number) {
  return value === null ? "n/a" : formatSigned(value, digits);
}

export function runSelfTest() {
  const state = initialState(0x2d70_0000);
  const metrics = boardMetrics(state);
  if (
    metrics.occupied !== 7 ||
    metrics.solid !== 7 ||
    metrics.cracked !== 0 ||
    metrics.maxHeight !== 1
  ) {
    throw new Error("Initial board metrics are incorrect");
  }
  const move = playActualMove(state, 3, 0x2d70_0000);
  if (!move) throw new Error("Self-test move was illegal");
  const throughput = emptyThroughput();
  accumulateMove(throughput, state, move);
  if (Object.values(throughput).some((value) => !Number.isFinite(value))) {
    throw new Error("Throughput contains a non-finite value");
  }
  const parsed = parseArguments(["--training-games", "2", "--window", "5"]);
  if (parsed.trainingGames !== 2 || parsed.window !== 5) {
    throw new Error("Argument parser failed");
  }
  process.stdout.write("drop7 trajectory throughput self-test passed\n");
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  const report = await buildTrajectoryReport(options);
  const text = reportText(report);
  process.stdout.write(`${text}\n`);
  if (options.outputPath) {
    await writeFile(
      options.outputPath,
      `${JSON.stringify(report, null, 2)}\n`,
      "utf8",
    );
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await runCli(process.argv.slice(2));
}
