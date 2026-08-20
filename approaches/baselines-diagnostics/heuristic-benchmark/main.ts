import { pathToFileURL } from "node:url";
import {
  HEURISTIC_PROFILES,
  type HeuristicProfileName,
} from "../../../src/core/typescript/heuristic.ts";
import {
  runHeadlessTournament,
  type HeadlessProfileSummary,
} from "../../../src/core/typescript/headless.ts";

export interface BenchmarkArguments {
  profiles: HeuristicProfileName[];
  seeds: number[];
  depth: number;
  timeLimitMs?: number;
  maxWork?: number;
  maxMoves: number;
  format: "table" | "json";
  quiet: boolean;
}

const MAX_SEED_COUNT = 100_000;

export function runCli(arguments_: readonly string[]) {
  const parsed = parseArguments(arguments_);
  if (parsed === null) {
    process.stdout.write(helpText());
    return;
  }

  const tournament = runHeadlessTournament({
    profiles: parsed.profiles,
    seeds: parsed.seeds,
    search: {
      maxDepth: parsed.depth,
      ...(parsed.timeLimitMs === undefined
        ? {}
        : { timeLimitMs: parsed.timeLimitMs }),
      ...(parsed.maxWork === undefined ? {} : { maxWork: parsed.maxWork }),
    },
    maxMoves: parsed.maxMoves,
    onGameComplete: (game, completed, total) => {
      if (parsed.quiet || parsed.format === "json") return;
      process.stderr.write(
        `\r${completed}/${total} · ${game.heuristicProfile} · seed ${game.seed} · ${formatInteger(game.score)} points`,
      );
      if (completed === total) process.stderr.write("\n");
    },
  });

  if (parsed.format === "json") {
    process.stdout.write(
      `${JSON.stringify(
        tournament,
        (key, value) => (key === "elapsedMs" ? undefined : value),
        2,
      )}\n`,
    );
    return;
  }

  process.stdout.write(`${formatSummaryTable(tournament.summaries)}\n`);
  process.stdout.write(
    `\nreference: ${tournament.referenceProfile} · ${parsed.seeds.length} paired seeds · depth ${parsed.depth} · ${
      parsed.maxWork === undefined
        ? `${formatInteger(parsed.timeLimitMs! / 1_000)}s per move`
        : `${formatInteger(parsed.maxWork)} work per move${
            parsed.timeLimitMs === undefined
              ? ""
              : ` + ${formatInteger(parsed.timeLimitMs / 1_000)}s guard`
          }`
    } · cap ${parsed.maxMoves} moves\n`,
  );
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runCli(process.argv.slice(2));
}

export function parseArguments(
  arguments_: readonly string[],
): BenchmarkArguments | null {
  let profiles: HeuristicProfileName[] = [];
  let seedStart = 1;
  let gameCount = 8;
  let explicitSeeds: number[] | undefined;
  let depth = 1;
  let timeLimitMs: number | undefined;
  let maxWork: number | undefined;
  let maxMoves = 300;
  let format: BenchmarkArguments["format"] = "table";
  let quiet = false;

  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--quiet") {
      quiet = true;
      continue;
    }
    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`Missing value after ${flag}`);
    index += 1;

    switch (flag) {
      case "--profile":
      case "--profiles":
        profiles.push(...parseProfiles(value));
        break;
      case "--games":
        gameCount = parsePositiveInteger(value, flag);
        if (gameCount > MAX_SEED_COUNT) {
          throw new Error(`--games cannot exceed ${MAX_SEED_COUNT}`);
        }
        break;
      case "--seed":
        seedStart = parseInteger(value, flag);
        break;
      case "--seeds":
        explicitSeeds = parseSeeds(value);
        break;
      case "--depth":
        depth = parsePositiveInteger(value, flag);
        if (depth > 8) throw new Error("--depth must be between 1 and 8");
        break;
      case "--time-limit-s":
        timeLimitMs = parsePositiveInteger(value, flag) * 1_000;
        break;
      case "--max-work":
        maxWork = parsePositiveInteger(value, flag);
        break;
      case "--max-moves":
        maxMoves = parsePositiveInteger(value, flag);
        break;
      case "--format":
        if (value !== "table" && value !== "json") {
          throw new Error("--format must be table or json");
        }
        format = value;
        break;
      default:
        throw new Error(`Unknown option ${flag}`);
    }
  }

  if (profiles.length === 0) profiles = ["legacy", "combined"];
  profiles = [...new Set(profiles)];
  if (maxWork === undefined && timeLimitMs === undefined) maxWork = 200_000;
  const seeds = [
    ...new Set(
      explicitSeeds ??
        Array.from({ length: gameCount }, (_, offset) => seedStart + offset),
    ),
  ];
  for (const seed of seeds) validateSeed(seed);

  return {
    profiles,
    seeds,
    depth,
    timeLimitMs,
    maxWork,
    maxMoves,
    format,
    quiet,
  };
}

