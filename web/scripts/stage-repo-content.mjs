import { createHash } from "node:crypto";
import {
  cpSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptsDir = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(scriptsDir, "..");
const repoRoot = resolve(webRoot, "..");
const stagedRoot = join(webRoot, "build", "repo");
const competitionCatalogPath = join(
  webRoot,
  "content",
  "competition",
  "catalog.json",
);
const competitionCatalog = JSON.parse(readFileSync(competitionCatalogPath, "utf8"));
const competitionArtifacts = competitionCatalog.games.map((entry) => {
  const manifestPath = resolve(repoRoot, entry.manifestPath);
  if (!manifestPath.startsWith(`${repoRoot}/`)) {
    throw new Error("Competition manifest must stay inside the repository");
  }
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  const expectedGameKey = `${manifest.competitionId}#${manifest.gameVersion}`;
  if (entry.gameKey !== expectedGameKey) {
    throw new Error(`Competition catalog key mismatch for ${entry.manifestPath}`);
  }
  const artifactPath = resolve(repoRoot, manifest.artifactPath);
  if (!artifactPath.startsWith(`${repoRoot}/`)) {
    throw new Error("Competition artifact must stay inside the repository");
  }
  const artifactSha256 = createHash("sha256")
    .update(readFileSync(artifactPath))
    .digest("hex");
  if (artifactSha256 !== manifest.artifactSha256) {
    throw new Error(
      `Competition artifact hash mismatch: expected ${manifest.artifactSha256}, got ${artifactSha256}`,
    );
  }
  return {
    gameKey: entry.gameKey,
    manifest,
    artifactPath,
    round: JSON.parse(readFileSync(artifactPath, "utf8")),
  };
});

const currentCompetition = competitionArtifacts.find(
  (game) => game.gameKey === competitionCatalog.currentGameKey,
);
if (!currentCompetition) {
  throw new Error("Current competition is not registered");
}

const publicCompetitionRoot = join(webRoot, "public", "competition");
rmSync(publicCompetitionRoot, { recursive: true, force: true });
mkdirSync(publicCompetitionRoot, { recursive: true });
writeFileSync(
  join(publicCompetitionRoot, "current.json"),
  `${JSON.stringify({
    format: "drop7-competition-current-v1",
    gameKey: currentCompetition.gameKey,
    manifest: currentCompetition.manifest,
    round: currentCompetition.round,
  })}\n`,
);
writeFileSync(
  join(publicCompetitionRoot, "catalog.json"),
  `${JSON.stringify({
    format: "drop7-competition-public-catalog-v1",
    currentGameKey: competitionCatalog.currentGameKey,
    games: competitionArtifacts.map((game) => ({
      format: "drop7-competition-current-v1",
      gameKey: game.gameKey,
      manifest: game.manifest,
      round: game.round,
    })),
  })}\n`,
);

rmSync(stagedRoot, { recursive: true, force: true });
mkdirSync(stagedRoot, { recursive: true });

for (const directory of ["approaches", "docs", "research", "src"]) {
  cpSync(join(repoRoot, directory), join(stagedRoot, directory), {
    recursive: true,
  });
}

const stagedWebRoot = join(stagedRoot, "web");
mkdirSync(stagedWebRoot, { recursive: true });
for (const contentDir of ["competition", "figures"]) {
  const source = join(webRoot, "content", contentDir);
  if (!existsSync(source)) continue;
  cpSync(source, join(stagedWebRoot, "content", contentDir), { recursive: true });
}
for (const { manifest, artifactPath } of competitionArtifacts) {
  const stagedArtifactPath = join(stagedRoot, manifest.artifactPath);
  mkdirSync(dirname(stagedArtifactPath), { recursive: true });
  cpSync(artifactPath, stagedArtifactPath);
}

const dataRoot = join(webRoot, "data");
if (existsSync(dataRoot)) {
  cpSync(dataRoot, join(stagedWebRoot, "data"), { recursive: true });
}
