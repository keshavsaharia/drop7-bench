import { auth } from "@/auth";
import { storeValidatedSubmission } from "@/lib/competition/ledger";
import { getCompetitionGame } from "@/lib/competition/registry";
import { replayCompetitionColumns } from "@/lib/competition/replay";
import { readLimitedJson } from "@/lib/request-body";

export const runtime = "nodejs";

const CORS_HEADERS = {
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "POST, OPTIONS",
  "access-control-allow-headers": "content-type,x-drop7-client",
  "access-control-max-age": "86400",
};

interface SubmissionBody {
  competitionId?: unknown;
  gameVersion?: unknown;
  columns?: unknown;
  clientScore?: unknown;
  source?: unknown;
  displayName?: unknown;
  runId?: unknown;
  platform?: unknown;
  appVersion?: unknown;
}

export function OPTIONS() {
  return new Response(null, { status: 204, headers: CORS_HEADERS });
}

export async function POST(request: Request) {
  const parsed = await readLimitedJson(request, 32_768);
  if (!parsed.ok) return json({ error: parsed.error }, { status: parsed.status });
  const body = parsed.value as SubmissionBody;

  if (
    !isSlug(body.competitionId) ||
    !isSlug(body.gameVersion) ||
    !Array.isArray(body.columns) ||
    !body.columns.every(isColumn) ||
    !Number.isSafeInteger(body.clientScore) ||
    (body.clientScore as number) < 0
  ) {
    return json({ error: "invalid-submission" }, { status: 400 });
  }
  const game = getCompetitionGame(`${body.competitionId}#${body.gameVersion}`);
  if (!game) return json({ error: "unknown-competition" }, { status: 404 });

  const mobile = body.source === "mobile-app";
  let identity;
  if (mobile) {
    if (
      !isDisplayName(body.displayName) ||
      !isSafeText(body.runId, 1, 160) ||
      !isPlatform(body.platform) ||
      !isSafeText(body.appVersion, 1, 64)
    ) {
      return json({ error: "invalid-mobile-submission" }, { status: 400 });
    }
    identity = {
      userId: `mobile:${body.runId}`,
      provider: "mobile app",
      providerAccountId: body.runId,
      displayName: body.displayName.trim(),
      sourceApplication: "drop7-mobile",
      sourcePlatform: body.platform,
    };
  } else {
    const expectedOrigin = process.env.DROP7_SITE_URL;
    const origin = request.headers.get("origin");
    if (expectedOrigin && origin !== expectedOrigin) {
      return json({ error: "invalid-origin" }, { status: 403 });
    }
    const session = await auth();
    if (
      !session?.user ||
      session.user.provider !== "github" ||
      !/^\d+$/.test(session.user.providerAccountId)
    ) {
      return json({ error: "authentication-required" }, { status: 401 });
    }
    identity = {
      userId: session.user.id,
      provider: session.user.provider,
      providerAccountId: session.user.providerAccountId,
      displayName: session.user.handle || session.user.name || "player",
      sourceApplication: "drop7-web",
      sourcePlatform: "web",
    };
  }

  const columns = body.columns as number[];
  const replay = replayCompetitionColumns(game.round, columns);
  if (!replay.valid) {
    console.warn(JSON.stringify({
      event: "competition_submission_rejected",
      sourceApplication: identity.sourceApplication,
      gameVersion: game.manifest.gameVersion,
      failure: replay.failure,
      submittedMoveCount: columns.length,
    }));
    return json(
      { error: "invalid-game", reason: replay.failure },
      { status: 422 },
    );
  }
  const stored = await storeValidatedSubmission({
    identity,
    columns,
    clientScore: body.clientScore as number,
    replay,
    gameKey: game.gameKey,
    competitionId: game.manifest.competitionId,
    gameVersion: game.manifest.gameVersion,
    roundId: game.manifest.roundId,
    artifactSha256: game.manifest.artifactSha256,
  });
  const scoreMismatch = body.clientScore !== replay.score;
  const log = {
    event: scoreMismatch
      ? "competition_score_mismatch"
      : "competition_submission_validated",
    submissionId: stored.record.submissionId,
    sourceApplication: identity.sourceApplication,
    gameVersion: game.manifest.gameVersion,
    clientScore: body.clientScore,
    verifiedScore: replay.score,
    moveCount: columns.length,
    duplicate: stored.duplicate,
  };
  if (scoreMismatch) console.warn(JSON.stringify(log));
  else console.info(JSON.stringify(log));
  return json({
    submissionId: stored.record.submissionId,
    verifiedScore: replay.score,
    clientScore: body.clientScore,
    scoreMismatch,
    duplicate: stored.duplicate,
  });
}

function json(body: unknown, init?: ResponseInit) {
  return Response.json(body, {
    ...init,
    headers: { ...CORS_HEADERS, ...init?.headers },
  });
}

function isColumn(value: unknown) {
  return Number.isInteger(value) && (value as number) >= 0 && (value as number) <= 6;
}

function isSlug(value: unknown): value is string {
  return typeof value === "string" && /^[a-z0-9][a-z0-9-]*$/.test(value);
}

function isDisplayName(value: unknown): value is string {
  return isSafeText(value, 1, 40) && value.trim().length > 0;
}

function isSafeText(value: unknown, minimum: number, maximum: number): value is string {
  return typeof value === "string" && value.length >= minimum && value.length <= maximum &&
    !/[\u0000-\u001f\u007f]/.test(value);
}

function isPlatform(value: unknown): value is "ios" | "android" | "web" | "unknown" {
  return value === "ios" || value === "android" || value === "web" || value === "unknown";
}
