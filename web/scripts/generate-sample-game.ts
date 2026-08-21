/**
 * Generates web/content/learn/data/sample-game.json: ONE complete Drop7 game
 * played by the toy policy in web/scripts/toy-policy.ts through the
 * repository's TypeScript engine in latent mode, with a fixed seed.
 *
 * The file is the data behind the figures on
 * /learn/concepts/survival-vs-score. Every board, score, clear, reveal and rise
 * in it is engine output. The policy is a teaching device — "most points right
 * now, then the lowest column" — not a research policy; the game is a
 * demonstration and is never evidence about strategy strength.
 *
 *   cd web && node --experimental-strip-types scripts/generate-sample-game.ts
 */
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { playToyGame } from "./toy-policy.ts";

// Playground domain (0x5eed****): overlaps no research seed lease.
const SEED = 0x5eed_6001;
const LATENT_SEED = 0x5eed_6002;

const game = playToyGame({ seed: SEED, latentSeed: LATENT_SEED, maxMoves: 400 });

const document = {
  generatedBy: "web/scripts/generate-sample-game.ts",
  engine: "src/core/typescript/engine.ts (latent mode)",
  policy: {
    name: "toy-greedy",
    rule: "the legal column that scores the most points now; ties to the lowest column, then the lowest index",
    reads: "public",
    note: "A teaching device, not a research policy. One game, no protocol, no seed lease, no strength claim.",
  },
  seedHex: game.seedHex,
  latentSeedHex: game.latentSeedHex,
  moveCap: 400,
  summary: {
    moves: game.moves,
    score: game.score,
    rises: game.rises,
    boardClears: game.boardClears,
    levelPoints: game.levelPoints,
    chainPoints: game.chainPoints,
    pointsPerMove: game.pointsPerMove,
    clears: game.clears,
    reveals: game.reveals,
    clearsPerMove: game.clearsPerMove,
    revealsPerMove: game.revealsPerMove,
    ending: game.ending,
    finalBoard: game.finalBoard,
  },
  history: game.history,
};

const target = join(import.meta.dirname, "..", "content", "learn", "data", "sample-game.json");
writeFileSync(target, JSON.stringify(document, null, 1) + "\n");
console.log(
  `${game.moves} moves, ${game.score} points, ${game.rises} rises, ` +
    `${game.clearsPerMove.toFixed(3)} clears/move, ${game.revealsPerMove.toFixed(3)} reveals/move, ` +
    `ending ${game.ending}`,
);
console.log("wrote", target);
