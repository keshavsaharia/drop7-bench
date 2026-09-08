"use client";

import { useId, useMemo, useState } from "react";
import type { FormEvent } from "react";
import { AnalyticsNavigation } from "./analytics/AnalyticsNavigation";
import type {
  AnalyticsAudience,
  AnalyticsRange,
  AthenaQueryResult,
  DashboardData,
  DashboardPoint,
  IosDashboardData,
} from "@/lib/analytics/types";
import {
  analyticsQuery,
  useAnalyticsQuery,
  PanelHeading,
  QueryFootnote,
  ErrorNotice,
  EmptyState,
  percentage,
} from "./analytics/shared";
import "./analytics/analytics.css";

const RANGES: { value: AnalyticsRange; label: string }[] = [
  { value: "24h", label: "Last 24 hours" },
  { value: "7d", label: "Last 7 days" },
  { value: "30d", label: "Last 30 days" },
  { value: "90d", label: "Last 90 days" },
];
const BREAKDOWNS = [
  ["pages", "Popular pages", "Where people spend their page views"],
  ["referrers", "Referrers", "Sites that send traffic here"],
  ["channels", "Acquisition channels", "How people discover the site"],
  ["countries", "Countries", "Traffic by country"],
  ["devices", "Devices", "The screens people use"],
  ["browsers", "Browsers", "Browser families"],
  ["systems", "Operating systems", "Platforms behind the visits"],
  ["agents", "User agents", "Reported browser and client strings"],
];
const DEFAULT_SQL = `SELECT
  date(from_unixtime(occurred_at_ms / 1000.0)) AS day,
  count(*) AS page_views,
  approx_distinct(visitor_id) AS visitors
FROM page_views
WHERE event_name = 'page_view'
  AND is_bot = false
  AND occurred_at_ms >= CAST(to_unixtime(current_timestamp - INTERVAL '7' DAY) * 1000 AS bigint)
GROUP BY 1
ORDER BY 1 DESC
LIMIT 30`;

export function AnalyticsDashboard() {
  const [view, setView] = useState<"site" | "ios">("site");
  const [range, setRange] = useState<AnalyticsRange>("30d");
  const [audience, setAudience] = useState<AnalyticsAudience>("humans");
  const [refresh, setRefresh] = useState(0);
  return (
    <div className="analytics-dashboard">
      <header className="analytics-heading">
        <div>
          <p className="analytics-eyebrow">Workspace / Analytics</p>
          <h1>{view === "site" ? "Site analytics" : "iOS app analytics"}</h1>
          <p>
            {view === "site"
              ? "Understand who visits, what gets read, and where people go next."
              : "Completed games and play activity from validated iOS submissions."}
          </p>
        </div>
        <span className="analytics-private">
          <span aria-hidden="true">◈</span> Admin access
        </span>
      </header>
      <div className="analytics-toolbar">
        <div
          className="analytics-tabs"
          role="group"
          aria-label="Analytics view"
        >
          <button
            aria-pressed={view === "site"}
            onClick={() => setView("site")}
          >
            Website
          </button>
          <button aria-pressed={view === "ios"} onClick={() => setView("ios")}>
            iOS app
          </button>
        </div>
        <div className="analytics-filters">
          {view === "site" && (
            <label className="analytics-filter">
              <span>Audience</span>
              <select
                aria-label="Audience"
                value={audience}
                onChange={(e) =>
                  setAudience(e.target.value as AnalyticsAudience)
                }
              >
                <option value="humans">People only</option>
                <option value="all">People and bots</option>
                <option value="bots">Bots only</option>
              </select>
            </label>
          )}
          <label className="analytics-filter">
            <span>Period</span>
            <select
              aria-label="Period"
              value={range}
              onChange={(e) => setRange(e.target.value as AnalyticsRange)}
            >
              {RANGES.map(({ value, label }) => (
                <option key={value} value={value}>
                  {label}
                </option>
              ))}
            </select>
          </label>
          <button
            className="analytics-button"
            onClick={() => setRefresh((n) => n + 1)}
          >
            <span aria-hidden="true">↻</span> Refresh
          </button>
        </div>
      </div>
      <div className="analytics-freshness">
        <span className="analytics-status-dot" /> First-party data{" "}
        <span>Usually about 5 minutes behind · All times UTC</span>
      </div>
      {view === "site" ? (
        <SiteContent
          key={`site-${range}-${audience}-${refresh}`}
          range={range}
          audience={audience}
        />
      ) : (
        <IosContent key={`ios-${range}-${refresh}`} range={range} />
      )}
      <SqlWorkspace />
    </div>
  );
}

