/**
 * Regenerates the engine-verified board strings used by the figures on
 * approaches/heuristic-search/README.mdx (and its approach pages).
 *
 * The boards are played through the repository's TypeScript engine in latent
 * mode from the scripted-figure seed domain 0x5eed****, which consumes no
 * research seed lease. Run it and paste the printed board strings into the MDX
 * if a figure ever needs to be rebuilt or checked:
 *
 *   cd web && node --experimental-strip-types scripts/generate-heuristic-figures.ts
 */
import {
  SOLID, CRACKED, EMPTY,
  createGame, createInitialLatentValues, legalColumns, playMove, seededRandom,
  serializeBoard, isNumbered,
  type Board, type DiscValue, type GameState, type LatentValues,
} from "../../src/core/typescript/engine.ts";
import { evaluateMoves } from "../../src/core/typescript/solver.ts";

const TARGETS = [
  { seed: 0x5eed1000, move: 35, columns: [4, 0] },
  { seed: 0x5eed1000, move: 31, columns: [1, 0] },
  { seed: 0x5eed1004, move: 22, columns: [4, 3] },
];

function stats(board: Board) {
  let occupied = 0, covers = 0, numbered = 0;
  const heights = Array(7).fill(0);
  board.forEach((cell, i) => {
    const row = Math.floor(i / 7), col = i % 7;
    if (cell !== EMPTY) { occupied += 1; heights[col] = Math.max(heights[col], 7 - row); }
    if (cell === SOLID || cell === CRACKED) covers += 1;
    if (isNumbered(cell)) numbered += 1;
  });
  return { occupied, covers, numbered, tallest: Math.max(...heights), heights };
}
function coverIndexes(board: Board) {
  return board.map((c, i) => (c === SOLID || c === CRACKED ? i : -1)).filter((i) => i >= 0);
}
function diffIndexes(a: Board, b: Board) {
  const out: number[] = [];
  for (let i = 0; i < 49; i += 1) if (a[i] !== b[i]) out.push(i);
  return out;
}

for (const target of TARGETS) {
  const rng = seededRandom(target.seed);
  let state: GameState = createGame(rng);
  let latent: LatentValues = createInitialLatentValues([3, 1, 6, 2, 7, 4, 5]);
  const coverRng = seededRandom(target.seed ^ 0x9e37);
  const nextCoveredRow = () => Array.from({ length: 7 }, () => (Math.floor(coverRng() * 7) + 1) as DiscValue);
  for (let move = 0; move <= target.move && !state.gameOver; move += 1) {
    if (move === target.move) {
      const before = state.board;
      const bs = stats(before);
      console.log("=== seed 0x" + target.seed.toString(16), "move", move,
        "nextDisc", state.nextDisc, "movesRemaining", state.movesRemaining);
      console.log("before", serializeBoard(before), JSON.stringify(bs));
      console.log("beforeCovers", JSON.stringify(coverIndexes(before)));
      console.log("legal", JSON.stringify(legalColumns(before)));
      const scored = legalColumns(before).map((column) => {
        const r = playMove({ ...state }, column, seededRandom(0x1234),
          { captureAnimation: false, latent: { values: latent, nextCoveredRow: () => [1,2,3,4,5,6,7] as DiscValue[] } })!;
        return { column, scoreDelta: r.scoreDelta };
      });
      console.log("allColumnScores", JSON.stringify(scored));
      console.log("handEvaluatorPick",
        evaluateMoves({ ...state, score: 0, level: 1, movesPlayed: 0 }, { maxDepth: 1, maxWork: 60_000 }).bestColumn);
      for (const column of target.columns) {
        const r = playMove({ ...state }, column, seededRandom(0x1234),
          { captureAnimation: false, latent: { values: latent, nextCoveredRow: () => [1,2,3,4,5,6,7] as DiscValue[] } })!;
        const as = stats(r.state.board);
        console.log(" -- column", column,
          JSON.stringify({
            scoreDelta: r.scoreDelta, waves: r.waves.length,
            cleared: r.waves.reduce((s, w) => s + w.cleared, 0),
            revealed: r.waves.reduce((s, w) => s + w.revealed, 0),
            after: serializeBoard(r.state.board), stats: as,
            changed: diffIndexes(before, r.state.board),
            afterCovers: coverIndexes(r.state.board),
          }));
      }
      break;
    }
    const choice = evaluateMoves({ ...state, score: 0, level: 1, movesPlayed: 0 }, { maxDepth: 1, maxWork: 60_000 }).bestColumn;
    if (choice === null) break;
    const result = playMove(state, choice, rng, { captureAnimation: false, latent: { values: latent, nextCoveredRow } })!;
    latent = (result.latentValues as LatentValues).slice();
    state = result.state;
  }
}
