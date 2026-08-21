import { auth } from "@/auth";
import {
  COMPETITION_GAME,
  COMPETITION_ROUND,
} from "@/lib/competition/game";
import { storeValidatedSubmission } from "@/lib/competition/ledger";
import { replayCompetitionColumns } from "@/lib/competition/replay";

export const runtime = "nodejs";

interface SubmissionBody {
  competitionId?: unknown;
  gameVersion?: unknown;
  columns?: unknown;
  clientScore?: unknown;
}

export async function POST(request: Request) {
  const expectedOrigin = process.env.DROP7_SITE_URL;
  const origin = request.headers.get("origin");
  if (expectedOrigin && origin !== expectedOrigin) {
    return Response.json({ error: "invalid-origin" }, { status: 403 });
  }

  const session = await auth();
  if (
    !session?.user ||
    session.user.provider !== "github" ||
    !/^\d+$/.test(session.user.providerAccountId)
  ) {
    return Response.json({ error: "authentication-required" }, { status: 401 });
  }

  const contentLength = Number(request.headers.get("content-length") ?? 0);
  if (contentLength > 32_768) {
    return Response.json({ error: "request-too-large" }, { status: 413 });
  }

  let body: SubmissionBody;
  try {
    body = (await request.json()) as SubmissionBody;
  } catch {
    return Response.json({ error: "invalid-json" }, { status: 400 });
  }

  if (
    body.competitionId !== COMPETITION_GAME.competitionId ||
    body.gameVersion !== COMPETITION_GAME.gameVersion ||
    !Array.isArray(body.columns) ||
    !body.columns.every(
      (column) =>
        typeof column === "number" &&
        Number.isInteger(column) &&
        column >= 0 &&
        column <= 6,
    ) ||
    !Number.isSafeInteger(body.clientScore) ||
    (body.clientScore as number) < 0
  ) {
    return Response.json({ error: "invalid-submission" }, { status: 400 });
  }

  const columns = body.columns as number[];
  const replay = replayCompetitionColumns(COMPETITION_ROUND, columns);
  if (!replay.valid) {
    console.warn(
      JSON.stringify({
        event: "competition_submission_rejected",
        userId: session.user.id,
        gameVersion: COMPETITION_GAME.gameVersion,
        failure: replay.failure,
        submittedMoveCount: columns.length,
      }),
    );
    return Response.json(
      { error: "invalid-game", reason: replay.failure },
      { status: 422 },
    );
  }

  const stored = await storeValidatedSubmission({
    identity: {
      userId: session.user.id,
      provider: session.user.provider,
      providerAccountId: session.user.providerAccountId,
      displayName: session.user.handle || session.user.name || "player",
    },
    columns,
    clientScore: body.clientScore as number,
    replay,
    competitionId: COMPETITION_GAME.competitionId,
    gameVersion: COMPETITION_GAME.gameVersion,
    roundId: COMPETITION_GAME.roundId,
    artifactSha256: COMPETITION_GAME.artifactSha256,
  });
  const scoreMismatch = body.clientScore !== replay.score;

  const log = {
    event: scoreMismatch
      ? "competition_score_mismatch"
      : "competition_submission_validated",
    submissionId: stored.record.submissionId,
    userId: session.user.id,
    gameVersion: COMPETITION_GAME.gameVersion,
    clientScore: body.clientScore,
    verifiedScore: replay.score,
    moveCount: columns.length,
    duplicate: stored.duplicate,
  };
  if (scoreMismatch) console.warn(JSON.stringify(log));
  else console.info(JSON.stringify(log));

  return Response.json({
    submissionId: stored.record.submissionId,
    verifiedScore: replay.score,
    clientScore: body.clientScore,
    scoreMismatch,
    duplicate: stored.duplicate,
  });
}
