"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import type { FormEvent } from "react";
import type {
  AnalyticsAudience,
  AnalyticsRange,
  AthenaQueryResult,
  DashboardData,
  DashboardPoint,
} from "@/lib/analytics/types";

const RANGE_OPTIONS: { value: AnalyticsRange; label: string }[] = [
  { value: "24h", label: "Last 24 hours" },
  { value: "7d", label: "Last 7 days" },
  { value: "30d", label: "Last 30 days" },
  { value: "90d", label: "Last 90 days" },
];

const BREAKDOWNS = [
  { key: "pages", title: "Popular pages" },
  { key: "channels", title: "Acquisition channels" },
  { key: "referrers", title: "Referrer hosts" },
  { key: "countries", title: "Countries" },
  { key: "devices", title: "Devices" },
  { key: "browsers", title: "Browsers" },
] as const;

const DEFAULT_SQL = `SELECT
  date(from_unixtime(occurred_at_ms / 1000.0)) AS day,
  count(*) AS page_views,
  approx_distinct(visitor_id) AS visitors
FROM page_views
WHERE event_name = 'page_view'
  AND is_bot = false
GROUP BY 1
ORDER BY 1 DESC
LIMIT 30`;

export function AnalyticsDashboard() {
  const [range, setRange] = useState<AnalyticsRange>("30d");
  const [audience, setAudience] = useState<AnalyticsAudience>("humans");
  const [dashboard, setDashboard] = useState<DashboardData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [sql, setSql] = useState(DEFAULT_SQL);
  const [customResult, setCustomResult] = useState<AthenaQueryResult | null>(null);
  const [customLoading, setCustomLoading] = useState(false);
  const [customError, setCustomError] = useState<string | null>(null);

  const loadDashboard = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch("/api/analytics/query", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ mode: "dashboard", range, audience }),
      });
      const payload = (await response.json()) as {
        data?: DashboardData;
        error?: string;
      };
      if (!response.ok || !payload.data) {
        throw new Error(payload.error ?? "The analytics query failed.");
      }
      setDashboard(payload.data);
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "The analytics query failed.");
    } finally {
      setLoading(false);
    }
  }, [audience, range]);

  useEffect(() => {
    const timer = window.setTimeout(() => void loadDashboard(), 0);
    return () => window.clearTimeout(timer);
  }, [loadDashboard]);

  async function runCustomQuery(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setCustomLoading(true);
    setCustomError(null);
    setCustomResult(null);
    try {
      const response = await fetch("/api/analytics/query", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ mode: "custom", sql }),
      });
      const payload = (await response.json()) as {
        data?: AthenaQueryResult;
        error?: string;
      };
      if (!response.ok || !payload.data) {
        throw new Error(payload.error ?? "The Athena query failed.");
      }
      setCustomResult(payload.data);
    } catch (caught) {
      setCustomError(caught instanceof Error ? caught.message : "The Athena query failed.");
    } finally {
      setCustomLoading(false);
    }
  }

  return (
    <div className="space-y-8">
      <section className="flex flex-col gap-5 border-b border-zinc-800 pb-7 lg:flex-row lg:items-end lg:justify-between">
        <div className="max-w-3xl">
          <p className="text-xs font-semibold uppercase tracking-[0.18em] text-emerald-400">
            Admin · first-party analytics
          </p>
          <h1 className="mt-2 text-3xl font-black text-zinc-50">Site analytics</h1>
          <p className="mt-3 leading-relaxed text-zinc-400">
            Aggregate server-side page views from the Firehose-backed Iceberg table.
            The table normally trails live traffic by about five minutes. Times are UTC.
          </p>
        </div>
        <div className="flex flex-wrap items-end gap-3">
          <label className="analytics-control">
            <span>Window</span>
            <select value={range} onChange={(event) => setRange(event.target.value as AnalyticsRange)}>
              {RANGE_OPTIONS.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
          <label className="analytics-control">
            <span>Audience</span>
            <select
              value={audience}
              onChange={(event) => setAudience(event.target.value as AnalyticsAudience)}
            >
              <option value="humans">People only</option>
              <option value="all">People and bots</option>
              <option value="bots">Bots only</option>
            </select>
          </label>
          <button
            type="button"
            className="analytics-run-button"
            onClick={() => void loadDashboard()}
            disabled={loading}
          >
            {loading ? "Querying…" : "Refresh"}
          </button>
        </div>
      </section>

      {error ? <ErrorNotice message={error} /> : null}
      {loading && !dashboard ? <DashboardSkeleton /> : null}

      {dashboard ? (
        <>
          <section className="grid gap-3 sm:grid-cols-3">
            <MetricCard label="Page views" value={dashboard.summary.views} />
            <MetricCard label="Approx. visitors" value={dashboard.summary.visitors} />
            <MetricCard label="Pages viewed" value={dashboard.summary.paths} />
          </section>

          <section className="analytics-panel">
            <div className="mb-5 flex flex-wrap items-baseline justify-between gap-2">
              <div>
                <h2 className="text-lg font-bold text-zinc-100">Traffic over time</h2>
                <p className="mt-1 text-sm text-zinc-500">Page views per reporting bucket</p>
              </div>
              <span className="text-xs text-zinc-600">
                {loading ? "Refreshing…" : `${dashboard.timeSeries.length} buckets`}
              </span>
            </div>
            <TimeSeriesChart points={dashboard.timeSeries} />
          </section>

          <section className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
            {BREAKDOWNS.map(({ key, title }) => (
              <BreakdownCard
                key={key}
                title={title}
                points={dashboard.breakdowns[key] ?? []}
              />
            ))}
          </section>

          <QueryFootnote
            dataScannedBytes={dashboard.query.dataScannedBytes}
            engineExecutionMs={dashboard.query.engineExecutionMs}
            queryExecutionId={dashboard.query.queryExecutionId}
          />
        </>
      ) : null}

      <section className="analytics-panel">
        <div className="max-w-3xl">
          <p className="text-xs font-semibold uppercase tracking-[0.18em] text-sky-400">
            Athena workspace
          </p>
          <h2 className="mt-2 text-xl font-bold text-zinc-100">Run a read-only query</h2>
          <p className="mt-2 text-sm leading-relaxed text-zinc-500">
            SELECT and WITH queries run in the analytics workgroup and return at most 500 rows.
            The workgroup enforces its S3 result location and a 1 GiB scan ceiling.
          </p>
        </div>
        <form className="mt-5 space-y-3" onSubmit={runCustomQuery}>
          <label className="block">
            <span className="sr-only">Athena SQL query</span>
            <textarea
              className="analytics-sql"
              value={sql}
              onChange={(event) => setSql(event.target.value)}
              spellCheck={false}
              rows={11}
            />
          </label>
          <button type="submit" className="analytics-run-button" disabled={customLoading}>
            {customLoading ? "Running in Athena…" : "Run query"}
          </button>
        </form>

        {customError ? <div className="mt-4"><ErrorNotice message={customError} /></div> : null}
        {customResult ? <CustomResultTable result={customResult} /> : null}
      </section>
    </div>
  );
}

