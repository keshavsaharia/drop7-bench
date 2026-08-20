import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type Board,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  DEFAULT_PHASE_HORIZON_WEIGHTS,
  evaluatePhaseHorizon,
} from "../../../src/core/typescript/phase-horizon-evaluator.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";

const DEFAULT_CHECKPOINT =
  "/tmp/drop7-ntuple-chance-hierarchical-l1-500k.bin";
const SCALE_SEED_START = 0x2e70_0000;
const TRAINING_SEED_START = 0x5d70_0000;
const PROBE_SEED_START = 0x4d70_0000;
const REVEAL_DOMAIN = 0x5245_564c;
const POLICY_DOMAIN = 0x504f_4c49;
const SEARCH_DOMAIN = 0xd707_5eed;
const COEFFICIENTS = [0, 0.25, 0.5, 1] as const;
const ACTIVE_FEATURES = 92;
const SHARED_TABLES = 17;
const ABSOLUTE_TABLES = 92;
const RISE_PHASES = 5;
const PATTERNS = 10_000;

interface Arguments {
  checkpoint: string;
  trainingGames: number;
  probeGames: number;
  scaleGames: number;
  maxMoves: number;
  depth: number;
  chanceSamples: number;
  maxWork: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  work: number;
  incomplete: number;
}

interface Summary {
  games: number;
  meanScore: number;
  meanMoves: number;
  minimumScore: number;
  maximumScore: number;
  meanWorkPerMove: number;
  incompleteDecisions: number;
  results: readonly GameResult[];
}

class HierarchicalNtuple {
  readonly shared: Float32Array;
  readonly absolute: Float32Array;

  constructor(path: string) {
    const bytes = readFileSync(path);
    const magic = bytes.subarray(0, 8).toString("latin1");
    if (magic !== "D7NTUH1\0") {
      throw new Error(`Expected a hierarchical n-tuple checkpoint, got ${magic}`);
    }
    const sharedEntries = bytes.readUInt32LE(8);
    const absoluteEntries = bytes.readUInt32LE(12);
    const expectedShared = SHARED_TABLES * RISE_PHASES * PATTERNS;
    const expectedAbsolute = ABSOLUTE_TABLES * RISE_PHASES * PATTERNS;
    if (sharedEntries !== expectedShared || absoluteEntries !== expectedAbsolute) {
      throw new Error("Unexpected n-tuple checkpoint dimensions");
    }
    const expectedBytes = 16 + (sharedEntries + absoluteEntries) * 4;
    if (bytes.byteLength !== expectedBytes) {
      throw new Error(`Expected ${expectedBytes} checkpoint bytes, got ${bytes.byteLength}`);
    }
    this.shared = copyFloats(bytes, 16, sharedEntries);
    this.absolute = copyFloats(bytes, 16 + sharedEntries * 4, absoluteEntries);
  }

  value(state: GameState) {
    if (state.gameOver) return 0;
    const board = canonicalBoard(state.board);
    let value = 0;
    let placement = 0;
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      for (let start = 0; start <= BOARD_SIZE - 4; start += 1) {
        const pattern = patternCode(
          board[row * BOARD_SIZE + start],
          board[row * BOARD_SIZE + start + 1],
          board[row * BOARD_SIZE + start + 2],
          board[row * BOARD_SIZE + start + 3],
        );
        value += this.shared[index(start, state.movesRemaining, pattern)];
        value += this.absolute[index(placement, state.movesRemaining, pattern)];
        placement += 1;
      }
    }
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      for (let start = 0; start <= BOARD_SIZE - 4; start += 1) {
        const pattern = patternCode(
          board[start * BOARD_SIZE + column],
          board[(start + 1) * BOARD_SIZE + column],
          board[(start + 2) * BOARD_SIZE + column],
          board[(start + 3) * BOARD_SIZE + column],
        );
        value += this.shared[index(4 + start, state.movesRemaining, pattern)];
        value += this.absolute[index(placement, state.movesRemaining, pattern)];
        placement += 1;
      }
    }
    for (let row = 0; row < BOARD_SIZE - 1; row += 1) {
      for (let column = 0; column < BOARD_SIZE - 1; column += 1) {
        const pattern = patternCode(
          board[row * BOARD_SIZE + column],
          board[row * BOARD_SIZE + column + 1],
          board[(row + 1) * BOARD_SIZE + column],
          board[(row + 1) * BOARD_SIZE + column + 1],
        );
        const sharedTable = 8 + (row % 3) * 3 + (column % 3);
        value += this.shared[index(sharedTable, state.movesRemaining, pattern)];
        value += this.absolute[index(placement, state.movesRemaining, pattern)];
        placement += 1;
      }
    }
    if (placement !== ACTIVE_FEATURES) throw new Error("Bad tuple placement count");
    return value;
  }
}

