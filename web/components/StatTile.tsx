/**
 * <StatTile label value unit? hint? delta? trend? source? evidence? size? />
 *
 * A recorded number with its label and its provenance. Every value is a
 * pre-formatted string written by the page or read verbatim from a record;
 * the component parses only the leading sign of `delta` to tint it. The
 * value is set in the display face with proportional figures, the label is
 * a mono `label`, the source line is ink-3 and links to the record when the
 * console has a route for it. `trend` names a figure spec (and a series);
 * the tile validates the spec on the server and passes the points to the
 * client Sparkline.
 *
 * `Stat`'s props (`label`, `value`, `hint`) are a subset, so `Stat` can be
 * an alias. Never render a tile without a value: omit it or use EmptyState.
 * Server component; styled by web/components/charts/charts.css.
 */
import type { EvidenceInput } from "@/lib/charts/evidence";
import { validateFigureSpec } from "@/lib/charts/spec";
import { readRepoFile } from "@/lib/repo";
import { SourceRef } from "./charts/frame/SourceRef";
import { Sparkline, type SparkPoint } from "./charts/kinds/Sparkline";
import { sourceLinks, splitSource } from "./charts/sources.server";
import { EvidenceStrip } from "./EvidenceStrip";

export interface StatDelta {
  text: string;
  /** Whether a rising value is the good direction (default true). */
  upIsGood?: boolean;
}

export interface StatTrend {
  /** A figure spec name under web/content/figures/. */
  figure: string;
  /** Series name; the first series when absent. */
  series?: string;
}

export interface StatTileProps {
  label: string;
  value: string;
  unit?: string;
  hint?: string;
  delta?: string | StatDelta;
  trend?: StatTrend;
  /** A record id or docs path, optionally "id · field". */
  source?: string;
  evidence?: EvidenceInput;
  size?: "md" | "lg";
}

function deltaTone(delta: StatDelta): "favourable" | "unfavourable" | "neutral" {
  const sign = /^\s*[+]/.test(delta.text) ? 1 : /^\s*[-−]/.test(delta.text) ? -1 : 0;
  if (sign === 0) return "neutral";
  const upIsGood = delta.upIsGood ?? true;
  return (sign > 0) === upIsGood ? "favourable" : "unfavourable";
}

function loadTrend(trend: StatTrend): { points: SparkPoint[]; unit?: string; source?: string; xRange?: string } | null {
  if (!/^[a-z0-9-]+$/.test(trend.figure)) return null;
  const raw = readRepoFile(`web/content/figures/${trend.figure}.json`);
  if (!raw) return null;
  try {
    const spec = validateFigureSpec(JSON.parse(raw), trend.figure);
    const series = trend.series ? spec.series.find((s) => s.name === trend.series) : spec.series[0];
    if (!series || series.points.length < 2) return null;
    const points: SparkPoint[] = series.points.map((p) => ({ x: p.x, y: p.y, sourceRecord: p.sourceRecord, sourceField: p.sourceField }));
    const xs = points.map((p) => String(p.x));
    return { points, unit: spec.y?.unit, source: series.sourceRecord ?? points[0].sourceRecord, xRange: `${spec.x?.label ?? "x"} ${xs[0]} to ${xs[xs.length - 1]}` };
  } catch {
    return null;
  }
}

export function StatTile({ label, value, unit, hint, delta, trend, source, evidence, size = "md" }: StatTileProps) {
  const deltaObj = typeof delta === "string" ? { text: delta } : delta;
  const trendData = trend ? loadTrend(trend) : null;
  const src = source ? splitSource(source) : null;
  const links = src ? sourceLinks([src.id]) : {};
  return (
    <div className={size === "lg" ? "stat-tile is-lg" : "stat-tile"}>
      <p className="stat-tile-label label">{label}</p>
      <p className="stat-tile-value-row">
        <span className="stat-tile-value">{value}</span>
        {unit && <span className="stat-tile-unit">{unit}</span>}
        {deltaObj && <span className={`stat-tile-delta is-${deltaTone(deltaObj)}`}>{deltaObj.text}</span>}
      </p>
      {hint && <p className="stat-tile-hint">{hint}</p>}
      {trendData && (
        <div className="stat-tile-trend">
          <Sparkline points={trendData.points} label={label} unit={trendData.unit} source={trendData.source} />
          {trendData.xRange && <p className="stat-tile-hint">{trendData.xRange}</p>}
        </div>
      )}
      {(evidence || src) && (
        <div className="stat-tile-foot">
          {evidence && <EvidenceStrip {...evidence} />}
          {src && (
            <p className="stat-tile-source">
              <SourceRef id={src.id} field={src.field} href={links[src.id] ?? null} />
            </p>
          )}
        </div>
      )}
    </div>
  );
}