function MetricCard({ label, value }: { label: string; value: number }) {
  return (
    <article className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-5">
      <p className="text-xs font-semibold uppercase tracking-[0.14em] text-zinc-500">{label}</p>
      <p className="mt-2 text-3xl font-black tabular-nums text-zinc-50">
        {value.toLocaleString()}
      </p>
    </article>
  );
}

function TimeSeriesChart({ points }: { points: DashboardPoint[] }) {
  const geometry = useMemo(() => {
    const width = 900;
    const height = 260;
    const left = 48;
    const right = 16;
    const top = 12;
    const bottom = 34;
    const chartWidth = width - left - right;
    const chartHeight = height - top - bottom;
    const max = Math.max(1, ...points.map((point) => point.views));
    const step = points.length > 1 ? chartWidth / (points.length - 1) : chartWidth;
    const coordinates = points.map((point, index) => ({
      x: left + (points.length === 1 ? chartWidth / 2 : index * step),
      y: top + chartHeight - (point.views / max) * chartHeight,
      point,
    }));
    return { width, height, left, right, top, bottom, chartWidth, chartHeight, max, coordinates };
  }, [points]);

  if (points.length === 0) {
    return <EmptyState label="No page views in this window." />;
  }

  const line = geometry.coordinates.map(({ x, y }) => `${x},${y}`).join(" ");
  const area = [
    `${geometry.left},${geometry.height - geometry.bottom}`,
    line,
    `${geometry.width - geometry.right},${geometry.height - geometry.bottom}`,
  ].join(" ");
  const labelIndexes = Array.from(
    new Set([0, Math.floor((points.length - 1) / 2), points.length - 1]),
  );

  return (
    <div className="overflow-x-auto">
      <svg
        viewBox={`0 0 ${geometry.width} ${geometry.height}`}
        className="min-w-[42rem]"
        role="img"
        aria-label={`Page-view time series with ${points.length} buckets and a maximum of ${geometry.max} views`}
      >
        {[0, 0.5, 1].map((fraction) => {
          const y = geometry.top + geometry.chartHeight * fraction;
          const value = Math.round(geometry.max * (1 - fraction));
          return (
            <g key={fraction}>
              <line
                x1={geometry.left}
                x2={geometry.width - geometry.right}
                y1={y}
                y2={y}
                stroke="#27272a"
                strokeWidth="1"
              />
              <text x={geometry.left - 9} y={y + 4} textAnchor="end" fill="#71717a" fontSize="11">
                {value.toLocaleString()}
              </text>
            </g>
          );
        })}
        <polygon points={area} fill="rgba(16, 185, 129, 0.11)" />
        <polyline
          points={line}
          fill="none"
          stroke="#34d399"
          strokeWidth="3"
          strokeLinejoin="round"
          strokeLinecap="round"
        />
        {geometry.coordinates.map(({ x, y, point }) => (
          <circle key={point.bucket} cx={x} cy={y} r="3" fill="#09090b" stroke="#6ee7b7" strokeWidth="2">
            <title>{`${formatBucket(point.bucket)}: ${point.views.toLocaleString()} views`}</title>
          </circle>
        ))}
        {labelIndexes.map((index) => {
          const coordinate = geometry.coordinates[index];
          if (!coordinate) return null;
          return (
            <text
              key={index}
              x={coordinate.x}
              y={geometry.height - 8}
              textAnchor={index === 0 ? "start" : index === points.length - 1 ? "end" : "middle"}
              fill="#71717a"
              fontSize="11"
            >
              {formatBucket(coordinate.point.bucket)}
            </text>
          );
        })}
      </svg>
    </div>
  );
}

