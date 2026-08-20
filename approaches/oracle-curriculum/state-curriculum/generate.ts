import { writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  evaluatePhaseHorizon,
  type PhaseHorizonWeights,
} from "../../../src/core/typescript/phase-horizon-evaluator.ts";
import { evaluateSparseExpectimaxMoves } from "../../../src/core/typescript/sparse-expectimax.ts";
import { evaluateMoves } from "../../../src/core/typescript/solver.ts";
import { planOracleMove } from "../perfect-information-oracle/main.ts";

const ORACLE_SEED_START = 0x3d70_0000;
const COMBINED_SEED_START = 0x3d71_0000;
const PHASE_SEED_START = 0x3d72_0000;
const REVEAL_DOMAIN = 0x5245_564c;
const REVEAL_MOVE_MULTIPLIER = 0x85eb_ca6b;
const PHASE_POLICY_SEED = 0xd707_5eed;
const MAGIC = new TextEncoder().encode("D7CURR1\0");
const HEADER_BYTES = 8 + 5 * 4;
const RECORD_BYTES = BOARD_SIZE * BOARD_SIZE + 1 + 2 + 1 + 4;

type Source = 1 | 2 | 3;

interface Record_ {
  board: readonly number[];
  movesRemaining: number;
  remainingMoves: number;
  remainingScore: number;
  source: Source;
}

interface Options {
  oracleGames: number;
  negativeGames: number;
  maxMoves: number;
  oracleDepth: number;
  oracleBeam: number;
  output: string;
}

const SAFETY_WEIGHTS: PhaseHorizonWeights = {
  baselineScale: 1,
  projectedOccupancyDebt: -240,
  residualCoverDebt: -200,
  coverAltitudeDebt: -50,
  imminentCoverAltitudeDebt: -70,
  peakHeightRisk: -1_800,
  lowCapLoad: -120,
  adjacentLowCapLoad: -180,
  directBuildInventory: 220,
  quietBuildOptions: 300,
  quietDirectGain: 600,
  triggerReadiness: 600,
  releaseReadiness: 440,
};

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

function mix32(value: number) {
  value = Math.imul(value ^ (value >>> 16), 0x7feb_352d);
  value = Math.imul(value ^ (value >>> 15), 0x846c_a68b);
  return (value ^ (value >>> 16)) >>> 0;
}

function advance(state: GameState, column: number, seed: number) {
  const revealSeed = mix32(
    seed ^
      Math.imul(state.movesPlayed + 1, REVEAL_MOVE_MULTIPLIER) ^
      REVEAL_DOMAIN,
  );
  const move = playMove(state, column, seededRandom(revealSeed), {
    captureAnimation: false,
  });
  if (!move) throw new Error(`Curriculum policy chose illegal column ${column}`);
  return move.state.gameOver
    ? move.state
    : {
        ...move.state,
        nextDisc: headlessDisc(seed, move.state.movesPlayed),
      };
}

function collectEpisode(
  seed: number,
  source: Source,
  options: Options,
): { records: Record_[]; score: number; moves: number; censored: boolean } {
  let state = initialState(seed);
  const states: GameState[] = [];
  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    states.push({ ...state, board: state.board.slice() });
    let column: number | null;
    if (source === 1) {
      column = planOracleMove(
        state,
        seed,
        options.oracleDepth,
        options.oracleBeam,
      ).column;
    } else if (source === 2) {
      column = evaluateMoves(state, {
        maxDepth: 1,
        maxWork: 200_000,
        timeLimitMs: Number.POSITIVE_INFINITY,
        heuristicProfile: "combined",
      }).bestColumn;
    } else {
      column = evaluateSparseExpectimaxMoves(state, {
        maxDepth: 3,
        chanceSamples: 5,
        maxWork: 100_000,
        maxCacheEntries: 40_000,
        seed: PHASE_POLICY_SEED,
        terminalUtility: -1_000_000,
        heuristicProfile: "combined",
        evaluator: (position) =>
          evaluatePhaseHorizon(position, SAFETY_WEIGHTS),
      }).bestColumn;
    }
    if (column === null) throw new Error("Curriculum policy found no live move");
    state = advance(state, column, seed);
  }
  const records = states.map((position) => ({
    board: position.board,
    movesRemaining: position.movesRemaining,
    remainingMoves: state.movesPlayed - position.movesPlayed,
    remainingScore: state.score - position.score,
    source,
  }));
  return {
    records,
    score: state.score,
    moves: state.movesPlayed,
    censored: !state.gameOver,
  };
}

