import "server-only";

import { createHash } from "node:crypto";
import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import {
  DynamoDBDocumentClient,
  GetCommand,
  PutCommand,
  QueryCommand,
} from "@aws-sdk/lib-dynamodb";
import { COMPETITION_GAME_KEY } from "./game.ts";
import { packColumns, unpackColumns } from "./packing.ts";
import type { CompetitionReplayResult } from "./replay.ts";

export const LEADERBOARD_INDEX = "LeaderboardByScore";
export const PACKED_MOVES_FORMAT = "drop7-columns-3bit-v1";

export interface CompetitionIdentity {
  userId: string;
  provider: string;
  providerAccountId: string;
  displayName: string;
}

export interface CompetitionSubmissionRecord {
  submissionId: string;
  recordType: "validated-score" | "policy-score";
  gameKey: string;
  competitionId: string;
  gameVersion: string;
  roundId: string;
  artifactSha256: string;
  userId: string;
  provider: string;
  providerAccountId: string;
  displayName: string;
  verifiedScore: number;
  clientScore: number;
  scoreMismatch: boolean;
  moveCount: number;
  packedMovesFormat: typeof PACKED_MOVES_FORMAT;
  packedMoves: Uint8Array;
  censored: boolean;
  submittedAt: string;
  validatedAt: string;
  policyId?: string;
  policyFamily?: string;
  policyDescription?: string;
  publicInformation?: boolean;
  researchUrl?: string;
  trajectoryChecksum?: string;
  policySourceRevision?: string;
  policySourceDirty?: boolean;
}

export interface CompetitionLeaderboardEntry {
  submissionId: string;
  kind: "human" | "ai";
  displayName: string;
  provider: string;
  verifiedScore: number;
  moveCount: number;
  scoreMismatch: boolean;
  submittedAt: string;
  policyId: string | null;
  policyFamily: string | null;
  publicInformation: boolean | null;
  researchUrl: string | null;
}

const documentClient = DynamoDBDocumentClient.from(new DynamoDBClient({}), {
  marshallOptions: { removeUndefinedValues: true },
});

export async function storeValidatedSubmission(input: {
  identity: CompetitionIdentity;
  columns: readonly number[];
  clientScore: number;
  replay: CompetitionReplayResult;
  competitionId: string;
  gameVersion: string;
  roundId: string;
  artifactSha256: string;
}): Promise<{ record: CompetitionSubmissionRecord; duplicate: boolean }> {
  const tableName = requiredTableName();
  const packedMoves = packColumns(input.columns);
  const submissionId = createHash("sha256")
    .update(COMPETITION_GAME_KEY)
    .update("\0")
    .update(input.identity.userId)
    .update("\0")
    .update(packedMoves)
    .digest("hex");
  const now = new Date().toISOString();
  const record: CompetitionSubmissionRecord = {
    submissionId,
    recordType: "validated-score",
    gameKey: COMPETITION_GAME_KEY,
    competitionId: input.competitionId,
    gameVersion: input.gameVersion,
    roundId: input.roundId,
    artifactSha256: input.artifactSha256,
    userId: input.identity.userId,
    provider: input.identity.provider,
    providerAccountId: input.identity.providerAccountId,
    displayName: input.identity.displayName.slice(0, 80),
    verifiedScore: input.replay.score,
    clientScore: input.clientScore,
    scoreMismatch: input.clientScore !== input.replay.score,
    moveCount: input.columns.length,
    packedMovesFormat: PACKED_MOVES_FORMAT,
    packedMoves,
    censored: input.replay.censored,
    submittedAt: now,
    validatedAt: now,
  };

  try {
    await documentClient.send(
      new PutCommand({
        TableName: tableName,
        Item: record,
        ConditionExpression: "attribute_not_exists(submissionId)",
      }),
    );
    return { record, duplicate: false };
  } catch (error) {
    if (!isConditionalFailure(error)) throw error;
    const existing = await getSubmissionRecord(submissionId);
    if (!existing) throw error;
    return { record: existing, duplicate: true };
  }
}

