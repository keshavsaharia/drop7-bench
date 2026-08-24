// TypeScript-engine trace emitter and move-throughput benchmark.
//
// The Drop7 rules come from src/core/typescript/engine.ts directly.  Two
// modes:
//   trace  --games N   play center-policy games with the TS random driver
//                      (seededRandom, one long-lived stream per game) and
//                      print the same per-move record format the C++ and Rust
//                      emitters use, for the three-way trajectory cross-check.
//   bench  --games N   measure raw moves/second of the TS engine.
//
// The TS driver differs from the C++ headless driver in RNG plumbing only
// (one continuous Mulberry32 stream vs per-move domain-separated seeds); the
// trace mode exists to prove the Rust engine reproduces the TS engine's
// board/score evolution under the TS draw pattern.
//
// Run: node --experimental-strip-types approaches/fair-expectimax/rust-engine/ts/trace.ts

import {
  createGame,
  legalColumns,
  playMove,
  seededRandom,
  serializeBoard,
  type GameState,
} from "../../../../src/core/typescript/engine.ts";

function centerFirst(board: GameState["board"]): number {
  for (const column of [3, 2, 4, 1, 5, 0, 6]) {
    if (legalColumns(board).includes(column)) return column;
  }
  return -1;
}

function main() {
  const args = process.argv.slice(2);
  const mode = args[0] ?? "bench";
  let games = 512;
  let maxMoves = 2000;
  let seedStart = 0xa5277000;
  for (let i = 1; i + 1 < args.length; i += 2) {
    if (args[i] === "--games") games = parseInt(args[i + 1], 10);
    if (args[i] === "--max-moves") maxMoves = parseInt(args[i + 1], 10);
    if (args[i] === "--seed-start") seedStart = parseInt(args[i + 1], 16);
  }

  let totalMoves = 0;
  let totalScore = 0;
  const start = performance.now();
  for (let game = 0; game < games; game += 1) {
    const seed = seedStart + game;
    const random = seededRandom(seed);
    let state = createGame(random);
    if (mode === "trace") {
      console.log(`game 0x${seed.toString(16)} next ${state.nextDisc}`);
    }
    while (!state.gameOver && state.movesPlayed < maxMoves) {
      const column = centerFirst(state.board);
      if (column < 0) break;
      const result = playMove(state, column, random, { captureAnimation: false });
      if (!result) break;
      state = result.state;
      totalMoves += 1;
      if (mode === "trace") {
        const waves = result.waves
          .map((w) => `${w.depth}:${w.cleared}:${w.revealed}:${w.points}`)
          .join(" ");
        console.log(
          `m ${state.movesPlayed} col ${column} sd ${result.scoreDelta} b ${serializeBoard(state.board)} next ${state.nextDisc} score ${state.score} level ${state.level} mr ${state.movesRemaining} over ${state.gameOver ? 1 : 0} cleared ${result.clearedBoard ? 1 : 0} advanced ${result.levelAdvanced ? 1 : 0} waves${waves ? " " + waves : ""}`,
        );
      }
    }
    totalScore += state.score;
    if (mode === "trace") {
      console.log(
        `end score ${state.score} moves ${state.movesPlayed} over ${state.gameOver ? 1 : 0}`,
      );
    }
  }
  const seconds = (performance.now() - start) / 1000;
  if (mode === "bench") {
    console.log(
      `typescript engine: ${games} games, ${totalMoves} moves, ${seconds.toFixed(3)} s, ${Math.round(totalMoves / seconds)} moves/s, ${(seconds * 1e9) / totalMoves} ns/move, mean score ${Math.round(totalScore / games)}`,
    );
  }
}

main();
