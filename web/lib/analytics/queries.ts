import type {
  AnalyticsAudience,
  AnalyticsRange,
  AthenaQueryResult,
  DashboardData,
  DashboardPoint,
  IosDashboardData,
  IosDashboardPoint,
} from "@/lib/analytics/types";

const RANGE_CONFIG: Record<
  AnalyticsRange,
  { durationMs: number; truncation: "hour" | "day" | "week" }
> = {
  "24h": { durationMs: 24 * 60 * 60 * 1000, truncation: "hour" },
  "7d": { durationMs: 7 * 24 * 60 * 60 * 1000, truncation: "day" },
  "30d": { durationMs: 30 * 24 * 60 * 60 * 1000, truncation: "day" },
  "90d": { durationMs: 90 * 24 * 60 * 60 * 1000, truncation: "week" },
};

const BREAKDOWN_SECTIONS = [
  "pages",
  "channels",
  "referrers",
  "countries",
  "devices",
  "browsers",
] as const;

export function isAnalyticsRange(value: unknown): value is AnalyticsRange {
  return typeof value === "string" && value in RANGE_CONFIG;
}

export function isAnalyticsAudience(value: unknown): value is AnalyticsAudience {
  return value === "humans" || value === "all" || value === "bots";
}

export function buildDashboardSql(
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
  const { durationMs, truncation } = RANGE_CONFIG[range];
  const endMs = now.getTime();
  const startMs = endMs - durationMs;
  const audienceClause =
    audience === "humans"
      ? "AND is_bot = false"
      : audience === "bots"
        ? "AND is_bot = true"
        : "";

  return `
WITH filtered AS (
  SELECT
    occurred_at_ms,
    path,
    visitor_id,
    referrer_host,
    referrer_channel,
    country_code,
    device_type,
    browser_family
  FROM "${database}"."${table}"
  WHERE event_name = 'page_view'
    AND occurred_at_ms >= ${startMs}
    AND occurred_at_ms < ${endMs}
    ${audienceClause}
),
summary AS (
  SELECT
    'summary' AS section,
    'All traffic' AS label,
    '' AS bucket,
    count(*) AS views,
    approx_distinct(visitor_id) AS visitors,
    approx_distinct(path) AS paths
  FROM filtered
),
time_series AS (
  SELECT
    'time_series' AS section,
    '' AS label,
    CAST(date_trunc('${truncation}', from_unixtime(occurred_at_ms / 1000.0)) AS varchar) AS bucket,
    count(*) AS views,
    approx_distinct(visitor_id) AS visitors,
    approx_distinct(path) AS paths
  FROM filtered
  GROUP BY 3
),
pages AS (
  SELECT * FROM (
    SELECT
      'pages' AS section,
      coalesce(nullif(path, ''), '(unknown)') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
),
channels AS (
  SELECT * FROM (
    SELECT
      'channels' AS section,
      coalesce(nullif(referrer_channel, ''), 'unknown') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
),
referrers AS (
  SELECT * FROM (
    SELECT
      'referrers' AS section,
      coalesce(nullif(referrer_host, ''), '(direct)') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
),
countries AS (
  SELECT * FROM (
    SELECT
      'countries' AS section,
      coalesce(nullif(country_code, ''), '(unknown)') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
),
devices AS (
  SELECT * FROM (
    SELECT
      'devices' AS section,
      coalesce(nullif(device_type, ''), 'unknown') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
),
browsers AS (
  SELECT * FROM (
    SELECT
      'browsers' AS section,
      coalesce(nullif(browser_family, ''), 'Unknown') AS label,
      '' AS bucket,
      count(*) AS views,
      approx_distinct(visitor_id) AS visitors,
      CAST(0 AS bigint) AS paths
    FROM filtered
    GROUP BY 2
    ORDER BY views DESC
    LIMIT 12
  )
)
SELECT * FROM summary
UNION ALL SELECT * FROM time_series
UNION ALL SELECT * FROM pages
UNION ALL SELECT * FROM channels
UNION ALL SELECT * FROM referrers
UNION ALL SELECT * FROM countries
UNION ALL SELECT * FROM devices
UNION ALL SELECT * FROM browsers
`.trim();
}

export function buildIosDashboardSql(
  range: AnalyticsRange,
  now = new Date(),
): string {
  const database = analyticsIdentifier(
    "DROP7_ANALYTICS_DATABASE",
    process.env.DROP7_ANALYTICS_DATABASE,
  );
  const table = analyticsIdentifier(
    "DROP7_GAME_SUBMISSIONS_TABLE",
    process.env.DROP7_GAME_SUBMISSIONS_TABLE,
  );
  const { durationMs, truncation } = RANGE_CONFIG[range];
  const endMs = now.getTime();
  const startMs = endMs - durationMs;
  return `
WITH ranked_deliveries AS (
  SELECT
    event_id,
    received_at_ms,
    app_version,
    mode,
    verified_score,
    verified_moves,
    row_number() OVER (
      PARTITION BY event_id
      ORDER BY received_at_ms ASC
    ) AS delivery_rank
  FROM "${database}"."${table}"
  WHERE event_name = 'completed_game'
    AND source_application = 'drop7-mobile'
    AND source_platform = 'ios'
),
filtered AS (
  SELECT received_at_ms, app_version, mode, verified_score, verified_moves
  FROM ranked_deliveries
  WHERE delivery_rank = 1
    AND received_at_ms >= ${startMs}
    AND received_at_ms < ${endMs}
),
summary AS (
  SELECT 'summary' AS section, 'All iOS games' AS label, '' AS bucket,
    count(*) AS games, coalesce(sum(verified_moves), 0) AS moves,
    coalesce(avg(verified_score), 0) AS average_score
  FROM filtered
),
time_series AS (
  SELECT 'time_series' AS section, '' AS label,
    CAST(date_trunc('${truncation}', from_unixtime(received_at_ms / 1000.0)) AS varchar) AS bucket,
    count(*) AS games, coalesce(sum(verified_moves), 0) AS moves,
    coalesce(avg(verified_score), 0) AS average_score
  FROM filtered GROUP BY 3
),
modes AS (
  SELECT * FROM (
    SELECT 'modes' AS section, mode AS label, '' AS bucket,
      count(*) AS games, coalesce(sum(verified_moves), 0) AS moves,
      coalesce(avg(verified_score), 0) AS average_score
    FROM filtered GROUP BY 2 ORDER BY games DESC LIMIT 12
  )
),
versions AS (
  SELECT * FROM (
    SELECT 'versions' AS section, app_version AS label, '' AS bucket,
      count(*) AS games, coalesce(sum(verified_moves), 0) AS moves,
      coalesce(avg(verified_score), 0) AS average_score
    FROM filtered GROUP BY 2 ORDER BY games DESC LIMIT 12
  )
)
SELECT * FROM summary
UNION ALL SELECT * FROM time_series
UNION ALL SELECT * FROM modes
UNION ALL SELECT * FROM versions
`.trim();
}