function encode(records: readonly Record_[]) {
  const oracleCount = records.filter((record) => record.source === 1).length;
  const bytes = new Uint8Array(HEADER_BYTES + records.length * RECORD_BYTES);
  bytes.set(MAGIC, 0);
  const view = new DataView(bytes.buffer);
  view.setUint32(8, 1, true);
  view.setUint32(12, records.length, true);
  view.setUint32(16, oracleCount, true);
  view.setUint32(20, records.length - oracleCount, true);
  view.setUint32(24, RECORD_BYTES, true);
  let offset = HEADER_BYTES;
  for (const record of records) {
    if (record.board.length !== BOARD_SIZE * BOARD_SIZE) {
      throw new Error("Curriculum record had the wrong board length");
    }
    for (const token of record.board) bytes[offset++] = token;
    bytes[offset++] = record.movesRemaining;
    view.setUint16(offset, record.remainingMoves, true);
    offset += 2;
    bytes[offset++] = record.source;
    view.setInt32(offset, record.remainingScore, true);
    offset += 4;
  }
  if (offset !== bytes.length) throw new Error("Curriculum encoding mismatch");
  return bytes;
}

function integer(args: readonly string[], flag: string, fallback: number) {
  const index = args.indexOf(flag);
  if (index < 0) return fallback;
  const value = Number(args[index + 1]);
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new Error(`${flag} must be a positive integer`);
  }
  return value;
}

function stringValue(args: readonly string[], flag: string, fallback: string) {
  const index = args.indexOf(flag);
  return index < 0 ? fallback : (args[index + 1] ?? fallback);
}

export async function run(arguments_: readonly string[]) {
  const options: Options = {
    oracleGames: integer(arguments_, "--oracle-games", 8),
    negativeGames: integer(arguments_, "--negative-games", 8),
    maxMoves: integer(arguments_, "--max-moves", 500),
    oracleDepth: integer(arguments_, "--oracle-depth", 4),
    oracleBeam: integer(arguments_, "--oracle-beam", 128),
    output: stringValue(
      arguments_,
      "--output",
      "/tmp/drop7-state-curriculum.bin",
    ),
  };
  if (options.oracleGames > 32 || options.negativeGames > 64) {
    throw new Error("Curriculum collection is bounded at 32/64 games");
  }
  const records: Record_[] = [];
  for (let game = 0; game < options.oracleGames; game += 1) {
    const seed = (ORACLE_SEED_START + game) >>> 0;
    const episode = collectEpisode(seed, 1, options);
    records.push(...episode.records);
    process.stdout.write(
      `oracle ${game + 1}/${options.oracleGames} · ${episode.score.toLocaleString()} points · ${episode.moves} moves${episode.censored ? " capped" : ""}\n`,
    );
  }
  for (const source of [2, 3] as const) {
    const label = source === 2 ? "combined" : "phase-safety";
    const start = source === 2 ? COMBINED_SEED_START : PHASE_SEED_START;
    for (let game = 0; game < options.negativeGames; game += 1) {
      const seed = (start + game) >>> 0;
      const episode = collectEpisode(seed, source, options);
      records.push(...episode.records);
      process.stdout.write(
        `${label} ${game + 1}/${options.negativeGames} · ${episode.score.toLocaleString()} points · ${episode.moves} moves${episode.censored ? " capped" : ""}\n`,
      );
    }
  }
  const encoded = encode(records);
  await writeFile(options.output, encoded);
  const counts = [1, 2, 3].map(
    (source) => records.filter((record) => record.source === source).length,
  );
  process.stdout.write(
    `wrote ${options.output} · ${records.length} states · oracle ${counts[0]} · combined ${counts[1]} · phase ${counts[2]} · ${(encoded.length / 1_048_576).toFixed(3)} MiB\n`,
  );
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await run(process.argv.slice(2));
}
