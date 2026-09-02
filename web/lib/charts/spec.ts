/**
 * The research-figure spec: a JSON file of recorded numbers under
 * web/content/figures/<name>.json, rendered by <Figure/> through the visx
 * chart kit in web/components/charts/. This module is shared by server and
 * client code, so it must stay free of Node imports.
 *
 * Every point names the record it was copied from (`sourceRecord`); a spec
 * with a point that lacks one is refused, exactly as the retired SVG
 * generator refused it. The chart draws the numbers as written and computes
 * only pixel positions and axis ranges.
 */

export type FigureKind = "line" | "bar" | "dot" | "forest";

export interface FigurePoint {
  /** Numeric for `line`; a category name for the other kinds. */
  x: number | string;
  y: number;
  /** Optional bound(s), drawn as whiskers. A lone `lo` is a one-sided 95% lower bound. */
  lo?: number;
  hi?: number;
  /** Sample size (games), shown in the tooltip. */
  n?: number;
  /** Free-text note shown in the tooltip; for `dot`, also drawn beside the marker. */
  label?: string;
  sourceRecord: string;
  sourceField?: string;
}

export interface FigureSeries {
  name: string;
  /** Dashed line and hollow markers: use for a stopped, partial or reference arm. */
  dashed?: boolean;
  points: FigurePoint[];
}

export interface FigureAxis {
  label: string;
  unit?: string;
  /** `log` draws a numeric axis on a base-10 log scale (positive values only). */
  scale?: "linear" | "log";
}

export interface FigureSpec {
  title: string;
  kind: FigureKind;
  x?: FigureAxis;
  y?: FigureAxis;
  /** Cohort, sample size, scoring rule, evidence tier, caveats. Shown under "Source data". */
  notes?: string;
  series: FigureSeries[];
}

export const SOURCE_ID =
  /^(RS|RUN|EX|TH)-[A-Za-z0-9-]+$|^docs\/.+\.md$|^web\/content\/log\/\d{4}-\d{2}-\d{2}\.mdx$/;

const KINDS: FigureKind[] = ["line", "bar", "dot", "forest"];

/** Throws a descriptive error when the spec is malformed or a point lacks provenance. */
export function validateFigureSpec(spec: unknown, where = "figure"): FigureSpec {
  const fail = (message: string): never => {
    throw new Error(`${where}: ${message}`);
  };
  if (!spec || typeof spec !== "object") return fail("spec is not an object");
  const s = spec as Partial<FigureSpec>;
  if (!s.title || typeof s.title !== "string") return fail("missing title");
  if (!s.kind || !KINDS.includes(s.kind)) return fail(`unknown kind ${String(s.kind)}`);
  if (!Array.isArray(s.series) || s.series.length === 0) return fail("series[] is empty");
  for (const series of s.series) {
    if (!series || typeof series.name !== "string" || !series.name) return fail("every series needs a name");
    if (!Array.isArray(series.points) || series.points.length === 0) return fail(`series ${series.name} has no points`);
    for (const p of series.points) {
      const at = `series ${series.name}, point x=${String(p?.x)}`;
      if (!p || typeof p !== "object") return fail(`${at}: point is not an object`);
      if (typeof p.sourceRecord !== "string" || !SOURCE_ID.test(p.sourceRecord)) {
        return fail(
          `${at}: sourceRecord must be a research record ID (RS-/RUN-/EX-/TH-) or a docs/*.md path; refusing to render a point without provenance`,
        );
      }
      if (typeof p.y !== "number" || !Number.isFinite(p.y)) return fail(`${at}: y must be a finite number`);
      if (s.kind === "line" && typeof p.x !== "number") return fail(`${at}: line charts need numeric x`);
      if (s.kind !== "line" && typeof p.x !== "string") return fail(`${at}: ${s.kind} charts need a string x category`);
      for (const key of ["lo", "hi", "n"] as const) {
        const value = p[key];
        if (value !== undefined && (typeof value !== "number" || !Number.isFinite(value))) {
          return fail(`${at}: ${key} must be a number`);
        }
      }
    }
  }
  for (const axis of [s.x, s.y]) {
    if (axis && axis.scale && axis.scale !== "linear" && axis.scale !== "log") return fail(`unknown axis scale ${axis.scale}`);
  }
  if (s.x?.scale === "log" && s.kind !== "line") return fail("a log x axis needs numeric x (kind line)");
  return s as FigureSpec;
}

/** Full-precision value with an optional unit, for tooltips and tables. */
export function formatValue(value: number, unit?: string): string {
  const abs = Math.abs(value);
  const text =
    abs >= 1000
      ? value.toLocaleString("en-US", { maximumFractionDigits: 0 })
      : value.toLocaleString("en-US", { maximumFractionDigits: 2 });
  return unit ? `${text} ${unit}` : text;
}

export function formatSigned(value: number, unit?: string): string {
  return value > 0 ? `+${formatValue(value, unit)}` : formatValue(value, unit);
}

/** Short axis-tick form: 1.2M, 350k, 0.75. Never used for a quoted number. */
export function formatCompact(value: number): string {
  const abs = Math.abs(value);
  if (abs >= 1e6) return trimZero(value / 1e6, abs >= 1e7 ? 0 : 1) + "M";
  if (abs >= 1e3) return trimZero(value / 1e3, abs >= 1e4 ? 0 : 1) + "k";
  if (abs >= 100) return trimZero(value, 0);
  if (abs >= 1) return trimZero(value, 2);
  return trimZero(value, 3);
}

function trimZero(value: number, digits: number): string {
  const fixed = value.toFixed(digits);
  return digits > 0 ? fixed.replace(/\.?0+$/, "") : fixed;
}
