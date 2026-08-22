import { createHash } from "node:crypto";
import { validateScriptedRound } from "../../../src/bench/rounds.ts";
import catalogJson from "../../content/competition/catalog.json" with { type: "json" };
import { readRepoFile } from "../repo.ts";

export interface CompetitionGameManifest {
  format: "drop7-competition-game-v1";
  competitionId: string;
  gameVersion: string;
  name: string;
  publishedAt: string;
  ruleset: string;
  roundId: string;
  roundFormat: "drop7-scripted-round-v1";
  artifactPath: string;
  artifactS3Key: string;
  artifactSha256: string;
  evidenceClass: "scripted-playground";
}

export interface CompetitionCatalogEntry {
  gameKey: string;
  manifestPath: string;
  archivedAt: string | null;
}

export interface CompetitionCatalog {
  format: "drop7-competition-catalog-v1";
  currentGameKey: string;
  games: CompetitionCatalogEntry[];
}

export interface CompetitionGameDefinition {
  gameKey: string;
  manifest: CompetitionGameManifest;
  round: ReturnType<typeof validateScriptedRound>;
  archivedAt: string | null;
}

export function validateManifest(value: unknown): CompetitionGameManifest {
  const manifest = value as CompetitionGameManifest;
  if (
    !manifest ||
    manifest.format !== "drop7-competition-game-v1" ||
    !/^[a-z0-9][a-z0-9-]*$/.test(manifest.competitionId) ||
    !/^[a-z0-9][a-z0-9-]*$/.test(manifest.gameVersion) ||
    !/^src\/bench\/rounds\/[a-z0-9][a-z0-9-]*\.json$/.test(
      manifest.artifactPath,
    ) ||
    !/^games\/[a-z0-9][a-z0-9-]*\/[a-z0-9][a-z0-9-]*\.json$/.test(
      manifest.artifactS3Key,
    ) ||
    !/^[a-f0-9]{64}$/.test(manifest.artifactSha256) ||
    manifest.evidenceClass !== "scripted-playground"
  ) {
    throw new Error("Invalid competition game manifest");
  }
  return manifest;
}

function validateCatalog(value: unknown): CompetitionCatalog {
  const catalog = value as CompetitionCatalog;
  if (
    !catalog ||
    catalog.format !== "drop7-competition-catalog-v1" ||
    !Array.isArray(catalog.games) ||
    catalog.games.length === 0 ||
    !catalog.games.some((game) => game.gameKey === catalog.currentGameKey)
  ) {
    throw new Error("Invalid competition catalog");
  }
  const gameKeys = new Set<string>();
  for (const entry of catalog.games) {
    if (
      !/^[a-z0-9][a-z0-9-]*#[a-z0-9][a-z0-9-]*$/.test(entry.gameKey) ||
      !/^web\/content\/competition\/[a-z0-9][a-z0-9-]*\.json$/.test(
        entry.manifestPath,
      ) ||
      (entry.archivedAt !== null &&
        Number.isNaN(Date.parse(entry.archivedAt))) ||
      gameKeys.has(entry.gameKey)
    ) {
      throw new Error("Invalid competition catalog entry");
    }
    gameKeys.add(entry.gameKey);
  }
  return catalog;
}

function loadDefinition(entry: CompetitionCatalogEntry): CompetitionGameDefinition {
  const manifestSource = readRepoFile(entry.manifestPath);
  if (!manifestSource) {
    throw new Error(`Missing competition manifest ${entry.manifestPath}`);
  }
  const manifest = validateManifest(JSON.parse(manifestSource));
  const gameKey = `${manifest.competitionId}#${manifest.gameVersion}`;
  if (gameKey !== entry.gameKey) {
    throw new Error(`Competition catalog key mismatch for ${entry.manifestPath}`);
  }
  const artifactSource = readRepoFile(manifest.artifactPath);
  if (!artifactSource) {
    throw new Error(`Missing competition artifact ${manifest.artifactPath}`);
  }
  const artifactSha256 = createHash("sha256")
    .update(artifactSource)
    .digest("hex");
  if (artifactSha256 !== manifest.artifactSha256) {
    throw new Error(
      `Competition artifact hash mismatch for ${gameKey}: expected ${manifest.artifactSha256}, got ${artifactSha256}`,
    );
  }
  const round = validateScriptedRound(JSON.parse(artifactSource));
  if (manifest.roundId !== round.id || manifest.roundFormat !== round.format) {
    throw new Error("Competition manifest does not identify its scripted round");
  }
  return { gameKey, manifest, round, archivedAt: entry.archivedAt };
}

export const COMPETITION_CATALOG = validateCatalog(catalogJson);
export const COMPETITION_GAMES = COMPETITION_CATALOG.games.map(loadDefinition);
const currentDefinition = COMPETITION_GAMES.find(
  (game) => game.gameKey === COMPETITION_CATALOG.currentGameKey,
);
if (!currentDefinition) throw new Error("Current competition is not registered");

export const COMPETITION_GAME = currentDefinition.manifest;
export const COMPETITION_ROUND = currentDefinition.round;

export const COMPETITION_GAME_KEY =
  `${COMPETITION_GAME.competitionId}#${COMPETITION_GAME.gameVersion}`;