export async function loadCompetitionLeaderboard(
  gameKey: string,
  limit = 100,
): Promise<{ available: boolean; entries: CompetitionLeaderboardEntry[] }> {
  const tableName = process.env.DROP7_COMPETITION_TABLE;
  if (!tableName) return { available: false, entries: [] };
  try {
    const response = await documentClient.send(
      new QueryCommand({
        TableName: tableName,
        IndexName: LEADERBOARD_INDEX,
        KeyConditionExpression: "gameKey = :gameKey",
        ExpressionAttributeValues: { ":gameKey": gameKey },
        ScanIndexForward: false,
        Limit: Math.min(250, Math.max(1, Math.trunc(limit))),
      }),
    );
    return {
      available: true,
      entries: (response.Items ?? []).map(toLeaderboardEntry),
    };
  } catch (error) {
    console.error(
      JSON.stringify({
        event: "competition_leaderboard_read_failed",
        gameKey,
        error: error instanceof Error ? error.name : "unknown",
      }),
    );
    return { available: false, entries: [] };
  }
}

export async function loadHumanLeaderboard(limit = 100) {
  const leaderboard = await loadCompetitionLeaderboard(
    COMPETITION_GAME_KEY,
    limit,
  );
  return {
    ...leaderboard,
    entries: leaderboard.entries.filter((entry) => entry.kind === "human"),
  };
}

export async function getSubmissionRecord(
  submissionId: string,
): Promise<CompetitionSubmissionRecord | null> {
  if (!/^[a-f0-9]{64}$/.test(submissionId)) return null;
  const tableName = process.env.DROP7_COMPETITION_TABLE;
  if (!tableName) return null;
  const response = await documentClient.send(
    new GetCommand({ TableName: tableName, Key: { submissionId } }),
  );
  return response.Item
    ? normalizeRecord(response.Item as CompetitionSubmissionRecord)
    : null;
}

export function columnsFromRecord(record: CompetitionSubmissionRecord): number[] {
  if (record.packedMovesFormat !== PACKED_MOVES_FORMAT) {
    throw new Error(`Unsupported packed move format ${record.packedMovesFormat}`);
  }
  return unpackColumns(record.packedMoves, record.moveCount);
}

function requiredTableName() {
  const tableName = process.env.DROP7_COMPETITION_TABLE;
  if (!tableName) throw new Error("DROP7_COMPETITION_TABLE is not configured");
  return tableName;
}

function normalizeRecord(record: CompetitionSubmissionRecord) {
  const packed = record.packedMoves;
  return {
    ...record,
    packedMoves:
      packed instanceof Uint8Array
        ? packed
        : new Uint8Array(packed as unknown as ArrayBuffer),
  };
}

function toLeaderboardEntry(
  item: Record<string, unknown>,
): CompetitionLeaderboardEntry {
  const ai = item.recordType === "policy-score";
  return {
    submissionId: String(item.submissionId),
    kind: ai ? "ai" : "human",
    displayName: String(item.displayName ?? "player"),
    provider: String(item.provider ?? "oauth"),
    verifiedScore: Number(item.verifiedScore),
    moveCount: Number(item.moveCount),
    scoreMismatch: Boolean(item.scoreMismatch),
    submittedAt: String(item.submittedAt),
    policyId: ai && typeof item.policyId === "string" ? item.policyId : null,
    policyFamily:
      ai && typeof item.policyFamily === "string" ? item.policyFamily : null,
    publicInformation:
      ai && typeof item.publicInformation === "boolean"
        ? item.publicInformation
        : null,
    researchUrl: safeResearchUrl(item.researchUrl),
  };
}

function safeResearchUrl(value: unknown) {
  if (typeof value !== "string") return null;
  try {
    const url = new URL(value);
    return url.protocol === "https:" ? url.toString() : null;
  } catch {
    return null;
  }
}

function isConditionalFailure(error: unknown) {
  return (
    error instanceof Error && error.name === "ConditionalCheckFailedException"
  );
}