function BreakdownCard({ title, points }: { title: string; points: DashboardPoint[] }) {
  const max = Math.max(1, ...points.map((point) => point.views));
  return (
    <article className="analytics-panel min-w-0">
      <h2 className="text-base font-bold text-zinc-100">{title}</h2>
      {points.length === 0 ? (
        <div className="mt-4"><EmptyState label="No data in this window." /></div>
      ) : (
        <ol className="mt-4 space-y-3">
          {points.map((point) => (
            <li key={point.label}>
              <div className="flex items-baseline justify-between gap-3 text-sm">
                <span className="truncate text-zinc-300" title={point.label}>{point.label}</span>
                <span className="shrink-0 tabular-nums text-zinc-500">
                  {point.views.toLocaleString()}
                </span>
              </div>
              <div className="mt-1.5 h-1.5 overflow-hidden rounded-full bg-zinc-800">
                <div
                  className="h-full rounded-full bg-emerald-500/75"
                  style={{ width: `${Math.max(2, (point.views / max) * 100)}%` }}
                />
              </div>
            </li>
          ))}
        </ol>
      )}
    </article>
  );
}

function CustomResultTable({ result }: { result: AthenaQueryResult }) {
  return (
    <div className="mt-6">
      <QueryFootnote
        dataScannedBytes={result.dataScannedBytes}
        engineExecutionMs={result.engineExecutionMs}
        queryExecutionId={result.queryExecutionId}
      />
      <div className="mt-3 overflow-x-auto rounded-lg border border-zinc-800">
        <table className="w-full min-w-max border-collapse text-left text-sm">
          <thead className="bg-zinc-900 text-xs uppercase tracking-wide text-zinc-500">
            <tr>
              {result.columns.map((column) => (
                <th key={column} className="border-b border-zinc-800 px-3 py-2.5 font-semibold">
                  {column}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {result.rows.map((row, rowIndex) => (
              <tr key={rowIndex} className="border-b border-zinc-900 last:border-0">
                {result.columns.map((column) => (
                  <td key={column} className="max-w-xl px-3 py-2 font-mono text-xs text-zinc-300">
                    {row[column] ?? <span className="text-zinc-700">null</span>}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
        {result.rows.length === 0 ? <EmptyState label="The query returned no rows." /> : null}
      </div>
    </div>
  );
}

function QueryFootnote({
  dataScannedBytes,
  engineExecutionMs,
  queryExecutionId,
}: {
  dataScannedBytes: number;
  engineExecutionMs: number;
  queryExecutionId: string;
}) {
  return (
    <p className="break-all text-xs text-zinc-600">
      Athena scanned {formatBytes(dataScannedBytes)} in {(engineExecutionMs / 1000).toFixed(2)}s · query{" "}
      {queryExecutionId}
    </p>
  );
}

function ErrorNotice({ message }: { message: string }) {
  return (
    <div role="alert" className="rounded-xl border border-red-900/70 bg-red-950/25 px-4 py-3 text-sm text-red-200">
      {message}
    </div>
  );
}

function EmptyState({ label }: { label: string }) {
  return <p className="py-8 text-center text-sm text-zinc-600">{label}</p>;
}

function DashboardSkeleton() {
  return (
    <div className="space-y-4" aria-label="Loading analytics">
      <div className="grid gap-3 sm:grid-cols-3">
        {[0, 1, 2].map((item) => (
          <div key={item} className="h-28 animate-pulse rounded-xl border border-zinc-800 bg-zinc-900/40" />
        ))}
      </div>
      <div className="h-80 animate-pulse rounded-xl border border-zinc-800 bg-zinc-900/30" />
    </div>
  );
}

function formatBucket(bucket: string): string {
  const date = new Date(bucket.endsWith("Z") ? bucket : `${bucket}Z`);
  if (Number.isNaN(date.getTime())) return bucket;
  return new Intl.DateTimeFormat("en", {
    month: "short",
    day: "numeric",
    hour: bucket.includes(":") ? "numeric" : undefined,
    timeZone: "UTC",
  }).format(date);
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GiB`;
}
