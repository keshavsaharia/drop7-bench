/**
 * Copies the per-game rows of a retained score-decomposition run into
 * web/content/learn/data/d4-cohort-scores.json so the console can plot them.
 *
 * The source is a run artifact under `runs/`, which is not committed. This
 * script therefore only *snapshots* numbers that already exist in a retained
 * record; it computes nothing. The summary block is copied verbatim from the
 * run file, and the run is the one reported in
 * docs/exploratory/finding-01-score-is-survival.md (fair D4, 64 games, seed
 * lease SEEDLEASE-A51D, development tier, reproduced in this checkout on
 * 2026-08-20). If the run file is absent the script writes nothing and the
 * figure falls back to a message.
 *
 *   cd web && node --experimental-strip-types scripts/extract-d4-cohort.ts
 */
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const SOURCE = "runs/RUN-A51D-d4/d4-standard64.json";

interface GameRow {
  seedHex: string;
  score: number;
  moves: number;
  censored: boolean;
  rises: number;
  boardClears: number;
  levelPoints: number;
  chainPoints: number;
  numberedCleared: number;
  coversRevealed: number;
  maxChainDepth: number;
}

interface RunFile {
  format: string;
  policy: string;
  seedLease: string;
  seedStartHex: string;
  games: number;
  maximumMoves: number;
  censoredGames: number;
  score: { mean: number; median: number; q25: number; min: number; max: number; sd: number };
  moves: { mean: number; median: number; q25: number; min: number; max: number };
  risesPerGame: number;
  boardClearsPerGame: number;
  numberedClearsPerMove: number;
  coverRevealsPerMove: number;
  pointsPerMove: number;
  decomposition?: Record<string, number>;
  games_detail: GameRow[];
}

const root = join(import.meta.dirname, "..", "..");
const path = join(root, SOURCE);
if (!existsSync(path)) {
  console.log(`${SOURCE} is not present in this checkout; nothing written.`);
  process.exit(0);
}

const run = JSON.parse(readFileSync(path, "utf8")) as RunFile;
const document = {
  generatedBy: "web/scripts/extract-d4-cohort.ts",
  source: SOURCE,
  finding: "docs/exploratory/finding-01-score-is-survival.md",
  policy: run.policy,
  seedLease: run.seedLease,
  seedStartHex: run.seedStartHex,
  games: run.games,
  moveCap: run.maximumMoves,
  censoredGames: run.censoredGames,
  tier: "development",
  evidence: "exploratory finding, reproduced in this checkout 2026-08-20",
  note:
    "Every number here is copied from the run file; nothing is recomputed. " +
    "This is a fresh-seed development cohort and is not the ledger-recorded 64-game reference.",
  score: run.score,
  moves: run.moves,
  risesPerGame: run.risesPerGame,
  boardClearsPerGame: run.boardClearsPerGame,
  numberedClearsPerMove: run.numberedClearsPerMove,
  coverRevealsPerMove: run.coverRevealsPerMove,
  pointsPerMove: run.pointsPerMove,
  decomposition: run.decomposition ?? null,
  games_detail: run.games_detail.map((game) => ({
    seedHex: game.seedHex,
    score: game.score,
    moves: game.moves,
    censored: game.censored,
    rises: game.rises,
    boardClears: game.boardClears,
    levelPoints: game.levelPoints,
    chainPoints: game.chainPoints,
    numberedCleared: game.numberedCleared,
    coversRevealed: game.coversRevealed,
    maxChainDepth: game.maxChainDepth,
  })),
};

const target = join(import.meta.dirname, "..", "content", "learn", "data", "d4-cohort-scores.json");
writeFileSync(target, JSON.stringify(document, null, 1) + "\n");
console.log(
  `${document.games} games, mean ${document.score.mean}, median ${document.score.median}, ` +
    `max ${document.score.max}, censored ${document.censoredGames}`,
);
console.log("wrote", target);
