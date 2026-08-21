/**
 * Runs the scripted-round benchmark and writes leaderboard data for the web
 * app. Usage:
 *
 *   npm run bench                                  # default policies x all rounds
 *   npm run bench -- --policies greedy,expectimax-d3 --rounds gauntlet-01
 *   npm run bench -- --all                         # include slow policies (D4)
 *
 * Output: web/data/leaderboard.json plus one replay file per game under
 * web/data/replays/. Scripted rounds are a public playground; results are not
 * research tier evidence (docs/benchmarks.md).
 */
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  BENCH_POLICIES,
  DEFAULT_POLICY_IDS,
  getPolicy,
} from "./policies.ts";
import { playScriptedGame, type BenchGameResult } from "./runner.ts";
import { validateScriptedRound, type ScriptedRound } from "./rounds.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROUNDS_DIR = join(HERE, "rounds");
const DEFAULT_OUT = join(HERE, "..", "..", "web", "data");

interface CliOptions {
  policyIds: string[];
  roundIds: string[] | null;
  outDir: string;
}

function parseArgs(argv: readonly string[]): CliOptions {
  const options: CliOptions = {
    policyIds: [...DEFAULT_POLICY_IDS],
    roundIds: null,
    outDir: DEFAULT_OUT,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--policies") {
      options.policyIds = argv[++index].split(",").map((id) => id.trim());
    } else if (arg === "--rounds") {
      options.roundIds = argv[++index].split(",").map((id) => id.trim());
    } else if (arg === "--all") {
      options.policyIds = BENCH_POLICIES.map((policy) => policy.id);
    } else if (arg === "--out") {
      options.outDir = argv[++index];
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return options;
}

function loadRounds(): ScriptedRound[] {
  return readdirSync(ROUNDS_DIR)
    .filter((file) => file.endsWith(".json"))
    .sort()
    .map((file) =>
      validateScriptedRound(
        JSON.parse(readFileSync(join(ROUNDS_DIR, file), "utf8")),
      ),
    );
}

function summarize(games: readonly BenchGameResult[]) {
  const scores = games.map((game) => game.score).sort((a, b) => a - b);
  const moves = games.map((game) => game.moves);
  const quantile = (sorted: readonly number[], fraction: number) => {
    if (sorted.length === 0) return null;
    const position = Math.max(
      0,
      Math.min(sorted.length - 1, (sorted.length - 1) * fraction),
    );
    const lower = Math.floor(position);
    const upper = Math.ceil(position);
    return sorted[lower] * (upper - position) + sorted[upper] * (position - lower);
  };
  const mean = (values: readonly number[]) =>
    values.length === 0
      ? null
      : values.reduce((sum, value) => sum + value, 0) / values.length;
  return {
    games: games.length,
    meanScore: mean(scores),
    medianScore: quantile(scores, 0.5),
    minimumScore: scores[0] ?? null,
    maximumScore: scores.at(-1) ?? null,
    meanMoves: mean(moves),
    censoredGames: games.filter((game) => game.censored).length,
    illegalMoves: games.reduce((sum, game) => sum + game.illegalMoves, 0),
    meanClearsPerMove:
      games.reduce((sum, game) => sum + game.discsCleared, 0) /
      Math.max(1, games.reduce((sum, game) => sum + game.moves, 0)),
    meanRevealsPerMove:
      games.reduce((sum, game) => sum + game.coveredRevealed, 0) /
      Math.max(1, games.reduce((sum, game) => sum + game.moves, 0)),
    maxChain: Math.max(0, ...games.map((game) => game.maxChain)),
    elapsedMs: games.reduce((sum, game) => sum + game.elapsedMs, 0),
  };
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  const allRounds = loadRounds();
  const rounds = options.roundIds
    ? options.roundIds.map((id) => {
        const round = allRounds.find((candidate) => candidate.id === id);
        if (!round) {
          throw new Error(
            `Unknown round "${id}". Available: ${allRounds.map((r) => r.id).join(", ")}`,
          );
        }
        return round;
      })
    : allRounds;
  const policies = options.policyIds.map(getPolicy);

  const replayDir = join(options.outDir, "replays");
  mkdirSync(replayDir, { recursive: true });

  const games: BenchGameResult[] = [];
  for (const policy of policies) {
    for (const round of rounds) {
      const startedAt = Date.now();
      const game = playScriptedGame(policy, round);
      games.push(game);
      writeFileSync(
        join(replayDir, `${policy.id}--${round.id}.json`),
        `${JSON.stringify({ ...game, frames: game.frames })}\n`,
      );
      console.log(
        `${policy.id.padEnd(16)} ${round.id}  score=${String(
          game.score,
        ).padStart(8)}  moves=${String(game.moves).padStart(4)}  ${
          game.censored ? "censored" : "game over"
        }  (${Math.round(game.elapsedMs)}ms / ${Date.now() - startedAt}ms)`,
      );
    }
  }

  const leaderboard = {
    format: "drop7-leaderboard-v1",
    generatedAt: new Date().toISOString(),
    moveCap: rounds[0]?.maximumMoves ?? null,
    rounds: rounds.map((round) => ({
      id: round.id,
      name: round.name,
      generatorSeedHex: round.generatorSeedHex,
    })),
    policies: policies.map((policy) => ({
      id: policy.id,
      name: policy.name,
      family: policy.family,
      description: policy.description,
      publicInformation: policy.publicInformation,
    })),
    games: games.map(({ frames: _frames, ...game }) => game),
    summaries: policies.map((policy) => ({
      policyId: policy.id,
      ...summarize(games.filter((game) => game.policyId === policy.id)),
    })),
  };
  writeFileSync(
    join(options.outDir, "leaderboard.json"),
    `${JSON.stringify(leaderboard, null, 2)}\n`,
  );
  console.log(`\nwrote ${join(options.outDir, "leaderboard.json")}`);
}

main();
