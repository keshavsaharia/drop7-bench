import { readFile, stat } from "node:fs/promises";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import {
  CompiledMcReturnPolicy,
  type McReturnArtifact,
} from "../../../src/core/typescript/mc-return-policy.ts";
import { loadDqnCheckpoint } from "../dqn/train.ts";

const DEFAULT_AUDIT_SEED = 0x1d70_4000;
const REVEAL_DOMAIN = 0x5245_564c;

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
}

async function main() {
  const checkpoint = text("--checkpoint", "/tmp/drop7-mc-return-run1.json");
  const dqnCheckpoint = text("--dqn", "/tmp/drop7-dqn-360k.json");
  const seed = uint32("--seed", DEFAULT_AUDIT_SEED);
  const games = integer("--games", 64);
  const maxMoves = integer("--max-moves", 1_000);
  if (seed + games - 1 > 0x1d70_ffff) {
    throw new Error("audit seeds must remain inside 0x1d70xxxx");
  }
  const artifact = JSON.parse(await readFile(checkpoint, "utf8")) as
    McReturnArtifact;
  const mc = new CompiledMcReturnPolicy(artifact);
  const dqn = await loadDqnCheckpoint(dqnCheckpoint, { cacheEntries: 32_768 });
  const mcResults: GameResult[] = [];
  const dqnResults: GameResult[] = [];
  for (let offset = 0; offset < games; offset += 1) {
    const gameSeed = seed + offset;
    mcResults.push(
      runGame(gameSeed, maxMoves, (state) => mc.chooseMove(state)),
    );
    dqnResults.push(
      runGame(gameSeed, maxMoves, (state) => dqn.chooseMove(state)),
    );
  }
  const mcSummary = summarize(mcResults);
  const dqnSummary = summarize(dqnResults);
  const paired = pairedComparison(dqnResults, mcResults);
  const artifactBytes = (await stat(checkpoint)).size;
  process.stdout.write(
    `audit ${formatSeed(seed)}..${formatSeed(seed + games - 1)} · maxMoves ${maxMoves}\n`,
  );
  process.stdout.write(`DQN · ${formatSummary(dqnSummary)}\n`);
  process.stdout.write(`MC   · ${formatSummary(mcSummary)}\n`);
  process.stdout.write(
    `MC vs DQN ${signed(paired.mean)} · median ${signed(paired.median)} · W/T/L ${paired.wins}/${paired.ties}/${paired.losses} · bootstrap95 [${signed(paired.interval[0])}, ${signed(paired.interval[1])}]\n`,
  );
  process.stdout.write(
    `artifact ${(artifactBytes / 1024).toFixed(1)} KiB · replay ${(Number((artifact as unknown as { memory?: { replayBytes?: number } }).memory?.replayBytes ?? 0) / 1024 / 1024).toFixed(1)} MiB\n`,
  );
  process.stdout.write(
    `RESULT ${JSON.stringify({
      seed,
      games,
      maxMoves,
      dqn: { ...dqnSummary, results: dqnResults },
      mc: { ...mcSummary, results: mcResults },
      paired,
      artifactBytes,
    })}\n`,
  );
}

function runGame(
  seed: number,
  maxMoves: number,
  chooseMove: (state: Readonly<GameState>) => number | null,
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
  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const column = chooseMove(state);
    if (column === null) throw new Error("policy found no move in a live game");
    const move = playMove(
      state,
      column,
      seededRandom(
        mix32(
          seed ^
            Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error(`policy chose illegal column ${column}`);
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
    censored: !state.gameOver,
  };
}

function summarize(results: readonly GameResult[]) {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    meanScore: mean(scores),
    medianScore: median(scores),
    meanMoves: mean(results.map((result) => result.moves)),
    censored: results.filter((result) => result.censored).length,
  };
}

function pairedComparison(
  baseline: readonly GameResult[],
  candidate: readonly GameResult[],
) {
  const values = candidate.map(
    (result, index) => result.score - baseline[index].score,
  );
  const random = seededRandom(0x6d63_626f);
  const bootstrapMeans = Array<number>(10_000);
  for (let sample = 0; sample < bootstrapMeans.length; sample += 1) {
    let total = 0;
    for (let index = 0; index < values.length; index += 1) {
      total += values[Math.floor(random() * values.length)];
    }
    bootstrapMeans[sample] = total / values.length;
  }
  bootstrapMeans.sort(numberOrder);
  return {
    mean: mean(values),
    median: median(values),
    wins: values.filter((value) => value > 0).length,
    ties: values.filter((value) => value === 0).length,
    losses: values.filter((value) => value < 0).length,
    interval: [bootstrapMeans[250], bootstrapMeans[9_750]] as const,
  };
}

function text(flag: string, fallback: string) {
  const index = process.argv.indexOf(flag);
  if (index < 0) return fallback;
  const value = process.argv[index + 1];
  if (!value) throw new Error(`${flag} needs a value`);
  return value;
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

function formatSummary(summary: ReturnType<typeof summarize>) {
  return `mean ${Math.round(summary.meanScore).toLocaleString()} · median ${Math.round(summary.medianScore).toLocaleString()} · moves ${summary.meanMoves.toFixed(1)} · censored ${summary.censored}`;
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

function formatSeed(value: number) {
  return `0x${value.toString(16).padStart(8, "0")}`;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

void main();
