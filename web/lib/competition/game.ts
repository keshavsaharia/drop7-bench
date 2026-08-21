import roundJson from "../../../src/bench/rounds/gauntlet-01.json" with { type: "json" };
import { validateScriptedRound } from "../../../src/bench/rounds.ts";
import manifestJson from "../../content/competition/global-2026-08-v1.json" with { type: "json" };

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

function validateManifest(value: unknown): CompetitionGameManifest {
  const manifest = value as CompetitionGameManifest;
  if (
    !manifest ||
    manifest.format !== "drop7-competition-game-v1" ||
    !/^[a-z0-9][a-z0-9-]*$/.test(manifest.competitionId) ||
    !/^[a-z0-9][a-z0-9-]*$/.test(manifest.gameVersion) ||
    !/^[a-f0-9]{64}$/.test(manifest.artifactSha256) ||
    manifest.evidenceClass !== "scripted-playground"
  ) {
    throw new Error("Invalid competition game manifest");
  }
  return manifest;
}

export const COMPETITION_GAME = validateManifest(manifestJson);
export const COMPETITION_ROUND = validateScriptedRound(roundJson);

if (
  COMPETITION_GAME.roundId !== COMPETITION_ROUND.id ||
  COMPETITION_GAME.roundFormat !== COMPETITION_ROUND.format
) {
  throw new Error("Competition manifest does not identify its scripted round");
}

export const COMPETITION_GAME_KEY =
  `${COMPETITION_GAME.competitionId}#${COMPETITION_GAME.gameVersion}` as const;
