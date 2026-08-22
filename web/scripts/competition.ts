import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import {
  existsSync,
  readFileSync,
  renameSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import {
  DynamoDBDocumentClient,
  GetCommand,
  PutCommand,
} from "@aws-sdk/lib-dynamodb";
import {
  COMPETITION_GAMES,
  COMPETITION_GAME_KEY,
  type CompetitionCatalog,
  type CompetitionCatalogEntry,
  type CompetitionGameDefinition,
  type CompetitionGameManifest,
} from "../lib/competition/game.ts";
import { packColumns } from "../lib/competition/packing.ts";
import { replayCompetitionColumns } from "../lib/competition/replay.ts";
import {
  COMPETITION_POLICY_IDS,
  getPolicy,
  type BenchPolicy,
} from "../../src/bench/policies.ts";
import { playScriptedGame } from "../../src/bench/runner.ts";
import { validateScriptedRound } from "../../src/bench/rounds.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const WEB_ROOT = resolve(HERE, "..");
const REPO_ROOT = resolve(WEB_ROOT, "..");
const CATALOG_PATH = join(WEB_ROOT, "content", "competition", "catalog.json");

const STAGES = {
  production: {
    table: "drop7-prod-competition-ledger",
    siteUrl: "https://drop7.dev",
  },
  dev: {
    table: "drop7-dev-competition-ledger",
    siteUrl: "https://dev.drop7.dev",
  },
} as const;

interface ParsedOptions {
  values: Map<string, string>;
  flags: Set<string>;
}

function parseOptions(args: readonly string[]): ParsedOptions {
  const values = new Map<string, string>();
  const flags = new Set<string>();
  const booleanFlags = new Set(["--write"]);
  for (let index = 0; index < args.length; index += 1) {
    const option = args[index];
    if (!option.startsWith("--")) throw new Error(`Unexpected argument ${option}`);
    if (booleanFlags.has(option)) {
      flags.add(option);
      continue;
    }
    const value = args[++index];
    if (!value || value.startsWith("--")) {
      throw new Error(`${option} requires a value`);
    }
    values.set(option, value);
  }
  return { values, flags };
}

function option(options: ParsedOptions, name: string, fallback?: string) {
  const value = options.values.get(name) ?? fallback;
  if (value === undefined) throw new Error(`${name} is required`);
  return value;
}

function assertSlug(value: string, label: string) {
  if (!/^[a-z0-9][a-z0-9-]*$/.test(value)) {
    throw new Error(`${label} must be lowercase kebab-case`);
  }
}

function readCatalog(): CompetitionCatalog {
  const catalog = JSON.parse(readFileSync(CATALOG_PATH, "utf8")) as CompetitionCatalog;
  if (catalog.format !== "drop7-competition-catalog-v1") {
    throw new Error("Unsupported competition catalog format");
  }
  return catalog;
}

function writeJsonAtomically(path: string, value: unknown) {
  const temporaryPath = `${path}.tmp-${process.pid}`;
  writeFileSync(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
  renameSync(temporaryPath, path);
}

function listCompetitions() {
  const catalog = readCatalog();
  for (const entry of catalog.games) {
    const marker = entry.gameKey === catalog.currentGameKey ? "current" : "archived";
    console.log(
      `${entry.gameKey.padEnd(28)} ${marker.padEnd(8)} ${entry.manifestPath}`,
    );
  }
}

function startCompetition(options: ParsedOptions) {
  const competitionId = option(options, "--competition", "global");
  const gameVersion = option(options, "--version");
  const roundId = option(options, "--round");
  assertSlug(competitionId, "competition id");
  assertSlug(gameVersion, "game version");
  assertSlug(roundId, "round id");

  const artifactPath = `src/bench/rounds/${roundId}.json`;
  const absoluteArtifactPath = join(REPO_ROOT, artifactPath);
  if (!existsSync(absoluteArtifactPath)) {
    throw new Error(`Unknown scripted round ${roundId}`);
  }
  const artifactSource = readFileSync(absoluteArtifactPath);
  const round = validateScriptedRound(JSON.parse(artifactSource.toString("utf8")));
  if (round.id !== roundId) throw new Error("Round file id does not match its filename");

  const publishedAt = option(
    options,
    "--published-at",
    new Date().toISOString(),
  );
  if (Number.isNaN(Date.parse(publishedAt))) {
    throw new Error("--published-at must be an ISO timestamp");
  }
  const gameKey = `${competitionId}#${gameVersion}`;
  const manifestPath = `web/content/competition/${competitionId}-${gameVersion}.json`;
  const absoluteManifestPath = join(REPO_ROOT, manifestPath);
  if (existsSync(absoluteManifestPath)) {
    throw new Error(`Manifest already exists: ${manifestPath}`);
  }

  const manifest: CompetitionGameManifest = {
    format: "drop7-competition-game-v1",
    competitionId,
    gameVersion,
    name: option(options, "--name", `Global Game · ${gameVersion}`),
    publishedAt,
    ruleset: "corrected-hardcore-17000-v1",
    roundId,
    roundFormat: "drop7-scripted-round-v1",
    artifactPath,
    artifactS3Key: `games/${competitionId}/${gameVersion}.json`,
    artifactSha256: createHash("sha256").update(artifactSource).digest("hex"),
    evidenceClass: "scripted-playground",
  };
  const catalog = readCatalog();
  if (catalog.games.some((entry) => entry.gameKey === gameKey)) {
    throw new Error(`Competition ${gameKey} is already registered`);
  }
  const now = new Date().toISOString();
  const previous = catalog.games.map((entry) =>
    entry.gameKey === catalog.currentGameKey
      ? { ...entry, archivedAt: entry.archivedAt ?? now }
      : entry,
  );
  const nextEntry: CompetitionCatalogEntry = {
    gameKey,
    manifestPath,
    archivedAt: null,
  };
  const nextCatalog: CompetitionCatalog = {
    ...catalog,
    currentGameKey: gameKey,
    games: [nextEntry, ...previous],
  };

  console.log(JSON.stringify({ manifest, catalog: nextCatalog }, null, 2));
  if (!options.flags.has("--write")) {
    console.log("\nPreview only. Re-run with --write to update the repository.");
    return;
  }
  writeJsonAtomically(absoluteManifestPath, manifest);
  writeJsonAtomically(CATALOG_PATH, nextCatalog);
  console.log(`\nStarted ${gameKey}. Deploy the site, then seed its policies.`);
}

function activateCompetition(options: ParsedOptions) {
  const gameKey = option(options, "--game-key");
  const catalog = readCatalog();
  if (!catalog.games.some((entry) => entry.gameKey === gameKey)) {
    throw new Error(`Unknown competition ${gameKey}`);
  }
  const now = new Date().toISOString();
  const nextCatalog: CompetitionCatalog = {
    ...catalog,
    currentGameKey: gameKey,
    games: catalog.games.map((entry) => {
      if (entry.gameKey === gameKey) return { ...entry, archivedAt: null };
      if (entry.gameKey === catalog.currentGameKey) {
        return { ...entry, archivedAt: entry.archivedAt ?? now };
      }
      return entry;
    }),
  };
  console.log(JSON.stringify(nextCatalog, null, 2));
  if (!options.flags.has("--write")) {
    console.log("\nPreview only. Re-run with --write to activate it.");
    return;
  }
  writeJsonAtomically(CATALOG_PATH, nextCatalog);
  console.log(`\nActivated ${gameKey}. Deploy the site before accepting submissions.`);
}

function archiveCompetition(options: ParsedOptions) {
  const gameKey = option(options, "--game-key");
  const catalog = readCatalog();
  if (gameKey === catalog.currentGameKey) {
    throw new Error("Activate or start another competition before archiving the current one");
  }
  if (!catalog.games.some((entry) => entry.gameKey === gameKey)) {
    throw new Error(`Unknown competition ${gameKey}`);
  }
  const nextCatalog: CompetitionCatalog = {
    ...catalog,
    games: catalog.games.map((entry) =>
      entry.gameKey === gameKey
        ? { ...entry, archivedAt: entry.archivedAt ?? new Date().toISOString() }
        : entry,
    ),
  };
  console.log(JSON.stringify(nextCatalog, null, 2));
  if (!options.flags.has("--write")) {
    console.log("\nPreview only. Re-run with --write to archive it.");
    return;
  }
  writeJsonAtomically(CATALOG_PATH, nextCatalog);
  console.log(`\nArchived ${gameKey}. Its ledger entries remain queryable.`);
}

function currentRevision() {
  try {
    return execFileSync("git", ["rev-parse", "HEAD"], {
      cwd: REPO_ROOT,
      encoding: "utf8",
    }).trim();
  } catch {
    return "unknown";
  }
}

function worktreeIsDirty() {
  try {
    return (
      execFileSync(
        "git",
        ["status", "--porcelain", "--untracked-files=no"],
        { cwd: REPO_ROOT, encoding: "utf8" },
      ).trim().length > 0
    );
  } catch {
    return true;
  }
}

function policySubmissionId(gameKey: string, policyId: string) {
  return createHash("sha256")
    .update("drop7-policy-score-v1\0")
    .update(gameKey)
    .update("\0")
    .update(policyId)
    .digest("hex");
}

function researchUrl(policy: BenchPolicy, siteUrl: string) {
  const url = new URL(policy.researchPath, siteUrl);
  if (url.protocol !== "https:") throw new Error("Research URLs must use HTTPS");
  return url.toString();
}

function sameBinary(left: unknown, right: Uint8Array) {
  if (!(left instanceof Uint8Array)) return false;
  return Buffer.from(left).equals(Buffer.from(right));
}

async function putPolicyRecord(
  client: DynamoDBDocumentClient,
  tableName: string,
  record: Record<string, unknown>,
) {
  try {
    await client.send(
      new PutCommand({
        TableName: tableName,
        Item: record,
        ConditionExpression: "attribute_not_exists(submissionId)",
      }),
    );
    return "stored";
  } catch (error) {
    if (!(error instanceof Error) || error.name !== "ConditionalCheckFailedException") {
      throw error;
    }
  }
  const existing = await client.send(
    new GetCommand({
      TableName: tableName,
      Key: { submissionId: record.submissionId },
    }),
  );
  const item = existing.Item;
  if (
    item?.recordType === record.recordType &&
    item.gameKey === record.gameKey &&
    item.policyId === record.policyId &&
    item.verifiedScore === record.verifiedScore &&
    item.trajectoryChecksum === record.trajectoryChecksum &&
    item.researchUrl === record.researchUrl &&
    sameBinary(item.packedMoves, record.packedMoves as Uint8Array)
  ) {
    return "already-present";
  }
  throw new Error(
    `Immutable policy slot ${record.submissionId} already contains a different result; version the policy id instead of overwriting it`,
  );
}

function selectGame(gameKey: string): CompetitionGameDefinition {
  const game = COMPETITION_GAMES.find((candidate) => candidate.gameKey === gameKey);
  if (!game) throw new Error(`Unknown competition ${gameKey}`);
  return game;
}

async function seedCompetition(options: ParsedOptions) {
  const stageName = option(options, "--stage");
  if (!(stageName in STAGES)) throw new Error("--stage must be dev or production");
  const stage = STAGES[stageName as keyof typeof STAGES];
  const tableName = option(options, "--table", stage.table);
  const siteUrl = option(options, "--site-url", stage.siteUrl);
  const region = option(options, "--region", "us-east-1");
  const profile = options.values.get("--profile");
  const game = selectGame(
    option(options, "--game-key", COMPETITION_GAME_KEY),
  );
  const policyIds = option(
    options,
    "--policies",
    COMPETITION_POLICY_IDS.join(","),
  )
    .split(",")
    .map((value) => value.trim())
    .filter(Boolean);
  const policies = policyIds.map(getPolicy);
  if (new Set(policyIds).size !== policyIds.length) {
    throw new Error("--policies contains duplicate ids");
  }
  if (profile) process.env.AWS_PROFILE = profile;
  const write = options.flags.has("--write");
  const client = write
    ? DynamoDBDocumentClient.from(new DynamoDBClient({ region }), {
        marshallOptions: { removeUndefinedValues: true },
      })
    : null;
  const revision = currentRevision();
  const dirty = worktreeIsDirty();

  console.log(
    `${write ? "Seeding" : "Dry-running"} ${policies.length} policies on ${game.gameKey} (${game.manifest.roundId})`,
  );
  console.log("Scripted playground only; these scores are not research-tier evidence.\n");

  for (const policy of policies) {
    const played = playScriptedGame(policy, game.round);
    if (played.illegalMoves !== 0) {
      throw new Error(`${policy.id} made ${played.illegalMoves} illegal choices`);
    }
    const columns = played.frames.map((frame) => frame.column);
    const replay = replayCompetitionColumns(game.round, columns);
    if (!replay.valid || replay.score !== played.score || replay.moves !== played.moves) {
      throw new Error(`${policy.id} failed the independent competition replay`);
    }
    const packedMoves = packColumns(columns);
    const timestamp = new Date().toISOString();
    const record = {
      submissionId: policySubmissionId(game.gameKey, policy.id),
      recordType: "policy-score",
      gameKey: game.gameKey,
      competitionId: game.manifest.competitionId,
      gameVersion: game.manifest.gameVersion,
      roundId: game.manifest.roundId,
      artifactSha256: game.manifest.artifactSha256,
      userId: `policy:${policy.id}`,
      provider: "research-registry",
      providerAccountId: policy.id,
      displayName: policy.name,
      verifiedScore: replay.score,
      clientScore: played.score,
      scoreMismatch: false,
      moveCount: columns.length,
      packedMovesFormat: "drop7-columns-3bit-v1",
      packedMoves,
      censored: replay.censored,
      submittedAt: timestamp,
      validatedAt: timestamp,
      policyId: policy.id,
      policyFamily: policy.family,
      policyDescription: policy.description,
      publicInformation: policy.publicInformation,
      researchUrl: researchUrl(policy, siteUrl),
      trajectoryChecksum: played.checksum,
      policySourceRevision: revision,
      policySourceDirty: dirty,
    };
    const disposition = client
      ? await putPolicyRecord(client, tableName, record)
      : "validated";
    console.log(
      `${policy.id.padEnd(18)} score=${String(replay.score).padStart(8)} moves=${String(replay.moves).padStart(4)} ${disposition}`,
    );
  }
  if (!write) {
    console.log("\nDry run complete. Re-run with --write to persist these entries.");
  }
}

function printHelp() {
  console.log(`Competition lifecycle and AI leaderboard seeding

Usage:
  npm run competition -- list
  npm run competition -- start --version 2026-09-v1 --round gauntlet-02 --name "Global Game · September 2026" [--write]
  npm run competition -- activate --game-key global#2026-08-v1 [--write]
  npm run competition -- archive --game-key global#2026-08-v1 [--write]
  npm run competition -- seed --stage production --profile personal-deploy [--policies expectimax-d4,greedy] [--write]

Mutating commands preview by default. --write updates the catalog or DynamoDB.`);
}

async function main() {
  const [command, ...args] = process.argv.slice(2);
  if (!command || command === "help" || command === "--help") {
    printHelp();
    return;
  }
  const options = parseOptions(args);
  if (command === "list") listCompetitions();
  else if (command === "start") startCompetition(options);
  else if (command === "activate") activateCompetition(options);
  else if (command === "archive") archiveCompetition(options);
  else if (command === "seed") await seedCompetition(options);
  else throw new Error(`Unknown command ${command}`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