function SiteContent({
  range,
  audience,
}: {
  range: AnalyticsRange;
  audience: AnalyticsAudience;
}) {
  const { data, error } = useAnalyticsQuery<DashboardData>({
    mode: "dashboard",
    range,
    audience,
  });
  if (error) return <ErrorNotice message={error} />;
  if (!data) return <DashboardSkeleton />;
  return (
    <>
      <section className="analytics-metrics" aria-label="Traffic summary">
        <MetricCard
          label="Page views"
          value={data.summary.views}
          note="Total recorded page views"
          icon="↗"
        />
        <MetricCard
          label="Visitors"
          value={data.summary.visitors}
          note="Approximate unique visitors"
          icon="◎"
        />
        <MetricCard
          label="Pages viewed"
          value={data.summary.paths}
          note="Approximate unique paths"
          icon="▤"
        />
      </section>
      <section className="analytics-card analytics-traffic">
        <PanelHeading
          title="Traffic over time"
          description="Page views across the selected period"
        >
          <span className="analytics-legend">Page views</span>
        </PanelHeading>
        <TimeSeriesChart points={data.timeSeries} metricLabel="page views" />
      </section>
      <AnalyticsNavigation range={range} audience={audience} />
      <div className="analytics-section-heading">
        <h2>Traffic breakdown</h2>
        <span>Ranked by page views · Top 40 per category</span>
      </div>
      <section className="analytics-breakdowns" aria-label="Traffic breakdown">
        {BREAKDOWNS.map(([key, title, description]) => (
          <BreakdownCard
            key={key}
            title={title}
            description={description}
            points={data.breakdowns[key] ?? []}
            total={data.summary.views}
            category={key}
          />
        ))}
      </section>
      <QueryFootnote query={data.query} />
    </>
  );
}

function IosContent({ range }: { range: AnalyticsRange }) {
  const { data, error } = useAnalyticsQuery<IosDashboardData>({
    mode: "ios-dashboard",
    range,
  });
  if (error) return <ErrorNotice message={error} />;
  if (!data) return <DashboardSkeleton />;
  const convert = (point: IosDashboardData["summary"]): DashboardPoint => ({
    label: point.label,
    bucket: point.bucket,
    views: point.games,
    visitors: point.moves,
    paths: point.averageScore,
  });
  return (
    <>
      <section className="analytics-metrics" aria-label="App summary">
        <MetricCard
          label="Completed games"
          value={data.summary.games}
          note="Server-validated submissions"
          icon="▦"
        />
        <MetricCard
          label="Moves collected"
          value={data.summary.moves}
          note="Moves in completed games"
          icon="↗"
        />
        <MetricCard
          label="Average score"
          value={Math.round(data.summary.averageScore)}
          note="Across completed games"
          icon="◎"
        />
      </section>
      <section className="analytics-card analytics-traffic">
        <PanelHeading
          title="Completed games over time"
          description="Validated iOS submissions across the selected period"
        />
        <TimeSeriesChart
          points={data.timeSeries.map(convert)}
          metricLabel="completed games"
        />
      </section>
      <section className="analytics-breakdowns">
        <BreakdownCard
          title="Game modes"
          description="Completed games by mode"
          points={(data.breakdowns.modes ?? []).map(convert)}
          total={data.summary.games}
          metric="Games"
        />
        <BreakdownCard
          title="App versions"
          description="Completed games by release"
          points={(data.breakdowns.versions ?? []).map(convert)}
          total={data.summary.games}
          metric="Games"
        />
      </section>
      <QueryFootnote query={data.query} />
    </>
  );
}

function MetricCard({
  label,
  value,
  note,
  icon,
}: {
  label: string;
  value: number;
  note: string;
  icon: string;
}) {
  return (
    <article className="analytics-card analytics-metric">
      <div>
        <span>{label}</span>
        <span className="analytics-metric-icon" aria-hidden="true">
          {icon}
        </span>
      </div>
      <strong>{value.toLocaleString("en")}</strong>
      <p>{note}</p>
    </article>
  );
}