const PHASE_SAFETY_WEIGHTS = {
  ...DEFAULT_PHASE_HORIZON_WEIGHTS,
  projectedOccupancyDebt:
    DEFAULT_PHASE_HORIZON_WEIGHTS.projectedOccupancyDebt * 2,
  residualCoverDebt: DEFAULT_PHASE_HORIZON_WEIGHTS.residualCoverDebt * 2,
  coverAltitudeDebt: DEFAULT_PHASE_HORIZON_WEIGHTS.coverAltitudeDebt * 2,
  imminentCoverAltitudeDebt:
    DEFAULT_PHASE_HORIZON_WEIGHTS.imminentCoverAltitudeDebt * 2,
  peakHeightRisk: DEFAULT_PHASE_HORIZON_WEIGHTS.peakHeightRisk * 2,
  triggerReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.triggerReadiness * 2,
  releaseReadiness: DEFAULT_PHASE_HORIZON_WEIGHTS.releaseReadiness * 2,
};

function runGame(
  seed: number,
  evaluator: (state: GameState) => number,
  options: Arguments,
): GameResult {
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
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const decision = evaluateSparseExpectimaxMoves(state, {
      maxDepth: options.depth,
      chanceSamples: options.chanceSamples,
      maxWork: options.maxWork,
      seed: mix32(SEARCH_DOMAIN ^ observableHash(state)),
      terminalUtility: -1_000_000,
      evaluator,
    });
    if (decision.bestColumn === null) throw new Error("Sparse search found no move");
    work += decision.work;
    if (!decision.complete) incomplete += 1;
    const revealSeed = mix32(
      seed ^
        Math.imul((state.movesPlayed + 1) >>> 0, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(
      state,
      decision.bestColumn,
      seededRandom(revealSeed),
      { captureAnimation: false },
    );
    if (!move) throw new Error("Sparse search selected an illegal move");
    state = move.state.gameOver
      ? move.state
      : { ...move.state, nextDisc: headlessDisc(seed, move.state.movesPlayed) };
  }
  return { seed, score: state.score, moves: state.movesPlayed, work, incomplete };
}

function evaluateSeeds(
  seedStart: number,
  games: number,
  evaluator: (state: GameState) => number,
  options: Arguments,
) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame((seedStart + offset) >>> 0, evaluator, options),
  );
  return summarize(results);
}

function summarize(results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  const moves = results.reduce((sum, result) => sum + result.moves, 0);
  return {
    games: results.length,
    meanScore: mean(scores),
    meanMoves: moves / results.length,
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanWorkPerMove:
      results.reduce((sum, result) => sum + result.work, 0) / moves,
    incompleteDecisions: results.reduce(
      (sum, result) => sum + result.incomplete,
      0,
    ),
    results,
  };
}

function calibrationStates(games: number, maxMoves: number) {
  const states: GameState[] = [];
  for (let offset = 0; offset < games; offset += 1) {
    const seed = (SCALE_SEED_START + offset) >>> 0;
    const policyRandom = seededRandom(mix32(seed ^ POLICY_DOMAIN));
    let state: GameState = {
      board: createInitialBoard(),
      nextDisc: headlessDisc(seed, 0),
      score: 0,
      level: 1,
      movesRemaining: MOVES_PER_LEVEL,
      movesPlayed: 0,
      gameOver: false,
    };
    while (!state.gameOver && state.movesPlayed < Math.min(80, maxMoves)) {
      states.push(state);
      const legal = Array.from({ length: BOARD_SIZE }, (_, column) => column).filter(
        (column) => state.board[column] === 0,
      );
      const column = legal[Math.floor(policyRandom() * legal.length)];
      const revealSeed = mix32(
        seed ^
          Math.imul((state.movesPlayed + 1) >>> 0, 0x85eb_ca6b) ^
          REVEAL_DOMAIN,
      );
      const move = playMove(state, column, seededRandom(revealSeed), {
        captureAnimation: false,
      });
      if (!move) throw new Error("Calibration policy selected an illegal move");
      state = move.state.gameOver
        ? move.state
        : { ...move.state, nextDisc: headlessDisc(seed, move.state.movesPlayed) };
    }
  }
  return states;
}

