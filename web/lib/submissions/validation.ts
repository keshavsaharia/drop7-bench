import { createHash } from "node:crypto";
import {
  CLASSIC_RULESET,
  HARDCORE_RULESET,
  evaluateRecordedGameTape,
  isRecordedGameTape,
} from "../../../src/core/typescript/recorded-game.ts";
import type {
  GameSubmissionBody,
  SubmissionMode,
  SubmissionPlatform,
  ValidatedGameSubmission,
} from "./types.ts";

export type SubmissionValidation =
  | { ok: true; submission: ValidatedGameSubmission }
  | { ok: false; error: string; status: number };

const PLATFORMS = new Set<SubmissionPlatform>([
  "ios",
  "android",
  "web",
  "unknown",
]);

export function validateGameSubmission(
  value: unknown,
  routeMode: SubmissionMode,
  now = new Date(),
): SubmissionValidation {
  if (!value || typeof value !== "object") return invalid("invalid-submission");
  const body = value as Partial<GameSubmissionBody>;
  if (
    body.schemaVersion !== 2 ||
    body.mode !== routeMode ||
    !isSafeText(body.gameId, 1, 160) ||
    !isTimestamp(body.startedAt) ||
    !isTimestamp(body.completedAt) ||
    Date.parse(body.completedAt) < Date.parse(body.startedAt) ||
    !body.source ||
    body.source.application !== "drop7-mobile" ||
    !PLATFORMS.has(body.source.platform as SubmissionPlatform) ||
    !isSafeText(body.source.appVersion, 1, 64) ||
    !Number.isSafeInteger(body.claimedScore) ||
    (body.claimedScore as number) < 0 ||
    !Number.isSafeInteger(body.claimedLevel) ||
    (body.claimedLevel as number) < 1 ||
    !Number.isSafeInteger(body.claimedMoves) ||
    (body.claimedMoves as number) < 1 ||
    !isRecordedGameTape(body.tape)
  ) {
    return invalid("invalid-submission");
  }

  const expectedRuleset = routeMode === "classic" ? CLASSIC_RULESET : HARDCORE_RULESET;
  if (body.tape.ruleset !== expectedRuleset) return invalid("ruleset-mismatch");
  const replay = evaluateRecordedGameTape(body.tape);
  if (!replay.valid) {
    return { ok: false, error: replay.failure ?? "invalid-game", status: 422 };
  }
  if (
    replay.score !== body.claimedScore ||
    replay.level !== body.claimedLevel ||
    replay.moves !== body.claimedMoves
  ) {
    return { ok: false, error: "result-mismatch", status: 422 };
  }

  const tapeJson = JSON.stringify(body.tape);
  const eventId = createHash("sha256")
    .update(routeMode)
    .update("\0")
    .update(body.gameId)
    .update("\0")
    .update(tapeJson)
    .digest("hex");
  return {
    ok: true,
    submission: {
      event_id: eventId,
      event_name: "completed_game",
      schema_version: 2,
      received_at: now.toISOString(),
      received_at_ms: now.getTime(),
      game_id: body.gameId,
      started_at: body.startedAt,
      completed_at: body.completedAt,
      source_application: "drop7-mobile",
      source_platform: body.source.platform,
      app_version: body.source.appVersion,
      mode: routeMode,
      ruleset: body.tape.ruleset,
      verified_score: replay.score,
      verified_level: replay.level,
      verified_moves: replay.moves,
      tape_json: tapeJson,
      stage: (process.env.DROP7_STAGE ?? "local").slice(0, 32),
    },
  };
}

function invalid(error: string): SubmissionValidation {
  return { ok: false, error, status: 400 };
}

function isTimestamp(value: unknown): value is string {
  if (
    typeof value !== "string" ||
    !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(value)
  ) return false;
  try {
    return new Date(value).toISOString() === value;
  } catch {
    return false;
  }
}

function isSafeText(value: unknown, minimum: number, maximum: number): value is string {
  return typeof value === "string" && value.length >= minimum &&
    value.length <= maximum && !/[\u0000-\u001f\u007f]/.test(value);
}