function TimeSeriesChart({
  points,
  metricLabel,
}: {
  points: DashboardPoint[];
  metricLabel: string;
}) {
  const id = useId();
  const max = Math.max(1, ...points.map((p) => p.views));
  if (!points.length || points.every((point) => point.views === 0))
    return <EmptyState label={`No ${metricLabel} in this period.`} />;
  const dates = points.map((point) =>
    Date.parse(`${point.bucket.replace(/Z$/, "")}Z`),
  );
  const span = dates[dates.length - 1] - dates[0];
  const coordinates = points.map((point, i) => ({
    x: 52 + (span > 0 ? (dates[i] - dates[0]) / span : 0.5) * 1020,
    y: 210 - (point.views / max) * 182,
    point,
  }));
  const line = coordinates.map(({ x, y }) => `${x},${y}`).join(" ");
  return (
    <>
      <div className="analytics-chart">
        <svg
          viewBox="0 0 1100 252"
          role="img"
          aria-label={`${metricLabel} over time. Exact values are available in the table below.`}
        >
          <defs>
            <linearGradient id={id} x1="0" y1="0" x2="0" y2="1">
              <stop
                offset="0%"
                stopColor="var(--color-series-1)"
                stopOpacity="0.24"
              />
              <stop
                offset="100%"
                stopColor="var(--color-series-1)"
                stopOpacity="0.02"
              />
            </linearGradient>
          </defs>
          {[0, 0.25, 0.5, 0.75, 1].map((fraction) => (
            <g key={fraction}>
              <line
                x1="52"
                x2="1072"
                y1={210 - fraction * 182}
                y2={210 - fraction * 182}
                stroke="var(--color-rule)"
                strokeDasharray="3 5"
              />
              <text
                x="40"
                y={214 - fraction * 182}
                textAnchor="end"
                fill="var(--color-ink-3)"
                fontSize="11"
              >
                {Math.round(max * fraction).toLocaleString("en")}
              </text>
            </g>
          ))}
          <polygon
            points={`${coordinates[0].x},210 ${line} ${coordinates.at(-1)!.x},210`}
            fill={`url(#${id})`}
          />
          <polyline
            points={line}
            fill="none"
            stroke="var(--color-series-1)"
            strokeWidth="2.5"
            strokeLinejoin="round"
          />
          {coordinates.map(({ x, y, point }) => (
            <circle
              key={point.bucket}
              cx={x}
              cy={y}
              r="3"
              fill="var(--color-series-1)"
            >
              <title>{`${formatBucket(point.bucket)}: ${point.views.toLocaleString("en")} ${metricLabel}`}</title>
            </circle>
          ))}
          {[
            ...new Set([
              0,
              Math.floor((points.length - 1) / 2),
              points.length - 1,
            ]),
          ].map((i) => (
            <text
              key={i}
              x={coordinates[i].x}
              y="242"
              textAnchor={
                i === 0 ? "start" : i === points.length - 1 ? "end" : "middle"
              }
              fill="var(--color-ink-3)"
              fontSize="11"
            >
              {formatBucket(points[i].bucket)}
            </text>
          ))}
        </svg>
      </div>
      <details className="analytics-details">
        <summary>View data table</summary>
        <div className="analytics-table-scroll">
          <table className="analytics-table">
            <thead>
              <tr>
                <th>Time (UTC)</th>
                <th>{metricLabel}</th>
              </tr>
            </thead>
            <tbody>
              {points.map((p) => (
                <tr key={p.bucket}>
                  <td>{formatBucket(p.bucket)}</td>
                  <td>{p.views.toLocaleString("en")}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </details>
    </>
  );
}

function BreakdownCard({
  title,
  description,
  points,
  total,
  category,
  metric = "Views",
}: {
  title: string;
  description: string;
  points: DashboardPoint[];
  total: number;
  category?: string;
  metric?: string;
}) {
  const [expanded, setExpanded] = useState(false);
  const id = useId();
  const sorted = useMemo(
    () =>
      [...points].sort(
        (a, b) => b.views - a.views || a.label.localeCompare(b.label),
      ),
    [points],
  );
  const visible = expanded ? sorted : sorted.slice(0, 5);
  const max = Math.max(1, ...sorted.map((p) => p.views));
  return (
    <article className="analytics-card analytics-breakdown">
      <PanelHeading title={title} description={description} />
      <div className="analytics-list-heading">
        <span>{category === "pages" ? "Page" : "Name"}</span>
        <span>{metric} / Share</span>
      </div>
      {!points.length ? (
        <EmptyState label="No data in this period." />
      ) : (
        <ol id={id} className="analytics-ranked-list">
          {visible.map((point, index) => (
            <li key={point.label}>
              <span className="analytics-rank">{index + 1}</span>
              <div className="analytics-row-main">
                <span
                  className="analytics-row-bar"
                  style={{ width: `${(point.views / max) * 100}%` }}
                />
                <span className="analytics-row-label" title={point.label}>
                  {category === "countries"
                    ? countryName(point.label)
                    : point.label === "/"
                      ? "/ · Home"
                      : point.label}
                </span>
              </div>
              <div className="analytics-row-values">
                <strong>{point.views.toLocaleString("en")}</strong>
                <span>{percentage(point.views, total)}</span>
              </div>
            </li>
          ))}
        </ol>
      )}
      {points.length > 5 && (
        <button
          className="analytics-show-more"
          aria-expanded={expanded}
          aria-controls={id}
          onClick={() => setExpanded((value) => !value)}
        >
          {expanded ? "Show less" : `Show more (${points.length - 5})`}
          <span aria-hidden="true">{expanded ? "−" : "+"}</span>
        </button>
      )}
    </article>
  );
}

function SqlWorkspace() {
  const [sql, setSql] = useState(DEFAULT_SQL);
  const [result, setResult] = useState<AthenaQueryResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  async function run(event: FormEvent) {
    event.preventDefault();
    setLoading(true);
    setError(null);
    setResult(null);
    try {
      setResult(
        await analyticsQuery<AthenaQueryResult>({ mode: "custom", sql }),
      );
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "The query failed.");
    } finally {
      setLoading(false);
    }
  }
  return (
    <details className="analytics-card analytics-workspace">
      <summary>
        <span>
          <span className="analytics-eyebrow">Advanced</span>
          <strong>SQL workspace</strong>
          <span>Explore the data with a read-only query</span>
        </span>
        <span aria-hidden="true">＋</span>
      </summary>
      <div className="analytics-workspace-body">
        <p>
          SELECT and WITH queries return up to 500 rows, with a 1 GiB scan
          limit.
        </p>
        <form onSubmit={run}>
          <label htmlFor="analytics-sql">Athena SQL</label>
          <textarea
            id="analytics-sql"
            value={sql}
            onChange={(e) => setSql(e.target.value)}
            rows={10}
            spellCheck={false}
          />
          <button
            className="analytics-button analytics-button-primary"
            disabled={loading}
          >
            {loading ? "Running query…" : "Run query"}
          </button>
        </form>
        {error && <ErrorNotice message={error} />}
        {result && (
          <>
            <QueryFootnote query={result} />
            <div className="analytics-table-scroll">
              <table className="analytics-table">
                <thead>
                  <tr>
                    {result.columns.map((column) => (
                      <th key={column}>{column}</th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {result.rows.map((row, i) => (
                    <tr key={i}>
                      {result.columns.map((column) => (
                        <td key={column}>{row[column] ?? "(null)"}</td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {!result.rows.length && (
              <EmptyState label="The query returned no rows." />
            )}
          </>
        )}
      </div>
    </details>
  );
}

function DashboardSkeleton() {
  return (
    <div role="status" className="analytics-loading">
      <span className="sr-only">Loading analytics</span>
      <div className="analytics-metrics">
        {[0, 1, 2].map((i) => (
          <div key={i} className="analytics-card analytics-skeleton-metric" />
        ))}
      </div>
      <div className="analytics-card analytics-skeleton-chart" />
    </div>
  );
}
function formatBucket(bucket: string) {
  const date = new Date(`${bucket.replace(/Z$/, "")}Z`);
  return Number.isNaN(date.getTime())
    ? bucket
    : new Intl.DateTimeFormat("en", {
        month: "short",
        day: "numeric",
        hour: "numeric",
        timeZone: "UTC",
      }).format(date);
}
function countryName(code: string) {
  try {
    return /^[A-Z]{2}$/.test(code)
      ? (new Intl.DisplayNames(["en"], { type: "region" }).of(code) ?? code)
      : code;
  } catch {
    return code;
  }
}