export function buildReadOnlyCustomSql(sql: string): string {
  const trimmed = sql.trim();
  if (!trimmed || trimmed.length > 10_000) {
    throw new Error("Query must contain between 1 and 10,000 characters.");
  }
  if (trimmed.includes(";")) {
    throw new Error("Run one read-only statement at a time, without a semicolon.");
  }

  const withoutComments = trimmed
    .replace(/\/\*[\s\S]*?\*\//g, " ")
    .replace(/--[^\r\n]*/g, " ")
    .trim();
  if (!/^(select|with)\b/i.test(withoutComments)) {
    throw new Error("Only SELECT and WITH queries are allowed.");
  }
  if (
    /\b(insert|update|delete|merge|create|drop|alter|truncate|unload|call|optimize|vacuum|grant|revoke|prepare|execute|msck)\b/i.test(
      withoutComments,
    )
  ) {
    throw new Error("Only read-only analytics queries are allowed.");
  }

  return `SELECT * FROM (${trimmed}) AS admin_query LIMIT 500`;
}

export function parseDashboardResult(result: AthenaQueryResult): DashboardData {
  const points = result.rows.map((row) => ({
    section: row.section ?? "",
    label: row.label ?? "",
    bucket: row.bucket ?? "",
    views: numberValue(row.views),
    visitors: numberValue(row.visitors),
    paths: numberValue(row.paths),
  }));
  const summary = points.find((point) => point.section === "summary") ?? {
    section: "summary",
    label: "All traffic",
    bucket: "",
    views: 0,
    visitors: 0,
    paths: 0,
  };
  const breakdowns: Record<string, DashboardPoint[]> = {};
  for (const section of BREAKDOWN_SECTIONS) {
    breakdowns[section] = points
      .filter((point) => point.section === section)
      .map(withoutSection)
      .sort((left, right) => right.views - left.views);
  }

  return {
    summary: withoutSection(summary),
    timeSeries: points
      .filter((point) => point.section === "time_series")
      .map(withoutSection)
      .sort((left, right) => left.bucket.localeCompare(right.bucket)),
    breakdowns,
    query: {
      queryExecutionId: result.queryExecutionId,
      dataScannedBytes: result.dataScannedBytes,
      engineExecutionMs: result.engineExecutionMs,
    },
  };
}

export function parseIosDashboardResult(result: AthenaQueryResult): IosDashboardData {
  const points = result.rows.map((row) => ({
    section: row.section ?? "",
    label: row.label ?? "",
    bucket: row.bucket ?? "",
    games: numberValue(row.games),
    moves: numberValue(row.moves),
    averageScore: numberValue(row.average_score),
  }));
  const summary = points.find((point) => point.section === "summary") ?? {
    section: "summary",
    label: "All iOS games",
    bucket: "",
    games: 0,
    moves: 0,
    averageScore: 0,
  };
  const clean = (point: typeof summary): IosDashboardPoint => ({
    label: point.label,
    bucket: point.bucket,
    games: point.games,
    moves: point.moves,
    averageScore: point.averageScore,
  });
  return {
    summary: clean(summary),
    timeSeries: points.filter((point) => point.section === "time_series")
      .map(clean).sort((left, right) => left.bucket.localeCompare(right.bucket)),
    breakdowns: {
      modes: points.filter((point) => point.section === "modes")
        .map(clean).sort((left, right) => right.games - left.games),
      versions: points.filter((point) => point.section === "versions")
        .map(clean).sort((left, right) => right.games - left.games),
    },
    query: {
      queryExecutionId: result.queryExecutionId,
      dataScannedBytes: result.dataScannedBytes,
      engineExecutionMs: result.engineExecutionMs,
    },
  };
}

function withoutSection(point: {
  label: string;
  bucket: string;
  views: number;
  visitors: number;
  paths: number;
}): DashboardPoint {
  return {
    label: point.label,
    bucket: point.bucket,
    views: point.views,
    visitors: point.visitors,
    paths: point.paths,
  };
}

function numberValue(value: string | null): number {
  const parsed = Number(value ?? 0);
  return Number.isFinite(parsed) ? parsed : 0;
}

function analyticsIdentifier(name: string, value: string | undefined): string {
  if (!value || !/^[a-z0-9_]+$/i.test(value)) {
    throw new Error(`${name} is not configured with a valid Glue identifier.`);
  }
  return value;
}
