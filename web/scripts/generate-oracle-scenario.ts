/**
 * Generates web/content/learn/oracle-scenario.json: one real mid-game position
 * played through the repository's TypeScript engine in latent mode, exported
 * twice — once as the player sees it (gray discs hidden) and once as a
 * privileged planner sees it (the hidden numbers filled in).
 *
 * Latent mode fixes each covered disc's number when its row appears, so the
 * same game has a well-defined "answer key". Nothing here is gameplay
 * evidence: the seeds come from the figure/scripted domain (0x5eed****), the
 * behaviour is a one-move greedy rule, and the output is used only to draw a
 * figure.
 *
 *   cd web && node --experimental-strip-types scripts/generate-oracle-scenario.ts
 */
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import {
  createGame,
  createInitialLatentValues,
  legalColumns,
  playMove,
  randomDisc,
  seededRandom,
  serializeBoard,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "../../src/core/typescript/engine.ts";

const OUT = join(process.cwd(), "content", "learn", "oracle-scenario.json");

interface Snapshot {
  seed: string;
  movesPlayed: number;
  level: number;
  movesRemaining: number;
  nextDisc: number;
  /** What the player sees: "8" solid gray, "9" cracked gray. */
  board: string;
  /** The same board with every covered cell replaced by its hidden number. */
  oracle: string;
  /** Board indexes of the covered cells. */
  covered: number[];
  cracked: number[];
}

function snapshot(seed: number): Snapshot | null {
  const random = seededRandom(seed);
  const hidden = seededRandom(seed ^ 0x0000_00ff);
  let state: GameState = createGame(random);
  let latent: LatentValues = createInitialLatentValues(
    Array.from({ length: 7 }, () => randomDisc(hidden)) as DiscValue[],
  );
  const options = () => ({
    captureAnimation: false as const,
    latent: {
      values: latent,
      nextCoveredRow: () => Array.from({ length: 7 }, () => randomDisc(hidden)) as DiscValue[],
    },
  });

  for (let move = 0; move < 60 && !state.gameOver; move += 1) {
    // Greedy one-move rule: most points now, then the emptiest board, then the
    // lowest column. Deterministic, and only used to reach a lived-in position.
    let best: { column: number; delta: number; filled: number } | null = null;
    for (const column of legalColumns(state.board)) {
      const trial = playMove(state, column, seededRandom(seed + 1), options());
      if (!trial) continue;
      const filled = [...serializeBoard(trial.state.board)].filter((c) => c !== "0").length;
      if (
        best === null ||
        trial.scoreDelta > best.delta ||
        (trial.scoreDelta === best.delta && filled < best.filled)
      ) {
        best = { column, delta: trial.scoreDelta, filled };
      }
    }
    if (!best) break;
    const played = playMove(state, best.column, random, options());
    if (!played) break;
    state = played.state;
    latent = [...(played.latentValues ?? latent)] as LatentValues;

    const board = serializeBoard(state.board);
    const covered: number[] = [];
    const cracked: number[] = [];
    for (let index = 0; index < board.length; index += 1) {
      if (board[index] === "8" || board[index] === "9") covered.push(index);
      if (board[index] === "9") cracked.push(index);
    }
    const numbered = [...board].filter((c) => c >= "1" && c <= "7").length;
    // A position that shows the point: a handful of hidden discs, at least one
    // already cracked, and enough numbered discs to keep the board readable.
    if (
      !state.gameOver &&
      covered.length >= 7 &&
      covered.length <= 16 &&
      cracked.length >= 1 &&
      numbered >= 8
    ) {
      if (covered.some((index) => latent[index] == null)) return null;
      const oracle = [...board]
        .map((cell, index) => (cell === "8" || cell === "9" ? String(latent[index]) : cell))
        .join("");
      return {
        seed: `0x${seed.toString(16)}`,
        movesPlayed: state.movesPlayed,
        level: state.level,
        movesRemaining: state.movesRemaining,
        nextDisc: state.nextDisc,
        board,
        oracle,
        covered,
        cracked,
      };
    }
  }
  return null;
}

let found: Snapshot | null = null;
for (let offset = 0; offset < 64 && !found; offset += 1) {
  found = snapshot(0x5eed_0300 + offset);
}
if (!found) throw new Error("no qualifying position found in the searched figure seeds");
writeFileSync(OUT, `${JSON.stringify(found, null, 2)}\n`);
console.log(`wrote ${OUT} (seed ${found.seed}, move ${found.movesPlayed}, ${found.covered.length} covered)`);
