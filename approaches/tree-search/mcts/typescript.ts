import { pathToFileURL } from "node:url";

import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";
import { evaluateMctsMoves } from "../../../src/core/typescript/mcts-solver.ts";

interface Arguments {
  seed: number;
  games: number;
  simulations: number;
  horizon: number;
  rolloutDepth: number;
  exploration: number;
  maxNodes: number;
  terminalUtility: number;
  maxMoves: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  maxChain: number;
  clears: number;
  gameOver: boolean;
  work: number;
  nodes: number;
}

const REVEAL_DOMAIN = 0x5245_564c;
const POLICY_SEED = 0xd707_5eed;

export function runMctsGame(options: Arguments, seed: number): GameResult {
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
  let nodes = 0;

  while (!state.gameOver && state.movesPlayed < options.maxMoves) {
    const evaluation = evaluateMctsMoves(state, {
      simulations: options.simulations,
      horizon: options.horizon,
      rolloutDepth: options.rolloutDepth,
      exploration: options.exploration,
      maxNodes: options.maxNodes,
      seed: POLICY_SEED,
      terminalUtility: options.terminalUtility,
      heuristicProfile: "combined",
    });
    if (evaluation.bestColumn === null) {
      throw new Error("MCTS returned no move for a live game");
    }
    work += evaluation.work.simulatedMoves;
    nodes += evaluation.work.createdNodes;

    const revealSeed = mix32(
      seed ^
        Math.imul((state.movesPlayed + 1) >>> 0, 0x85eb_ca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(
      state,
      evaluation.bestColumn,
      seededRandom(revealSeed),
      { captureAnimation: false },
    );
    if (!move) throw new Error("MCTS selected an illegal move");
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
    nodes,
  };
}

export function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  const results: GameResult[] = [];
  for (let offset = 0; offset < options.games; offset += 1) {
    results.push(runMctsGame(options, (options.seed + offset) >>> 0));
  }
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  process.stdout.write(
    [
      `MCTS mean ${Math.round(mean(scores)).toLocaleString()}`,
      `median ${scores[Math.floor(scores.length / 2)].toLocaleString()}`,
      `moves ${mean(results.map((result) => result.moves)).toFixed(1)}`,
      `chain ${mean(results.map((result) => result.maxChain)).toFixed(2)}`,
      `clears ${mean(results.map((result) => result.clears)).toFixed(2)}`,
      `max ${scores.at(-1)!.toLocaleString()}`,
      `censored ${results.filter((result) => !result.gameOver).length}/${results.length}`,
      `work/move ${Math.round(
        mean(
          results.map((result) =>
            result.moves === 0 ? 0 : result.work / result.moves,
          ),
        ),
      ).toLocaleString()}`,
      `nodes/move ${Math.round(
        mean(
          results.map((result) =>
            result.moves === 0 ? 0 : result.nodes / result.moves,
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
  return {
    seed,
    games: integer("--games", 4),
    simulations: integer("--simulations", 2_000),
    horizon: integer("--horizon", 20),
    rolloutDepth: integer("--rollout-depth", 2, 0),
    exploration: finite("--exploration", 40_000),
    maxNodes: integer("--max-nodes", 100_000),
    terminalUtility: finite("--terminal-utility", -1_000_000),
    maxMoves: integer("--max-moves", 1_000),
  };
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
