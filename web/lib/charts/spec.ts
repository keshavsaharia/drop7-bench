/**
 * The research-figure spec: a JSON file of recorded numbers under
 * web/content/figures/<name>.json, rendered by <Figure/> through the chart
 * kit in web/components/charts/. This module is shared by server and client
 * code, so it must stay free of Node imports.
 *
 * Every plotted value names the record it was copied from (`sourceRecord`).
 * A series may carry the default for its points, so a 64-game strip does not
 * repeat one id 64 times, but a point that resolves to no record is refused,
 * exactly as the retired SVG generator refused it. The chart draws the numbers
 * as written and computes only pixel positions, axis ranges and tick values:
 * histograms carry pre-binned counts, stacked bars carry recorded shares,
 * reference lines carry recorded values. The validator never fills a missing
 * value; it returns a normalised copy whose only additions are inherited
 * provenance ids.
 */
import { unknownEvidenceWords } from "./evidence.ts";
import { parseIsoDate } from "./layout.ts";

export type FigureKind = "line" | "bar" | "dot" | "forest" | "delta" | "paired" | "strip" | "histogram" | "stacked" | "heatmap" | "sparkline";

export const KINDS: readonly FigureKind[] = ["line", "bar", "dot", "forest", "delta", "paired", "strip", "histogram", "stacked", "heatmap", "sparkline"];

/**
 * primary: solid 2 px in its slot colour; control: dashed 2 px in its slot;
 * reference: dashed 1.5 px in the de-emphasis grey with hollow markers, takes
 * no slot; band: lo..hi drawn as a 10 % wash with no line; context: solid in
 * the de-emphasis grey, takes no slot.
 */
export type SeriesRole = "primary" | "control" | "reference" | "band" | "context";

export const SERIES_ROLES: readonly SeriesRole[] = ["primary", "control", "reference", "band", "context"];

export type AxisScale = "linear" | "log" | "time";

export interface FigureAxis {
  label: string;
  unit?: string;
  /** `log` is base-10 (positive values only); `time` means x values are ISO dates (YYYY-MM-DD). */
  scale?: AxisScale;
  /** A recorded domain (for example a fixed 0..1 metric). Never inferred beyond the data when absent. */
  domain?: [number, number];
}

export interface FigurePoint {
  /** Numeric for `line`/`sparkline` (an ISO date on a time axis); a category name for the row kinds; either for `paired`/`strip`. */
  x: number | string;
  /** The recorded value. For `strip` and `paired` it is the per-game value; the chart lays it along the value axis. */
  y: number;
  /** Optional bound(s), drawn as whiskers. A lone `lo` is a one-sided 95 % lower bound. */
  lo?: number;
  hi?: number;
  /** Sample size (games), shown in the tooltip and table. */
  n?: number;
  /** Recorded detection floor (delta, forest): drawn as a band from -floor to +floor behind that row. */
  floor?: number;
  /** Recorded wins / ties / losses (delta, forest, paired). Tooltip and table only. */
  wtl?: [number, number, number];
  /** A game stopped at the move cap (strip). Drawn hollow. */
  censored?: boolean;
  /** Free-text note shown in the tooltip and the table. */
  label?: string;
  /** Always present after validation: inherited from the series when the point omits it. */
  sourceRecord: string;
  sourceField?: string;
}

export interface FigureSeries {
  name: string;
  /** Older specs: a stopped, partial or reference arm. `role` supersedes it (dashed reads as `control`). */
  dashed?: boolean;
  role?: SeriesRole;
  /** Default provenance for points that omit theirs. */
  sourceRecord?: string;
  sourceField?: string;
  points: FigurePoint[];
}

/** A recorded reference value drawn as a line or band (a median, a target, 1x). */
export interface FigureMarker {
  value: number;
  label: string;
  /** Which axis the value lives on; the value axis when absent. */
  axis?: "x" | "y";
  lo?: number;
  hi?: number;
  style?: "line" | "band";
  sourceRecord: string;
  sourceField?: string;
}