function dispersionScale(model: HierarchicalNtuple, states: readonly GameState[]) {
  const phase = states.map((state) => evaluatePhaseHorizon(state, PHASE_SAFETY_WEIGHTS));
  const learned = states.map((state) => model.value(state));
  const phaseDispersion = standardDeviation(phase);
  const learnedDispersion = standardDeviation(learned);
  if (!(phaseDispersion > 0) || !(learnedDispersion > 0)) {
    throw new Error("Could not calibrate nonzero evaluator dispersion");
  }
  return { phaseDispersion, learnedDispersion, scale: phaseDispersion / learnedDispersion };
}

function canonicalBoard(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[row * BOARD_SIZE + column];
      const reflected = board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
      if (reflected < forward) return mirrorBoard(board);
      if (reflected > forward) return board;
    }
  }
  return board;
}

function mirrorBoard(board: Board) {
  return Array.from({ length: board.length }, (_, target) => {
    const row = Math.floor(target / BOARD_SIZE);
    const column = target % BOARD_SIZE;
    return board[row * BOARD_SIZE + BOARD_SIZE - 1 - column];
  });
}

function patternCode(first: number, second: number, third: number, fourth: number) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

function index(table: number, movesRemaining: number, pattern: number) {
  return (table * RISE_PHASES + movesRemaining - 1) * PATTERNS + pattern;
}

function copyFloats(bytes: Buffer, offset: number, entries: number) {
  const result = new Float32Array(entries);
  for (let index = 0; index < entries; index += 1) {
    result[index] = bytes.readFloatLE(offset + index * 4);
  }
  return result;
}

function observableHash(state: GameState) {
  let hash = 0x811c_9dc5;
  for (const cell of canonicalBoard(state.board)) {
    hash ^= cell + 1;
    hash = Math.imul(hash, 0x0100_0193);
  }
  hash ^= state.nextDisc;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.movesRemaining;
  hash = Math.imul(hash, 0x0100_0193);
  hash ^= state.level;
  return mix32(hash);
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

function standardDeviation(values: readonly number[]) {
  const center = mean(values);
  return Math.sqrt(mean(values.map((value) => (value - center) ** 2)));
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function parseArguments(arguments_: readonly string[]): Arguments {
  const value = (flag: string) => {
    const position = arguments_.indexOf(flag);
    return position < 0 ? undefined : arguments_[position + 1];
  };
  const integer = (flag: string, fallback: number) => {
    const parsed = Number(value(flag) ?? fallback);
    if (!Number.isSafeInteger(parsed) || parsed < 1) {
      throw new Error(`${flag} must be a positive integer`);
    }
    return parsed;
  };
  return {
    checkpoint: value("--checkpoint") ?? DEFAULT_CHECKPOINT,
    trainingGames: integer("--training-games", 8),
    probeGames: integer("--probe-games", 64),
    scaleGames: integer("--scale-games", 24),
    maxMoves: integer("--max-moves", 500),
    depth: integer("--depth", 2),
    chanceSamples: integer("--chance-samples", 3),
    maxWork: integer("--max-work", 100_000),
  };
}

export function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  const model = new HierarchicalNtuple(options.checkpoint);
  const calibration = dispersionScale(
    model,
    calibrationStates(options.scaleGames, options.maxMoves),
  );
  process.stdout.write(`CALIBRATION ${JSON.stringify(calibration)}\n`);
  const candidates = COEFFICIENTS.map((coefficient) => {
    const evaluator = (state: GameState) =>
      evaluatePhaseHorizon(state, PHASE_SAFETY_WEIGHTS) +
      coefficient * calibration.scale * model.value(state);
    const summary = evaluateSeeds(
      TRAINING_SEED_START,
      options.trainingGames,
      evaluator,
      options,
    );
    process.stdout.write(
      `TRAINING ${JSON.stringify({ coefficient, ...summary })}\n`,
    );
    return { coefficient, evaluator, summary };
  });
  candidates.sort(
    (first, second) =>
      second.summary.meanScore - first.summary.meanScore ||
      first.coefficient - second.coefficient,
  );
  const frozen = candidates[0];
  process.stdout.write(
    `FROZEN ${JSON.stringify({ coefficient: frozen.coefficient, training: frozen.summary })}\n`,
  );
  const probe = evaluateSeeds(
    PROBE_SEED_START,
    options.probeGames,
    frozen.evaluator,
    options,
  );
  process.stdout.write(
    `RESULT ${JSON.stringify({ coefficient: frozen.coefficient, calibration, training: frozen.summary, probe })}\n`,
  );
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runCli(process.argv.slice(2));
}
