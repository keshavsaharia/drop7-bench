import { analyticsIdentifier, RANGE_CONFIG } from "./queries.ts";
import type {
  AnalyticsAudience,
  AnalyticsRange,
  AthenaQueryResult,
} from "./types.ts";
import type { NavigationData } from "./navigation.ts";

/** Aggregate in Athena. Neither visitor IDs nor individual event rows leave it. */
export function buildNavigationSql(
  range: AnalyticsRange,
  audience: AnalyticsAudience,
  now = new Date(),
): string {
  const database = analyticsIdentifier(
    "DROP7_ANALYTICS_DATABASE",
    process.env.DROP7_ANALYTICS_DATABASE,
  );
  const table = analyticsIdentifier(
    "DROP7_ANALYTICS_TABLE",
    process.env.DROP7_ANALYTICS_TABLE,
  );
  const end = now.getTime();
  const start = end - RANGE_CONFIG[range].durationMs;
  const audienceClause =
    audience === "humans"
      ? "AND is_bot = false"
      : audience === "bots"
        ? "AND is_bot = true"
        : "";
  return `
WITH deliveries AS (
  SELECT event_id, occurred_at_ms, path, visitor_id,
    row_number() OVER (PARTITION BY event_id ORDER BY occurred_at_ms, path) AS delivery_rank
  FROM "${database}"."${table}"
  WHERE event_name = 'page_view'
    AND occurred_at_ms >= ${start} AND occurred_at_ms < ${end}
    AND nullif(visitor_id, '') IS NOT NULL AND nullif(path, '') IS NOT NULL
    ${audienceClause}
),
previous AS (
  SELECT *, lag(occurred_at_ms) OVER (
    PARTITION BY visitor_id ORDER BY occurred_at_ms, event_id
  ) AS previous_ms
  FROM deliveries WHERE delivery_rank = 1
),
session_numbers AS (
  SELECT *, sum(CASE WHEN previous_ms IS NULL OR occurred_at_ms - previous_ms >= 1800000
    THEN 1 ELSE 0 END) OVER (
      PARTITION BY visitor_id ORDER BY occurred_at_ms, event_id ROWS UNBOUNDED PRECEDING
    ) AS session_number
  FROM previous
),
steps AS (
  SELECT *, row_number() OVER (
    PARTITION BY visitor_id, session_number ORDER BY occurred_at_ms, event_id
  ) AS step_number, count(*) OVER (PARTITION BY visitor_id, session_number) AS page_count
  FROM session_numbers
),
sessions AS (
  SELECT visitor_id, session_number,
    json_format(CAST(array_agg(path ORDER BY step_number) AS JSON)) AS path_json,
    max(page_count) AS page_count
  FROM steps WHERE step_number <= 6 GROUP BY visitor_id, session_number
),
paths AS (
  SELECT 'path' AS section, path_json, count(*) AS sessions,
    count_if(page_count <= 6) AS ended_sessions
  FROM sessions GROUP BY path_json ORDER BY sessions DESC, path_json ASC LIMIT 400
)
SELECT 'summary' AS section, '' AS path_json, count(*) AS sessions,
  CAST(0 AS bigint) AS ended_sessions FROM sessions
UNION ALL SELECT * FROM paths
`.trim();
}

export function parseNavigationResult(
  result: AthenaQueryResult,
): NavigationData {
  const paths = result.rows
    .filter((row) => row.section === "path")
    .map((row) => {
      const pages: unknown = JSON.parse(row.path_json ?? "null");
      const sessions = Number(row.sessions);
      const endedSessions = Number(row.ended_sessions);
      if (
        !Array.isArray(pages) ||
        pages.length < 1 ||
        pages.length > 6 ||
        !pages.every(
          (page) => typeof page === "string" && page.startsWith("/"),
        ) ||
        !Number.isSafeInteger(sessions) ||
        sessions < 1 ||
        !Number.isSafeInteger(endedSessions) ||
        endedSessions < 0 ||
        endedSessions > sessions
      ) {
        throw new Error("Unexpected navigation query result.");
      }
      return { pages: pages as string[], sessions, endedSessions };
    });
  const totalSessions = Number(
    result.rows.find((row) => row.section === "summary")?.sessions,
  );
  const representedSessions = paths.reduce(
    (total, path) => total + path.sessions,
    0,
  );
  if (
    !Number.isSafeInteger(totalSessions) ||
    totalSessions < representedSessions
  ) {
    throw new Error("Navigation session totals are missing or inconsistent.");
  }
  return {
    paths,
    totalSessions,
    representedSessions,
    query: {
      queryExecutionId: result.queryExecutionId,
      dataScannedBytes: result.dataScannedBytes,
      engineExecutionMs: result.engineExecutionMs,
    },
  };
}
