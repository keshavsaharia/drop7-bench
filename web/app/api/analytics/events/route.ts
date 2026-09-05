import {
  MAX_APP_ANALYTICS_REQUEST_BYTES,
  buildAppAnalyticsRows,
  validateAppAnalyticsBatch,
} from "@/lib/analytics/app-events";
import { deliverAppAnalyticsRows } from "@/lib/analytics/ingest";
import { readLimitedJson } from "@/lib/request-body";

export const runtime = "nodejs";
export const maxDuration = 10;

const CORS_HEADERS = {
  "access-control-allow-origin": "*",
  "access-control-allow-headers": "content-type,x-drop7-client",
  "access-control-allow-methods": "POST,OPTIONS",
};

export function OPTIONS() {
  return new Response(null, { status: 204, headers: CORS_HEADERS });
}

export async function POST(request: Request) {
  if (request.headers.get("x-drop7-client") !== "mobile-app") {
    return response({ error: "invalid-client" }, 403);
  }

  const body = await readLimitedJson(request, MAX_APP_ANALYTICS_REQUEST_BYTES);
  if (!body.ok) return response({ error: body.error }, body.status);
  const validation = validateAppAnalyticsBatch(body.value);
  if (!validation.ok) {
    return response({ error: validation.error }, validation.status);
  }

  const rows = buildAppAnalyticsRows(request, validation.batch);
  try {
    await deliverAppAnalyticsRows(rows);
  } catch (error) {
    console.error(
      JSON.stringify({
        event: "app_analytics_delivery_failed",
        eventCount: rows.length,
        message: error instanceof Error ? error.message : "unknown",
      }),
    );
    return response({ error: "delivery-unavailable" }, 503);
  }

  return response({ accepted: rows.length }, 202);
}

function response(body: unknown, status: number) {
  return Response.json(body, { status, headers: CORS_HEADERS });
}
