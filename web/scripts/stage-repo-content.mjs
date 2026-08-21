import { createHash } from "node:crypto";
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptsDir = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(scriptsDir, "..");
const repoRoot = resolve(webRoot, "..");
const stagedRoot = join(webRoot, "build", "repo");
const competitionManifestPath = join(
  webRoot,
  "content",
  "competition",
  "global-2026-08-v1.json",
);

const competitionManifest = JSON.parse(
  readFileSync(competitionManifestPath, "utf8"),
);
const competitionArtifactPath = resolve(
  repoRoot,
  competitionManifest.artifactPath,
);
if (!competitionArtifactPath.startsWith(`${repoRoot}/`)) {
  throw new Error("Competition artifact must stay inside the repository");
}
const competitionArtifactSha256 = createHash("sha256")
  .update(readFileSync(competitionArtifactPath))
  .digest("hex");
if (competitionArtifactSha256 !== competitionManifest.artifactSha256) {
  throw new Error(
    `Competition artifact hash mismatch: expected ${competitionManifest.artifactSha256}, got ${competitionArtifactSha256}`,
  );
}

rmSync(stagedRoot, { recursive: true, force: true });
mkdirSync(stagedRoot, { recursive: true });

for (const directory of ["approaches", "docs", "research"]) {
  cpSync(join(repoRoot, directory), join(stagedRoot, directory), {
    recursive: true,
  });
}

const stagedWebRoot = join(stagedRoot, "web");
mkdirSync(stagedWebRoot, { recursive: true });

const dataRoot = join(webRoot, "data");
if (existsSync(dataRoot)) {
  cpSync(dataRoot, join(stagedWebRoot, "data"), { recursive: true });
}
