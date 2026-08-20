import { spawnSync } from "node:child_process";
import { pathToFileURL } from "node:url";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";

const DEFAULT_NATIVE = "build/native-suite";
const DEFAULT_SEEDS = [0, 1, 0x1d70_0000, 0xdead_beef, 0xffff_ffff];
const REVEAL_DOMAIN = 0x5245_564c;
const TRACE_DOMAIN = 0x5452_4143;

interface TraceRecord {
  seed: number;
  move: number;
  disc: number;
  column: number;
  scoreDelta: number;
  score: number;
  level: number;
  movesRemaining: number;
  gameOver: boolean;
  clearedBoard: boolean;
  levelAdvanced: boolean;
  waves: MoveResult["waves"];
  board: string;
}

function trace(seed: number, maxMoves: number) {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  const actionRandom = seededRandom(mix32(seed ^ TRACE_DOMAIN));
  const records: TraceRecord[] = [];
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const legal = Array.from({ length: 7 }, (_, column) => column).filter(
      (column) => state.board[column] === 0,
    );
    const column = legal[Math.floor(actionRandom() * legal.length)];
    if (column === undefined) throw new Error("Live state had no legal move");
    const disc = state.nextDisc;
    const revealSeed = mix32(
      seed ^
        Math.imul((state.movesPlayed + 1) >>> 0, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Trace policy chose illegal column ${column}`);
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
    records.push({
      seed,
      move: state.movesPlayed,
      disc,
      column,
      scoreDelta: move.scoreDelta,
      score: state.score,
      level: state.level,
      movesRemaining: state.movesRemaining,
      gameOver: state.gameOver,
      clearedBoard: move.clearedBoard,
      levelAdvanced: move.levelAdvanced,
      waves: move.waves,
      board: state.board.join(""),
    });
  }
  return records.map((record) => JSON.stringify(record));
}

function compareTrace(nativePath: string, seed: number, maxMoves: number) {
  const result = spawnSync(
    nativePath,
    ["--trace", "--seed", String(seed), "--moves", String(maxMoves)],
    { encoding: "utf8" },
  );
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(
      `Native trace failed for ${formatSeed(seed)}:\n${result.stderr.trim()}`,
    );
  }
  const actual = result.stdout.trim().length === 0
    ? []
    : result.stdout.trimEnd().split("\n");
  const expected = trace(seed, maxMoves);
  const lines = Math.max(actual.length, expected.length);
  for (let index = 0; index < lines; index += 1) {
    if (actual[index] !== expected[index]) {
      throw new Error(
        [
          `Trace mismatch for ${formatSeed(seed)} at line ${index + 1}`,
          `TS:     ${expected[index] ?? "<missing>"}`,
          `Native: ${actual[index] ?? "<missing>"}`,
        ].join("\n"),
      );
    }
  }
  return actual.length;
}

function parseArguments(arguments_: readonly string[]) {
  const value = (flag: string) => {
    const index = arguments_.indexOf(flag);
    return index < 0 ? undefined : arguments_[index + 1];
  };
  const nativePath = value("--native") ?? DEFAULT_NATIVE;
  const maxMoves = Number(value("--moves") ?? 500);
  if (!Number.isSafeInteger(maxMoves) || maxMoves < 1) {
    throw new Error("--moves must be a positive integer");
  }
  const rawSeeds = value("--seeds");
  const seedCount = Number(value("--seed-count") ?? 0);
  const seedStart = Number(value("--seed-start") ?? 0x2d70_0000);
  if (!Number.isSafeInteger(seedCount) || seedCount < 0) {
    throw new Error("--seed-count must be a nonnegative integer");
  }
  if (!Number.isSafeInteger(seedStart) || seedStart < 0 || seedStart > 0xffff_ffff) {
    throw new Error("--seed-start must be a uint32");
  }
  const seeds = rawSeeds !== undefined
    ? rawSeeds.split(",").map((raw) => {
        const parsed = Number(raw);
        if (!Number.isSafeInteger(parsed) || parsed < 0 || parsed > 0xffff_ffff) {
          throw new Error(`Invalid uint32 seed: ${raw}`);
        }
        return parsed >>> 0;
      })
    : seedCount > 0
      ? Array.from(
          { length: seedCount },
          (_, offset) => (seedStart + offset) >>> 0,
        )
      : DEFAULT_SEEDS;
  if (seeds.length === 0) throw new Error("At least one seed is required");
  return { nativePath, maxMoves, seeds };
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

export function runCli(arguments_: readonly string[]) {
  const { nativePath, maxMoves, seeds } = parseArguments(arguments_);
  let totalMoves = 0;
  for (const seed of seeds) {
    const moves = compareTrace(nativePath, seed, maxMoves);
    totalMoves += moves;
    process.stderr.write(`parity ${formatSeed(seed)}: ${moves} moves\n`);
  }
  process.stdout.write(
    `PARITY ${JSON.stringify({ seeds: seeds.length, moves: totalMoves, exact: true })}\n`,
  );
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runCli(process.argv.slice(2));
}
