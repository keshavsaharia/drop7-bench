import { auth } from "@/auth";
import { isAnalyticsAdmin } from "@/lib/analytics/admin";
import { runAthenaQuery } from "@/lib/analytics/athena";
import {
  buildDashboardSql,
  buildReadOnlyCustomSql,
  isAnalyticsAudience,
  isAnalyticsRange,
  parseDashboardResult,
} from "@/lib/analytics/queries";

export const runtime = "nodejs";
export const maxDuration = 20;

interface QueryBody {
  mode?: unknown;
  range?: unknown;
  audience?: unknown;
  sql?: unknown;
}

export async function POST(request: Request) {
  const session = await auth();
  if (!isAnalyticsAdmin(session?.user)) {
    return Response.json({ error: "not-found" }, { status: 404 });
  }

  const expectedOrigin = process.env.DROP7_SITE_URL;
  const origin = request.headers.get("origin");
  if (expectedOrigin && origin !== expectedOrigin) {
    return Response.json({ error: "invalid-origin" }, { status: 403 });
  }

  const contentLength = Number(request.headers.get("content-length") ?? 0);
  if (contentLength > 16_384) {
    return Response.json({ error: "request-too-large" }, { status: 413 });
  }

  let body: QueryBody;
  try {
    body = (await request.json()) as QueryBody;
  } catch {
    return Response.json({ error: "invalid-json" }, { status: 400 });
  }

  try {
    if (body.mode === "dashboard") {
      if (!isAnalyticsRange(body.range) || !isAnalyticsAudience(body.audience)) {
        return Response.json({ error: "invalid-dashboard-query" }, { status: 400 });
      }
      const result = await runAthenaQuery(
        buildDashboardSql(body.range, body.audience),
      );
      return Response.json({ data: parseDashboardResult(result) });
    }

    if (body.mode === "custom" && typeof body.sql === "string") {
      const result = await runAthenaQuery(buildReadOnlyCustomSql(body.sql));
      return Response.json({ data: result });
    }

    return Response.json({ error: "invalid-query" }, { status: 400 });
  } catch (error) {
    const message = error instanceof Error ? error.message : "Analytics query failed.";
    const status = /allowed|characters|configured|statement/i.test(message) ? 400 : 502;
    console.warn(JSON.stringify({ event: "analytics_query_failed", message }));
    return Response.json({ error: message }, { status });
  }
}
