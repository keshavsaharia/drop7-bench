/*
 * Print every scripted round the competition catalog still serves, current game
 * first. The deploy benches these rounds so the leaderboard and its replay pages
 * cover the current game and every archived game a visitor can still select.
 */
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

interface CatalogEntry {
  gameKey: string;
  manifestPath: string;
}

const catalog = JSON.parse(
  readFileSync(join(REPO_ROOT, "web/content/competition/catalog.json"), "utf8"),
) as { currentGameKey: string; games: CatalogEntry[] };

const ordered = [...catalog.games].sort((left, right) =>
  Number(right.gameKey === catalog.currentGameKey) -
  Number(left.gameKey === catalog.currentGameKey),
);

const rounds: string[] = [];
for (const entry of ordered) {
  const manifest = JSON.parse(
    readFileSync(join(REPO_ROOT, entry.manifestPath), "utf8"),
  ) as { roundId: string };
  if (!rounds.includes(manifest.roundId)) rounds.push(manifest.roundId);
}
if (rounds.length === 0) throw new Error("The competition catalog names no rounds");

process.stdout.write(rounds.join(","));
