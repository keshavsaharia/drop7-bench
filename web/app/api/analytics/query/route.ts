import { auth } from "@/auth";
import { isAnalyticsAdmin } from "@/lib/analytics/admin";
import { runAthenaQuery } from "@/lib/analytics/athena";
import {
  buildDashboardSql,
  buildIosDashboardSql,
  buildReadOnlyCustomSql,
  isAnalyticsAudience,
  isAnalyticsRange,
  parseDashboardResult,
  parseIosDashboardResult,
} from "@/lib/analytics/queries";
import { readLimitedJson } from "@/lib/request-body";
import {
  buildNavigationSql,
  parseNavigationResult,
} from "@/lib/analytics/navigation-query";

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
  if (!session?.user) {
    return Response.json(
      { error: "Your session expired. Sign in again to view analytics." },
      { status: 401 },
    );
  }
  if (!isAnalyticsAdmin(session?.user)) {
    return Response.json({ error: "not-found" }, { status: 404 });
  }

  const expectedOrigin = process.env.DROP7_SITE_URL;
  const origin = request.headers.get("origin");
  if (expectedOrigin && origin !== expectedOrigin) {
    return Response.json({ error: "invalid-origin" }, { status: 403 });
  }

  const parsed = await readLimitedJson(request, 16_384);
  if (!parsed.ok) {
    return Response.json({ error: parsed.error }, { status: parsed.status });
  }
  const body = parsed.value as QueryBody;
  if (!body || typeof body !== "object" || Array.isArray(body)) {
    return Response.json({ error: "invalid-query" }, { status: 400 });
  }

  try {
    const now = new Date();
    if (body.mode === "navigation") {
      if (
        !isAnalyticsRange(body.range) ||
        !isAnalyticsAudience(body.audience)
      ) {
        return Response.json(
          { error: "invalid-navigation-query" },
          { status: 400 },
        );
      }
      const result = await runAthenaQuery(
        buildNavigationSql(body.range, body.audience, now),
      );
      return Response.json({ data: parseNavigationResult(result) });
    }
    if (body.mode === "dashboard") {
      if (
        !isAnalyticsRange(body.range) ||
        !isAnalyticsAudience(body.audience)
      ) {
        return Response.json(
          { error: "invalid-dashboard-query" },
          { status: 400 },
        );
      }
      const result = await runAthenaQuery(
        buildDashboardSql(body.range, body.audience, now),
      );
      return Response.json({
        data: parseDashboardResult(result, body.range, now),
      });
    }

    if (body.mode === "ios-dashboard") {
      if (!isAnalyticsRange(body.range)) {
        return Response.json(
          { error: "invalid-ios-dashboard-query" },
          { status: 400 },
        );
      }
      const result = await runAthenaQuery(
        buildIosDashboardSql(body.range, now),
      );
      return Response.json({
        data: parseIosDashboardResult(result, body.range, now),
      });
    }

    if (body.mode === "custom" && typeof body.sql === "string") {
      const result = await runAthenaQuery(buildReadOnlyCustomSql(body.sql));
      return Response.json({ data: result });
    }

    return Response.json({ error: "invalid-query" }, { status: 400 });
  } catch (error) {
    const message =
      error instanceof Error ? error.message : "Analytics query failed.";
    const status = /allowed|characters|configured|statement/i.test(message)
      ? 400
      : 502;
    console.warn(JSON.stringify({ event: "analytics_query_failed", message }));
    return Response.json({ error: message }, { status });
  }
}