function parseProfiles(value: string) {
  return value.split(",").map((profile) => {
    if (!Object.hasOwn(HEURISTIC_PROFILES, profile)) {
      throw new Error(
        `Unknown profile ${profile}. Choose ${Object.keys(HEURISTIC_PROFILES).join(", ")}`,
      );
    }
    return profile as HeuristicProfileName;
  });
}

function parseSeeds(value: string) {
  const range = /^(?<start>-?\d+)\.\.(?<end>-?\d+)$/.exec(value);
  if (!range?.groups) {
    return value.split(",").map((seed) => parseInteger(seed, "--seeds"));
  }
  const start = parseInteger(range.groups.start, "--seeds");
  const end = parseInteger(range.groups.end, "--seeds");
  const direction = start <= end ? 1 : -1;
  const length = Math.abs(end - start) + 1;
  if (length > MAX_SEED_COUNT) {
    throw new Error(`--seeds cannot contain more than ${MAX_SEED_COUNT} games`);
  }
  return Array.from(
    { length },
    (_, offset) => start + offset * direction,
  );
}

function parsePositiveInteger(value: string, flag: string) {
  const parsed = parseInteger(value, flag);
  if (parsed < 1) throw new Error(`${flag} must be at least 1`);
  return parsed;
}

function parseInteger(value: string, flag: string) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(`${flag} must be an integer`);
  }
  return parsed;
}

function validateSeed(seed: number) {
  if (seed < 0 || seed > 0xffff_ffff) {
    throw new Error("Seeds must be uint32 integers");
  }
}

function formatSummaryTable(summaries: readonly HeadlessProfileSummary[]) {
  const rows = summaries.map((summary) => ({
    profile: summary.heuristicProfile,
    final: `${summary.completedGames}/${summary.games}`,
    mean: formatOptionalInteger(summary.meanScore),
    median: formatOptionalInteger(summary.medianScore),
    p10: formatOptionalInteger(summary.p10Score),
    p90: formatOptionalInteger(summary.p90Score),
    moves: summary.meanMoves.toFixed(1),
    depth: summary.meanCompletedDepth.toFixed(2),
    depth0: formatPercent(
      summary.depthZeroSearches,
      summary.meanMoves * summary.games,
    ),
    incomplete: formatPercent(
      summary.incompleteSearches,
      summary.meanMoves * summary.games,
    ),
    work: formatInteger(summary.meanSearchWorkPerMove),
    paired: summary.pairedGames.toString(),
    delta: signedInteger(summary.pairedMeanDelta),
    "95% delta": formatInterval(summary.pairedDelta95),
    "W/T/L": `${summary.wins}/${summary.ties}/${summary.losses}`,
  }));
  const headers = Object.keys(rows[0] ?? { profile: "profile" });
  const widths = headers.map((header) =>
    Math.max(
      header.length,
      ...rows.map((row) => String(row[header as keyof typeof row]).length),
    ),
  );
  const render = (values: readonly string[]) =>
    values.map((value, index) => value.padEnd(widths[index])).join("  ");
  return [
    render(headers),
    render(widths.map((width) => "-".repeat(width))),
    ...rows.map((row) =>
      render(headers.map((header) => String(row[header as keyof typeof row]))),
    ),
  ].join("\n");
}

function signedInteger(value: number | null) {
  if (value === null) return "—";
  const rounded = Math.round(value);
  return `${rounded > 0 ? "+" : ""}${formatInteger(rounded)}`;
}

function formatOptionalInteger(value: number | null) {
  return value === null ? "—" : formatInteger(value);
}

function formatInterval(value: readonly [number, number] | null) {
  return value === null
    ? "—"
    : `${signedInteger(value[0])}…${signedInteger(value[1])}`;
}

function formatPercent(numerator: number, denominator: number) {
  return denominator === 0
    ? "—"
    : `${((numerator / denominator) * 100).toFixed(1)}%`;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function helpText() {
  return `Drop7 headless heuristic tournament

Usage:
  npm run benchmark -- [options]

Options:
  --profile NAME[,NAME]  Profiles to compare; repeatable
  --games N              Consecutive seeds to run (default: 8)
  --seed N               First seed (default: 1)
  --seeds A..B           Explicit inclusive seed range
  --depth N              Maximum search depth, 1-8 (default: 1)
  --max-work N           Deterministic work budget per move (default: 200000)
  --time-limit-s N       Optional integer wall-clock limit per move
  --max-moves N          Mark a surviving game censored at this cap (default: 300)
  --format table|json    Summary table or full machine-readable results
  --quiet                Hide per-game progress
  --help                 Show this help

Profiles: ${Object.keys(HEURISTIC_PROFILES).join(", ")}

Use --max-work without a time limit when comparing move quality. A time-limited
run also measures heuristic cost, JIT warm-up, and current machine load.
`;
}