export interface FigureBin {
  label: string;
  lo?: number;
  hi?: number;
  count?: number;
  share?: number;
  /** Free-text note shown in the tooltip and the table (for example the number of waves the share came from). */
  note?: string;
  /** Always present after validation: inherited from the histogram series when the bin omits it. */
  sourceRecord: string;
  sourceField?: string;
}

export interface FigureHistogram {
  series: string;
  sourceRecord?: string;
  sourceField?: string;
  bins: FigureBin[];
}

export interface FigureCell {
  row: string | number;
  col: string | number;
  value: number;
  n?: number;
  sourceRecord: string;
  sourceField?: string;
}

export interface FigureEvidence {
  tier?: string;
  validity?: string;
  outcome?: string;
  status?: string;
  reads?: string;
  cohort?: string;
}

export interface FigureSpec {
  /** A noun phrase naming what is plotted; the verdict belongs in the page caption. */
  title: string;
  kind: FigureKind;
  x?: FigureAxis;
  y?: FigureAxis;
  /** Cohort, sample size, scoring rule, evidence tier, caveats. Shown under "Source data". */
  notes?: string;
  evidence?: FigureEvidence;
  orientation?: "vertical" | "horizontal";
  valueLabels?: "none" | "tip" | "end" | "extremes";
  colorBy?: "series" | "sign" | "disc";
  facet?: "series";
  markers?: FigureMarker[];
  /** Empty for `histogram` and `heatmap`, which carry `bins` / `cells` instead. */
  series: FigureSeries[];
  bins?: FigureHistogram[];
  rows?: string[];
  cols?: string[];
  cells?: FigureCell[];
  /** Heatmap colour scale. */
  scale?: "sequential" | "diverging";
}

export const SOURCE_ID = /^(RS|RUN|EX|TH)-[A-Za-z0-9-]+$|^docs\/.+\.md$|^web\/content\/log\/\d{4}-\d{2}-\d{2}\.mdx$/;

const PROVENANCE_MESSAGE = "sourceRecord must be a research record ID (RS-/RUN-/EX-/TH-) or a docs/*.md path; refusing to render a point without provenance";

const DISC_CATEGORY = /^disc [1-7]$/;

type Fail = (message: string) => never;

