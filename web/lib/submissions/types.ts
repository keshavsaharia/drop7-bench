import type { RecordedGameTape } from "../../../src/core/typescript/recorded-game.ts";

export type SubmissionMode = "classic" | "hardcore";
export type SubmissionPlatform = "ios" | "android" | "web" | "unknown";

export interface GameSubmissionBody {
  schemaVersion: 2;
  gameId: string;
  startedAt: string;
  completedAt: string;
  source: {
    application: "drop7-mobile";
    platform: SubmissionPlatform;
    appVersion: string;
  };
  mode: SubmissionMode;
  tape: RecordedGameTape;
  claimedScore: number;
  claimedLevel: number;
  claimedMoves: number;
}

export interface ValidatedGameSubmission {
  event_id: string;
  event_name: "completed_game";
  schema_version: 2;
  received_at: string;
  received_at_ms: number;
  game_id: string;
  started_at: string;
  completed_at: string;
  source_application: "drop7-mobile";
  source_platform: SubmissionPlatform;
  app_version: string;
  mode: SubmissionMode;
  ruleset: string;
  verified_score: number;
  verified_level: number;
  verified_moves: number;
  tape_json: string;
  stage: string;
}
