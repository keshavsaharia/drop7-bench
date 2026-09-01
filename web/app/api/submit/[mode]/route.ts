import { deliverGameSubmission } from "@/lib/submissions/delivery";
import type { SubmissionMode } from "@/lib/submissions/types";
import { validateGameSubmission } from "@/lib/submissions/validation";
import { readLimitedJson } from "@/lib/request-body";

export const runtime = "nodejs";
export const maxDuration = 20;

const CORS_HEADERS = {
  "access-control-allow-origin": "*",
  "access-control-allow-headers": "content-type,x-drop7-client",
  "access-control-allow-methods": "POST,OPTIONS",
};

export function OPTIONS() {
  return new Response(null, { status: 204, headers: CORS_HEADERS });
}

export async function POST(
  request: Request,
  { params }: { params: Promise<{ mode: string }> },
) {
  const { mode } = await params;
  if (mode !== "classic" && mode !== "hardcore") {
    return response({ error: "unknown-mode" }, 404);
  }
  const body = await readLimitedJson(request, 512_000);
  if (!body.ok) return response({ error: body.error }, body.status);
  const validation = validateGameSubmission(body.value, mode as SubmissionMode);
  if (!validation.ok) {
    return response({ error: validation.error }, validation.status);
  }

  try {
    await deliverGameSubmission(validation.submission);
  } catch (error) {
    console.error(JSON.stringify({
      event: "game_submission_delivery_failed",
      submissionId: validation.submission.event_id,
      message: error instanceof Error ? error.message : "unknown",
    }));
    return response({ error: "delivery-unavailable" }, 503);
  }
  return response({
    submissionId: validation.submission.event_id,
    verifiedScore: validation.submission.verified_score,
    verifiedLevel: validation.submission.verified_level,
    verifiedMoves: validation.submission.verified_moves,
  }, 202);
}

function response(body: unknown, status: number) {
  return Response.json(body, { status, headers: CORS_HEADERS });
}