function isFinite(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

function validateAxis(raw: unknown, name: string, fail: Fail): FigureAxis | undefined {
  if (raw === undefined) return undefined;
  if (!isRecord(raw)) return fail(`${name} axis is not an object`);
  if (typeof raw.label !== "string") return fail(`${name} axis needs a label`);
  if (raw.unit !== undefined && typeof raw.unit !== "string") return fail(`${name} axis unit must be a string`);
  if (raw.scale !== undefined && raw.scale !== "linear" && raw.scale !== "log" && raw.scale !== "time") return fail(`unknown axis scale ${String(raw.scale)}`);
  if (raw.domain !== undefined) {
    const d = raw.domain;
    if (!Array.isArray(d) || d.length !== 2 || !isFinite(d[0]) || !isFinite(d[1]) || d[0] >= d[1]) return fail(`${name} axis domain must be [lo, hi] with lo < hi`);
  }
  const axis: FigureAxis = { label: raw.label };
  if (raw.unit !== undefined) axis.unit = raw.unit as string;
  if (raw.scale !== undefined) axis.scale = raw.scale as AxisScale;
  if (raw.domain !== undefined) axis.domain = [...(raw.domain as [number, number])];
  return axis;
}

function validateMarkers(raw: unknown, fail: Fail): FigureMarker[] | undefined {
  if (raw === undefined) return undefined;
  if (!Array.isArray(raw)) return fail("markers must be an array");
  return raw.map((m, i) => {
    const at = `marker ${i}`;
    if (!isRecord(m)) return fail(`${at}: not an object`);
    if (!isFinite(m.value)) return fail(`${at}: value must be a finite number`);
    if (typeof m.label !== "string" || !m.label) return fail(`${at}: needs a label`);
    if (m.axis !== undefined && m.axis !== "x" && m.axis !== "y") return fail(`${at}: axis must be x or y`);
    if (m.style !== undefined && m.style !== "line" && m.style !== "band") return fail(`${at}: style must be line or band`);
    for (const key of ["lo", "hi"] as const) if (m[key] !== undefined && !isFinite(m[key])) return fail(`${at}: ${key} must be a number`);
    if (typeof m.sourceRecord !== "string" || !SOURCE_ID.test(m.sourceRecord)) return fail(`${at} (${m.label}): ${PROVENANCE_MESSAGE}`);
    if (m.sourceField !== undefined && typeof m.sourceField !== "string") return fail(`${at}: sourceField must be a string`);
    const marker: FigureMarker = { value: m.value, label: m.label, sourceRecord: m.sourceRecord };
    if (m.axis !== undefined) marker.axis = m.axis as "x" | "y";
    if (m.style !== undefined) marker.style = m.style as "line" | "band";
    if (m.lo !== undefined) marker.lo = m.lo as number;
    if (m.hi !== undefined) marker.hi = m.hi as number;
    if (m.sourceField !== undefined) marker.sourceField = m.sourceField as string;
    return marker;
  });
}

function validateEvidence(raw: unknown, fail: Fail): FigureEvidence | undefined {
  if (raw === undefined) return undefined;
  if (!isRecord(raw)) return fail("evidence must be an object");
  const out: FigureEvidence = {};
  for (const key of ["tier", "validity", "outcome", "status", "reads", "cohort"] as const) {
    if (raw[key] === undefined) continue;
    if (typeof raw[key] !== "string") return fail(`evidence.${key} must be a string`);
    out[key] = raw[key] as string;
  }
  const unknown = unknownEvidenceWords(out);
  if (unknown.length) return fail(`evidence uses words outside the closed vocabulary: ${unknown.map((u) => `${u.field}="${u.word}"`).join(", ")}`);
  return out;
}

function xRule(kind: FigureKind, x: FigureAxis | undefined): "number" | "string" | "either" | "date" {
  if (x?.scale === "time") return "date";
  switch (kind) {
    case "line":
    case "sparkline":
      return "number";
    case "paired":
    case "strip":
      return "either";
    default:
      return "string";
  }
}

function validatePoint(raw: unknown, series: { name: string; sourceRecord?: string; sourceField?: string }, kind: FigureKind, x: FigureAxis | undefined, fail: Fail): FigurePoint {
  const at = `series ${series.name}, point x=${String(isRecord(raw) ? raw.x : raw)}`;
  if (!isRecord(raw)) return fail(`${at}: point is not an object`);
  const p = raw;
  const sourceRecord = typeof p.sourceRecord === "string" ? p.sourceRecord : series.sourceRecord;
  if (typeof sourceRecord !== "string" || !SOURCE_ID.test(sourceRecord)) return fail(`${at}: ${PROVENANCE_MESSAGE}`);
  if (!isFinite(p.y)) return fail(`${at}: y must be a finite number`);
  const rule = xRule(kind, x);
  if (rule === "number" && typeof p.x !== "number") return fail(`${at}: ${kind} charts need numeric x`);
  if (rule === "string" && typeof p.x !== "string") return fail(`${at}: ${kind} charts need a string x category`);
  if (rule === "either" && typeof p.x !== "number" && typeof p.x !== "string") return fail(`${at}: x must be a number or a string`);
  if (rule === "date" && parseIsoDate(p.x) === null) return fail(`${at}: a time axis needs ISO dates (YYYY-MM-DD) for x`);
  if (x?.scale === "log" && typeof p.x === "number" && p.x <= 0) return fail(`${at}: a log x axis needs positive x`);
  for (const key of ["lo", "hi", "n", "floor"] as const) {
    if (p[key] !== undefined && !isFinite(p[key])) return fail(`${at}: ${key} must be a number`);
  }
  if (p.floor !== undefined && (p.floor as number) < 0) return fail(`${at}: floor must be >= 0`);
  if (p.wtl !== undefined) {
    const w = p.wtl;
    if (!Array.isArray(w) || w.length !== 3 || !w.every((v) => Number.isInteger(v) && v >= 0)) return fail(`${at}: wtl must be [wins, ties, losses] as non-negative integers`);
  }
  if (p.censored !== undefined && typeof p.censored !== "boolean") return fail(`${at}: censored must be a boolean`);
  if (p.label !== undefined && typeof p.label !== "string") return fail(`${at}: label must be a string`);
  if (p.sourceField !== undefined && typeof p.sourceField !== "string") return fail(`${at}: sourceField must be a string`);
  const point: FigurePoint = { x: p.x as number | string, y: p.y, sourceRecord };
  for (const key of ["lo", "hi", "n", "floor"] as const) if (p[key] !== undefined) point[key] = p[key] as number;
  if (p.wtl !== undefined) point.wtl = [...(p.wtl as [number, number, number])];
  if (p.censored !== undefined) point.censored = p.censored as boolean;
  if (p.label !== undefined) point.label = p.label as string;
  const sourceField = typeof p.sourceField === "string" ? p.sourceField : typeof p.sourceRecord === "string" ? undefined : series.sourceField;
  if (sourceField !== undefined) point.sourceField = sourceField;
  return point;
}

function validateSeries(raw: unknown, kind: FigureKind, x: FigureAxis | undefined, fail: Fail): FigureSeries[] {
  if (!Array.isArray(raw) || raw.length === 0) return fail("series[] is empty");
  const out = raw.map((s) => {
    if (!isRecord(s) || typeof s.name !== "string" || !s.name) return fail("every series needs a name");
    if (s.dashed !== undefined && typeof s.dashed !== "boolean") return fail(`series ${s.name}: dashed must be a boolean`);
    if (s.role !== undefined && !SERIES_ROLES.includes(s.role as SeriesRole)) return fail(`series ${s.name}: unknown role ${String(s.role)}`);
    if (s.sourceRecord !== undefined && (typeof s.sourceRecord !== "string" || !SOURCE_ID.test(s.sourceRecord))) return fail(`series ${s.name}: ${PROVENANCE_MESSAGE}`);
    if (s.sourceField !== undefined && typeof s.sourceField !== "string") return fail(`series ${s.name}: sourceField must be a string`);
    if (!Array.isArray(s.points) || s.points.length === 0) return fail(`series ${s.name} has no points`);
    const header = { name: s.name, sourceRecord: s.sourceRecord as string | undefined, sourceField: s.sourceField as string | undefined };
    const series: FigureSeries = { name: s.name, points: s.points.map((p) => validatePoint(p, header, kind, x, fail)) };
    if (s.dashed !== undefined) series.dashed = s.dashed as boolean;
    if (s.role !== undefined) series.role = s.role as SeriesRole;
    if (s.sourceRecord !== undefined) series.sourceRecord = s.sourceRecord as string;
    if (s.sourceField !== undefined) series.sourceField = s.sourceField as string;
    return series;
  });
  if (kind === "sparkline" && out.length !== 1) return fail("a sparkline has exactly one series");
  if (kind === "paired" && out.length !== 1) return fail("a paired chart has exactly one series (the recorded per-game deltas)");
  const slots = out.filter((s) => s.role !== "reference" && s.role !== "context" && s.role !== "band").length;
  if (slots > 8) return fail(`${slots} coloured series exceed the eight fixed palette slots; facet into small multiples or fold the tail`);
  return out;
}

function validateBins(raw: unknown, fail: Fail): FigureHistogram[] {
  if (!Array.isArray(raw) || raw.length === 0) return fail("histogram needs bins[] (pre-binned counts or shares from the record)");
  return raw.map((h, hi) => {
    const at = `bins[${hi}]`;
    if (!isRecord(h) || typeof h.series !== "string" || !h.series) return fail(`${at}: needs a series name`);
    if (h.sourceRecord !== undefined && (typeof h.sourceRecord !== "string" || !SOURCE_ID.test(h.sourceRecord))) return fail(`${at} (${h.series}): ${PROVENANCE_MESSAGE}`);
    if (!Array.isArray(h.bins) || h.bins.length === 0) return fail(`${at} (${h.series}): bins are empty`);
    const labels = new Set<string>();
    let mode: "count" | "share" | null = null;
    const bins = h.bins.map((b, bi) => {
      const where = `${at} (${h.series}), bin ${bi}`;
      if (!isRecord(b) || typeof b.label !== "string" || !b.label) return fail(`${where}: needs a label`);
      if (labels.has(b.label)) return fail(`${where}: duplicate bin label ${b.label}`);
      labels.add(b.label);
      const hasCount = b.count !== undefined;
      const hasShare = b.share !== undefined;
      if (hasCount === hasShare) return fail(`${where}: exactly one of count or share`);
      const thisMode = hasCount ? "count" : "share";
      if (mode && mode !== thisMode) return fail(`${where}: mixes count and share within one histogram`);
      mode = thisMode;
      const value = hasCount ? b.count : b.share;
      if (!isFinite(value) || (value as number) < 0) return fail(`${where}: ${thisMode} must be a non-negative number`);
      for (const key of ["lo", "hi"] as const) if (b[key] !== undefined && !isFinite(b[key])) return fail(`${where}: ${key} must be a number`);
      if (b.note !== undefined && typeof b.note !== "string") return fail(`${where}: note must be a string`);
      const sourceRecord = typeof b.sourceRecord === "string" ? b.sourceRecord : (h.sourceRecord as string | undefined);
      if (typeof sourceRecord !== "string" || !SOURCE_ID.test(sourceRecord)) return fail(`${where}: ${PROVENANCE_MESSAGE}`);
      const bin: FigureBin = { label: b.label, sourceRecord };
      if (hasCount) bin.count = b.count as number;
      else bin.share = b.share as number;
      if (b.lo !== undefined) bin.lo = b.lo as number;
      if (b.hi !== undefined) bin.hi = b.hi as number;
      if (b.note !== undefined) bin.note = b.note as string;
      const field = typeof b.sourceField === "string" ? b.sourceField : typeof b.sourceRecord === "string" ? undefined : (h.sourceField as string | undefined);
      if (field !== undefined) bin.sourceField = field;
      return bin;
    });
    const histogram: FigureHistogram = { series: h.series, bins };
    if (h.sourceRecord !== undefined) histogram.sourceRecord = h.sourceRecord as string;
    if (h.sourceField !== undefined) histogram.sourceField = h.sourceField as string;
    return histogram;
  });
}

function validateGrid(s: Record<string, unknown>, fail: Fail): { rows: string[]; cols: string[]; cells: FigureCell[] } {
  const axisList = (raw: unknown, name: string): string[] => {
    if (!Array.isArray(raw) || raw.length === 0 || !raw.every((v) => typeof v === "string" && v)) return fail(`heatmap ${name} must be a non-empty list of strings`);
    if (new Set(raw).size !== raw.length) return fail(`heatmap ${name} has duplicates`);
    return [...(raw as string[])];
  };
  const rows = axisList(s.rows, "rows");
  const cols = axisList(s.cols, "cols");
  if (!Array.isArray(s.cells) || s.cells.length === 0) return fail("heatmap needs cells[]");
  const seen = new Set<string>();
  const cells = s.cells.map((c, i) => {
    const at = `cell ${i}`;
    if (!isRecord(c)) return fail(`${at}: not an object`);
    const row = String(c.row);
    const col = String(c.col);
    if (!rows.includes(row)) return fail(`${at}: row ${row} is not in rows[]`);
    if (!cols.includes(col)) return fail(`${at}: col ${col} is not in cols[]`);
    const key = `${row} ${col}`;
    if (seen.has(key)) return fail(`${at}: duplicate cell (${row}, ${col})`);
    seen.add(key);
    if (!isFinite(c.value)) return fail(`${at}: value must be a finite number`);
    if (c.n !== undefined && !isFinite(c.n)) return fail(`${at}: n must be a number`);
    if (typeof c.sourceRecord !== "string" || !SOURCE_ID.test(c.sourceRecord)) return fail(`${at} (${row}, ${col}): ${PROVENANCE_MESSAGE}`);
    const cell: FigureCell = { row: c.row as string | number, col: c.col as string | number, value: c.value, sourceRecord: c.sourceRecord };
    if (c.n !== undefined) cell.n = c.n as number;
    if (c.sourceField !== undefined) {
      if (typeof c.sourceField !== "string") return fail(`${at}: sourceField must be a string`);
      cell.sourceField = c.sourceField;
    }
    return cell;
  });
  return { rows, cols, cells };
}

/**
 * Throws a descriptive error when the spec is malformed or a value lacks
 * provenance. Returns a normalised copy: point provenance inherited from its
 * series is written onto the point, histogram/heatmap specs get an empty
 * `series`, and nothing else is added or changed.
 */
export function validateFigureSpec(spec: unknown, where = "figure"): FigureSpec {
  const fail: Fail = (message) => {
    throw new Error(`${where}: ${message}`);
  };
  if (!isRecord(spec)) return fail("spec is not an object");
  const s = spec;
  if (typeof s.title !== "string" || !s.title) return fail("missing title");
  if (typeof s.kind !== "string" || !KINDS.includes(s.kind as FigureKind)) return fail(`unknown kind ${String(s.kind)}`);
  const kind = s.kind as FigureKind;
  const x = validateAxis(s.x, "x", fail);
  const y = validateAxis(s.y, "y", fail);
  if (s.notes !== undefined && typeof s.notes !== "string") return fail("notes must be a string");
  if (s.orientation !== undefined && s.orientation !== "vertical" && s.orientation !== "horizontal") return fail(`unknown orientation ${String(s.orientation)}`);
  if (s.valueLabels !== undefined && !["none", "tip", "end", "extremes"].includes(s.valueLabels as string)) return fail(`unknown valueLabels ${String(s.valueLabels)}`);
  if (s.colorBy !== undefined && !["series", "sign", "disc"].includes(s.colorBy as string)) return fail(`unknown colorBy ${String(s.colorBy)}`);
  if (s.facet !== undefined && s.facet !== "series") return fail(`unknown facet ${String(s.facet)}`);
  if (s.scale !== undefined && s.scale !== "sequential" && s.scale !== "diverging") return fail(`unknown scale ${String(s.scale)}`);
  if (x?.scale === "log" && kind !== "line" && kind !== "sparkline") return fail("a log x axis needs numeric x (kind line)");
  if (x?.scale === "time" && !["line", "bar", "dot", "sparkline"].includes(kind)) return fail(`a time x axis is not supported on kind ${kind}`);
  const markers = validateMarkers(s.markers, fail);
  const evidence = validateEvidence(s.evidence, fail);

  const out: FigureSpec = { title: s.title, kind, series: [] };
  if (x) out.x = x;
  if (y) out.y = y;
  if (s.notes !== undefined) out.notes = s.notes as string;
  if (evidence) out.evidence = evidence;
  if (s.orientation !== undefined) out.orientation = s.orientation as "vertical" | "horizontal";
  if (s.valueLabels !== undefined) out.valueLabels = s.valueLabels as FigureSpec["valueLabels"];
  if (s.colorBy !== undefined) out.colorBy = s.colorBy as FigureSpec["colorBy"];
  if (s.facet !== undefined) out.facet = "series";
  if (markers) out.markers = markers;
  if (s.scale !== undefined) out.scale = s.scale as "sequential" | "diverging";

  if (kind === "histogram") {
    if (Array.isArray(s.series) && s.series.length > 0) return fail("histogram carries pre-binned counts or shares in bins[]; series[] with raw values is refused because the chart never bins");
    out.bins = validateBins(s.bins, fail);
    return out;
  }
  if (kind === "heatmap") {
    if (Array.isArray(s.series) && s.series.length > 0) return fail("heatmap carries cells[]; series[] is not allowed");
    const grid = validateGrid(s, fail);
    out.rows = grid.rows;
    out.cols = grid.cols;
    out.cells = grid.cells;
    return out;
  }
  out.series = validateSeries(s.series, kind, x, fail);
  if (out.colorBy === "disc") {
    for (const series of out.series) for (const p of series.points) if (!DISC_CATEGORY.test(String(p.x))) return fail(`colorBy "disc" needs every category to be "disc 1".."disc 7" (found ${String(p.x)})`);
  }
  return out;
}

/**
 * Advisory findings that do not make a spec invalid: a scatter-like kind
 * with more than three series and no facet, and stacked rows whose recorded
 * shares do not sum to the whole (reported, never corrected).
 */
export function specWarnings(spec: FigureSpec): string[] {
  const out: string[] = [];
  if ((spec.kind === "dot" || spec.kind === "strip") && spec.series.length > 3 && !spec.facet) {
    out.push(`${spec.series.length} series on a ${spec.kind} chart without facet: "series"; scatter-like forms cap at three colours (the validator's all-pairs check covers three slots)`);
  }
  if (spec.kind === "stacked") {
    const whole = spec.y?.unit === "%" || spec.y?.unit === "percent" ? 100 : 1;
    const tolerance = whole === 100 ? 0.5 : 0.005;
    const sums = new Map<string, number>();
    for (const series of spec.series) for (const p of series.points) sums.set(String(p.x), (sums.get(String(p.x)) ?? 0) + p.y);
    for (const [row, sum] of sums) {
      if (Math.abs(sum - whole) > tolerance) out.push(`stacked row "${row}" sums to ${sum} (expected ${whole} within ${tolerance}); the shares are drawn as recorded`);
    }
  }
  return out;
}

export interface Provenance {
  record: string;
  field?: string;
  /** Where in the spec the value sits (series and x, marker label, bin label, or cell). */
  at: string;
}

/** Every plotted value mapped to the record it was copied from. */
export function resolveProvenance(spec: FigureSpec): Provenance[] {
  const out: Provenance[] = [];
  for (const series of spec.series) {
    for (const p of series.points) out.push({ record: p.sourceRecord, field: p.sourceField, at: `${series.name} · ${String(p.x)}` });
  }
  for (const m of spec.markers ?? []) out.push({ record: m.sourceRecord, field: m.sourceField, at: `marker ${m.label}` });
  for (const h of spec.bins ?? []) for (const b of h.bins) out.push({ record: b.sourceRecord, field: b.sourceField, at: `${h.series} · ${b.label}` });
  for (const c of spec.cells ?? []) out.push({ record: c.sourceRecord, field: c.sourceField, at: `cell (${String(c.row)}, ${String(c.col)})` });
  return out;
}

/** Distinct source records in a spec, in first-seen order. */
export function sourceRecords(spec: FigureSpec): string[] {
  return [...new Set(resolveProvenance(spec).map((p) => p.record))];
}

/** Kinds whose values are signed contrasts, printed with an explicit sign. */
export function isSignedKind(spec: FigureSpec): boolean {
  if (spec.kind === "delta" || spec.kind === "paired" || spec.kind === "forest") return true;
  if (spec.kind === "bar" && spec.series.some((s) => s.points.some((p) => p.y < 0 || (p.lo !== undefined && p.lo < 0)))) return true;
  return spec.colorBy === "sign";
}

export interface TableColumn {
  label: string;
  numeric?: boolean;
}

export interface TableCell {
  text: string;
  /** A source id the frame may link. */
  source?: string;
  field?: string;
  /** Secondary text (a point label, a censored flag). */
  note?: string;
}

function boundsText(lo: number | undefined, hi: number | undefined, unit?: string): string {
  if (lo !== undefined && hi !== undefined) return `${formatValue(lo)} to ${formatValue(hi, unit)}`;
  if (lo !== undefined) return `lower ${formatValue(lo, unit)}`;
  if (hi !== undefined) return `upper ${formatValue(hi, unit)}`;
  return "—";
}

/** The table-view twin of a spec, kind-aware. Cells are the recorded values, formatted. */
export function specTable(spec: FigureSpec): { columns: TableColumn[]; rows: TableCell[][] } {
  const unit = spec.y?.unit;
  if (spec.kind === "histogram") {
    const mode = spec.bins?.[0]?.bins[0]?.count !== undefined ? "count" : "share";
    const columns: TableColumn[] = [{ label: "Series" }, { label: "Bin" }, { label: "Range", numeric: true }, { label: mode === "count" ? "Count" : spec.y?.label ?? "Share", numeric: true }, { label: "Source" }];
    const rows = (spec.bins ?? []).flatMap((h) =>
      h.bins.map((b) => [
        { text: h.series },
        { text: b.label, note: b.note },
        { text: b.lo !== undefined || b.hi !== undefined ? boundsText(b.lo, b.hi) : "—" },
        { text: formatValue((b.count ?? b.share) as number, mode === "share" ? unit : undefined) },
        { text: b.sourceRecord, source: b.sourceRecord, field: b.sourceField },
      ]),
    );
    return { columns, rows };
  }
  if (spec.kind === "heatmap") {
    const columns: TableColumn[] = [{ label: "Row" }, { label: "Column" }, { label: spec.y?.label ?? "value", numeric: true }, { label: "n", numeric: true }, { label: "Source" }];
    const rows = (spec.cells ?? []).map((c) => [
      { text: String(c.row) },
      { text: String(c.col) },
      { text: formatValue(c.value, unit) },
      { text: c.n !== undefined ? formatValue(c.n) : "—" },
      { text: c.sourceRecord, source: c.sourceRecord, field: c.sourceField },
    ]);
    return { columns, rows };
  }
  const signed = isSignedKind(spec);
  const points = spec.series.flatMap((s) => s.points);
  const hasFloor = points.some((p) => p.floor !== undefined);
  const hasWtl = points.some((p) => p.wtl !== undefined);
  const columns: TableColumn[] = [
    { label: "Series" },
    { label: spec.x?.label ?? "x" },
    { label: spec.y?.label ?? "y", numeric: true },
    { label: "Bounds", numeric: true },
    ...(hasFloor ? [{ label: "Floor", numeric: true }] : []),
    ...(hasWtl ? [{ label: "W-T-L" }] : []),
    { label: "n", numeric: true },
    { label: "Source" },
  ];
  const rows = spec.series.flatMap((s) =>
    s.points.map((p) => {
      const notes: string[] = [];
      if (p.label) notes.push(p.label);
      if (p.censored) notes.push("censored");
      return [
        { text: s.name },
        { text: formatX(spec, p.x), note: notes.length ? notes.join(" · ") : undefined },
        { text: signed ? formatSigned(p.y, unit) : formatValue(p.y, unit) },
        { text: boundsText(p.lo, p.hi) },
        ...(hasFloor ? [{ text: p.floor !== undefined ? formatValue(p.floor) : "—" }] : []),
        ...(hasWtl ? [{ text: p.wtl ? p.wtl.join("-") : "—" }] : []),
        { text: p.n !== undefined ? formatValue(p.n) : "—" },
        { text: p.sourceRecord, source: p.sourceRecord, field: p.sourceField },
      ] as TableCell[];
    }),
  );
  for (const m of spec.markers ?? []) {
    rows.push([
      { text: `marker: ${m.label}` },
      { text: m.axis === "x" ? formatValue(m.value, spec.x?.unit) : "—" },
      { text: m.axis === "x" ? "—" : signed ? formatSigned(m.value, unit) : formatValue(m.value, unit) },
      { text: boundsText(m.lo, m.hi) },
      ...(hasFloor ? [{ text: "—" }] : []),
      ...(hasWtl ? [{ text: "—" }] : []),
      { text: "—" },
      { text: m.sourceRecord, source: m.sourceRecord, field: m.sourceField },
    ]);
  }
  return { columns, rows };
}

/** Axis title text: "label (unit)". */
export function axisTitle(axis?: FigureAxis): string | undefined {
  if (!axis) return undefined;
  return axis.unit ? `${axis.label} (${axis.unit})` : axis.label;
}

/** An x value as text: numbers with the axis unit, dates and categories as written. */
export function formatX(spec: FigureSpec, x: number | string): string {
  if (typeof x === "number") return formatValue(x, spec.x?.unit);
  return x;
}

/** Full-precision value with an optional unit, for tooltips and tables. */
export function formatValue(value: number, unit?: string): string {
  const abs = Math.abs(value);
  const text = abs >= 1000 ? value.toLocaleString("en-US", { maximumFractionDigits: 0 }) : value.toLocaleString("en-US", { maximumFractionDigits: 2 });
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
