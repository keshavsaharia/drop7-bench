import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
} from "../../../src/core/typescript/engine.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";

const ORDERS = {
  left: [0, 1, 2, 3, 4, 5, 6],
  center: [3, 2, 4, 1, 5, 0, 6],
  edge: [0, 6, 1, 5, 2, 4, 3],
  paired: [0, 1, 6, 5, 2, 3, 4],
} as const;

const REVEAL_DOMAIN = 0x5245_564c;
const POLICY_DOMAIN = 0x504f_4c59;
const seedStart = Number(argument("--seed", "0x1d700500"));
const games = Number(argument("--games", "64"));
const samples = Number(argument("--samples", "4"));
const maxMoves = Number(argument("--max-moves", "1000"));

for (const [name, order] of Object.entries(ORDERS)) {
  const results = Array.from({ length: games }, (_, offset) =>
    runGame((seedStart + offset) >>> 0, order, samples, maxMoves),
  );
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  process.stdout.write(
    `${name.padEnd(7)} mean ${Math.round(mean(results.map((result) => result.score))).toLocaleString()} · moves ${mean(results.map((result) => result.moves)).toFixed(1)} · max ${Math.max(...results.map((result) => result.score)).toLocaleString()}\n`,
  );
}

for (const strategy of ["shortest", "vertical", "grouped"] as const) {
  const results = Array.from({ length: games }, (_, offset) =>
    runRuleGame((seedStart + offset) >>> 0, strategy, maxMoves),
  );
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  process.stdout.write(
    `${strategy.padEnd(7)} mean ${Math.round(mean(results.map((result) => result.score))).toLocaleString()} · moves ${mean(results.map((result) => result.moves)).toFixed(1)} · max ${Math.max(...results.map((result) => result.score)).toLocaleString()}\n`,
  );
}

function runGame(
  seed: number,
  order: readonly number[],
  policySamples: number,
  moveCap: number,
) {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  while (!state.gameOver && state.movesPlayed < moveCap) {
    const legal = new Set(legalColumns(state.board));
    let bestColumn = -1;
    let bestValue = Number.NEGATIVE_INFINITY;
    for (const column of order) {
      if (!legal.has(column)) continue;
      let value = 0;
      for (let sample = 0; sample < policySamples; sample += 1) {
        const move = playMove(
          state,
          column,
          seededRandom(
            mix32(
              hashBoard(state.board) ^
                Math.imul(state.movesPlayed + 1, 0x9e37_79b9) ^
                Math.imul(column + 1, 0x85eb_ca6b) ^
                Math.imul(sample + 1, 0xc2b2_ae35) ^
                POLICY_DOMAIN,
            ),
          ),
          { captureAnimation: false },
        );
        if (move) {
          value += move.scoreDelta + evaluateHeuristic(move.state, "combined");
        }
      }
      value /= policySamples;
      if (value > bestValue) {
        bestValue = value;
        bestColumn = column;
      }
    }
    if (bestColumn < 0) throw new Error("No legal move");
    const move = playMove(
      state,
      bestColumn,
      seededRandom(
        mix32(
          seed ^
            Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error("Illegal move");
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }
  return { score: state.score, moves: state.movesPlayed };
}

function runRuleGame(
  seed: number,
  strategy: "shortest" | "vertical" | "grouped",
  moveCap: number,
) {
  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  while (!state.gameOver && state.movesPlayed < moveCap) {
    const legal = legalColumns(state.board);
    const heights = Array.from({ length: BOARD_SIZE }, (_, column) =>
      state.board.reduce<number>(
        (height, cell, index) =>
          index % BOARD_SIZE === column && cell !== EMPTY
            ? height + 1
            : height,
        0,
      ),
    );
    let candidates = legal;
    if (strategy === "vertical") {
      const triggers = legal.filter(
        (column) => heights[column] + 1 === state.nextDisc,
      );
      if (triggers.length > 0) candidates = triggers;
    }
    if (strategy === "grouped") {
      const sameCounts = legal.map((column) => ({
        column,
        count: state.board.reduce<number>(
          (count, cell, index) =>
            index % BOARD_SIZE === column && cell === state.nextDisc
              ? count + 1
              : count,
          0,
        ),
      }));
      const maximum = Math.max(...sameCounts.map((entry) => entry.count));
      if (maximum > 0) {
        candidates = sameCounts
          .filter((entry) => entry.count === maximum)
          .map((entry) => entry.column);
      }
    }
    const minimumHeight = Math.min(...candidates.map((column) => heights[column]));
    const bestColumn = ORDERS.center.find(
      (column) =>
        candidates.includes(column) && heights[column] === minimumHeight,
    )!;
    const move = playMove(
      state,
      bestColumn,
      seededRandom(
        mix32(
          seed ^
            Math.imul(state.movesPlayed + 1, 0x85eb_ca6b) ^
            REVEAL_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    if (!move) throw new Error("Illegal rule-policy move");
    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
  }
  return { score: state.score, moves: state.movesPlayed };
}

function argument(name: string, fallback: string) {
  const index = process.argv.indexOf(name);
  return index < 0 ? fallback : process.argv[index + 1];
}

function hashBoard(board: readonly number[]) {
  let hash = 0x811c9dc5;
  for (const cell of board) {
    hash ^= cell + 1;
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}
