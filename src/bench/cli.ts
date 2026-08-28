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
 *
 * Every game also journals each completed move to
 * <checkpoint-dir>/<policy>--<round>.jsonl (default runs/bench-checkpoints/),
 * so a crash mid-game loses at most the decision in flight: rerunning the
 * same policy and round replays the journal through the engine (no policy
 * calls), verifies it move by move, and continues from the checkpoint. The
 * journal is deleted once the final replay is written.
 */
import {
  closeSync,
  existsSync,
  mkdirSync,
  openSync,
  readFileSync,
  readdirSync,
  unlinkSync,
  writeFileSync,
  writeSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  BENCH_POLICIES,
  DEFAULT_POLICY_IDS,
  getPolicy,
  type BenchPolicy,
} from "./policies.ts";
import {
  playScriptedGame,
  type BenchCheckpointEntry,
  type BenchGameResult,
} from "./runner.ts";
import { validateScriptedRound, type ScriptedRound } from "./rounds.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROUNDS_DIR = join(HERE, "rounds");
const DEFAULT_OUT = join(HERE, "..", "..", "web", "data");
const DEFAULT_CHECKPOINT_DIR = join(HERE, "..", "..", "runs", "bench-checkpoints");
const CHECKPOINT_FORMAT = "drop7-bench-checkpoint-v1";

interface CliOptions {
  policyIds: string[];
  roundIds: string[] | null;
  outDir: string;
  checkpointDir: string;
}

function parseArgs(argv: readonly string[]): CliOptions {
  const options: CliOptions = {
    policyIds: [...DEFAULT_POLICY_IDS],
    roundIds: null,
    outDir: DEFAULT_OUT,
    checkpointDir: DEFAULT_CHECKPOINT_DIR,
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
    } else if (arg === "--checkpoint-dir") {
      options.checkpointDir = argv[++index];
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return options;
}

/**
 * Reads the surviving prefix of a per-game crash journal. A truncated final
 * line (a crash mid-write), a gap in move numbers, or a header for a different
 * policy/round ends or discards the prefix — the caller then replays whatever
 * consistent prefix remains.
 */
function loadCheckpoint(
  path: string,
  policyId: string,
  roundId: string,
): BenchCheckpointEntry[] {
  if (!existsSync(path)) return [];
  const lines = readFileSync(path, "utf8").split("\n");
  let sawHeader = false;
  const entries: BenchCheckpointEntry[] = [];
  for (const line of lines) {
    if (line.trim() === "") continue;
    let parsed: Record<string, unknown>;
    try {
      parsed = JSON.parse(line);
    } catch {
      break;
    }
    if (!sawHeader) {
      if (
        parsed.format !== CHECKPOINT_FORMAT ||
        parsed.policyId !== policyId ||
        parsed.roundId !== roundId
      ) {
        console.warn(`ignoring checkpoint ${path}: header does not match ${policyId}--${roundId}`);
        return [];
      }
      sawHeader = true;
      continue;
    }
    if (
      typeof parsed.move !== "number" ||
      typeof parsed.column !== "number" ||
      typeof parsed.board !== "string" ||
      typeof parsed.score !== "number" ||
      parsed.move !== entries.length + 1
    ) {
      break;
    }
    entries.push({
      move: parsed.move,
      column: parsed.column,
      board: parsed.board,
      score: parsed.score,
      illegal: parsed.illegal === true,
    });
  }
  return entries;
}

/**
 * Plays one game while journaling every fresh move to the checkpoint file, so
 * a crash loses at most the decision in flight. An existing journal is
 * replayed through the engine first (no policy calls) and verified move by
 * move; a mismatched journal — a changed policy, engine, or round — is
 * discarded with a warning and the game restarts from scratch.
 */
function playCheckpointedGame(
  policy: BenchPolicy,
  round: ScriptedRound,
  checkpointPath: string,
): BenchGameResult {
  let resume = loadCheckpoint(checkpointPath, policy.id, round.id);
  for (;;) {
    const fresh = resume.length === 0;
    const fd = openSync(checkpointPath, fresh ? "w" : "a");
    if (fresh) {
      writeSync(
        fd,
        `${JSON.stringify({ format: CHECKPOINT_FORMAT, policyId: policy.id, roundId: round.id })}\n`,
      );
    } else {
      console.log(
        `${policy.id.padEnd(16)} ${round.id}  resuming from checkpoint move ${resume.length} (score ${resume[resume.length - 1].score})`,
      );
    }
    try {
      return playScriptedGame(policy, round, {
        captureAnimation: true,
        resume,
        onFrame: (_frame, entry, resumed) => {
          if (!resumed) writeSync(fd, `${JSON.stringify(entry)}\n`);
        },
      });
    } catch (error) {
      if (
        !fresh &&
        error instanceof Error &&
        error.message.startsWith("checkpoint mismatch")
      ) {
        console.warn(`${error.message}; restarting ${policy.id} on ${round.id} from scratch`);
        resume = [];
        continue;
      }
      throw error;
    } finally {
      closeSync(fd);
    }
  }
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
  mkdirSync(options.checkpointDir, { recursive: true });

  const games: BenchGameResult[] = [];
  for (const policy of policies) {
    for (const round of rounds) {
      const startedAt = Date.now();
      const checkpointPath = join(
        options.checkpointDir,
        `${policy.id}--${round.id}.jsonl`,
      );
      const game = playCheckpointedGame(policy, round, checkpointPath);
      games.push(game);
      writeFileSync(
        join(replayDir, `${policy.id}--${round.id}.json`),
        `${JSON.stringify({ ...game, frames: game.frames })}\n`,
      );
      // The persisted replay supersedes the crash journal.
      unlinkSync(checkpointPath);
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
      researchPath: policy.researchPath,
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
