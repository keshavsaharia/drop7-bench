/**
 * What every chart kind shares: series styling by role and slot, legend
 * items, category lists, tooltip content (value first), plot boxes with
 * measured gutters and horizontal axis titles, and the reference markers a
 * spec carries. Pure functions over validated specs plus the measurer; the
 * kinds do the drawing.
 */
import type { ReactNode } from "react";
import { createElement } from "react";
import { ariaText, describePoint, sourceText, type PointText } from "@/lib/charts/describe";
import { formatIsoDate, parseIsoDate } from "@/lib/charts/layout";
import { axisTitle, formatCompact, formatX, type FigureAxis, type FigurePoint, type FigureSpec } from "@/lib/charts/spec";
import type { KeyStyle } from "../frame/Key";
import type { LegendItem } from "../frame/Legend";
import type { TooltipContent, TooltipRow } from "../frame/Tooltip";
import type { CursorTarget } from "../hover/useChartCursor";
import { wrapText, type Measurer } from "../layout";
import { ReferenceBand, ReferenceLine } from "../marks/ReferenceLine";
import type { ValueScale } from "../scales";
import { DEEMPHASIS, LABEL_SIZE, SERIES, TICK_SIZE } from "../tokens";

/* ------------------------------------------------------------- styling */

export interface SeriesStyle {
  color: string;
  dashed: boolean;
  thin: boolean;
  hollow: boolean;
  diamond: boolean;
  band: boolean;
  deemphasis: boolean;
  /** Palette slot consumed, or null for reference/context/band roles. */
  slot: number | null;
  key: KeyStyle;
}

export type MarkKind = "line" | "mark" | "bar";

/** Optional per-series overrides for adapters that build specs from snapshots. */
export interface SeriesOverride {
  color?: string;
  thin?: boolean;
  dashed?: boolean;
}

/**
 * Colour and stroke for each series: slots in fixed order for primary and
 * control roles; the de-emphasis grey (no slot) for reference and context;
 * a band takes the colour of the slot series before it. `dashed` on an old
 * spec reads as the control role.
 */
export function seriesStyles(spec: FigureSpec, kind: MarkKind, overrides: readonly (SeriesOverride | undefined)[] = []): SeriesStyle[] {
  let slot = 0;
  let lastSlotColor = SERIES(0);
  return spec.series.map((series, index) => {
    const role = series.role ?? (series.dashed ? "control" : "primary");
    const override = overrides[index];
    let color: string;
    let consumed: number | null = null;
    if (role === "reference" || role === "context") color = DEEMPHASIS;
    else if (role === "band") color = lastSlotColor;
    else {
      consumed = slot;
      color = SERIES(slot);
      lastSlotColor = color;
      slot += 1;
    }
    if (override?.color) color = override.color;
    const dashed = override?.dashed ?? (role === "control" || role === "reference");
    const thin = override?.thin ?? (role === "reference" || role === "context");
    const hollow = role === "reference";
    const diamond = role === "reference" && spec.kind === "forest";
    const shape: KeyStyle["shape"] = role === "band" ? "band" : kind === "bar" ? "rect" : kind === "mark" ? (diamond ? "diamond" : hollow ? "hollow" : "dot") : dashed ? "dashed" : "line";
    return { color, dashed, thin, hollow, diamond, band: role === "band", deemphasis: color === DEEMPHASIS, slot: consumed, key: { color, shape, thin } };
  });
}

export function legendItems(spec: FigureSpec, styles: SeriesStyle[]): LegendItem[] {
  return spec.series.map((series, index) => ({ name: series.name, key: styles[index].key }));
}

/** Distinct string categories in first-seen order. */
export function categoriesOf(spec: FigureSpec): string[] {
  const out: string[] = [];
  for (const series of spec.series) for (const point of series.points) if (!out.includes(String(point.x))) out.push(String(point.x));
  return out;
}

/** Indices of the series that have a point in `category`. */
export function seriesIn(spec: FigureSpec, category: string): number[] {
  const out: number[] = [];
  spec.series.forEach((series, index) => {
    if (series.points.some((p) => String(p.x) === category)) out.push(index);
  });
  return out;
}

export function pointIn(spec: FigureSpec, seriesIndex: number, category: string): FigurePoint | undefined {
  return spec.series[seriesIndex].points.find((p) => String(p.x) === category);
}

/* ---------------------------------------------------------------- axes */

export function axisText(axis?: FigureAxis): string | undefined {
  return axisTitle(axis);
}

export function tickFormatter(axis?: FigureAxis): (value: number) => string {
  if (axis?.scale === "time") return (value) => formatIsoDate(value);
  return formatCompact;
}

/** Numeric x for a point on a numeric or time axis. */
export function numericX(point: FigurePoint, axis?: FigureAxis): number {
  if (axis?.scale === "time") return parseIsoDate(point.x) ?? 0;
  return typeof point.x === "number" ? point.x : Number(point.x);
}

/** Every value that must fit on the value axis: y, bounds, floors and the value markers. */
export function valueRange(spec: FigureSpec, includeZero: boolean, floors = false): number[] {
  const values: number[] = [];
  for (const series of spec.series) {
    for (const point of series.points) {
      values.push(point.y);
      if (point.lo !== undefined) values.push(point.lo);
      if (point.hi !== undefined) values.push(point.hi);
      if (floors && point.floor !== undefined) values.push(point.floor, -point.floor);
    }
  }
  for (const marker of spec.markers ?? []) {
    if (marker.axis === "x") continue;
    values.push(marker.value);
    if (marker.lo !== undefined) values.push(marker.lo);
    if (marker.hi !== undefined) values.push(marker.hi);
  }
  if (includeZero) values.push(0);
  return values;
}

/* ------------------------------------------------------------- tooltips */

export function pointRow(text: PointText, seriesName: string | undefined, style: SeriesStyle | undefined, hot: boolean, note?: string): TooltipRow {
  return { value: text.value, label: seriesName, key: style?.key, hot, note };
}

export interface PointTargetOptions {
  head?: string;
  /** Show the series name after the value (off for one-series charts). */
  named?: boolean;
  rect?: CursorTarget["rect"];
  row?: number;
  col?: number;
  /** Extra rows listed before the details (other series at the same x). */
  siblings?: TooltipRow[];
  signed?: boolean;
  key?: KeyStyle;
}

/** A cursor target for one point: value first, then the details, then the source. */
export function pointTarget(key: string, x: number, y: number, spec: FigureSpec, seriesIndex: number, point: FigurePoint, styles: SeriesStyle[], options: PointTargetOptions = {}): CursorTarget {
  const series = spec.series[seriesIndex];
  const text = describePoint(spec, point, options.signed);
  const named = options.named ?? spec.series.length > 1;
  const row: TooltipRow = { value: text.value, label: named ? series.name : undefined, key: options.key ?? styles[seriesIndex]?.key, hot: Boolean(options.siblings?.length) };
  const content: TooltipContent = {
    head: options.head ?? text.x,
    rows: options.siblings ? [row, ...options.siblings] : [row],
    details: text.details,
    source: text.source,
  };
  return { key, x, y, rect: options.rect, row: options.row, col: options.col, content, aria: ariaText(named ? series.name : null, text) };
}

/** A tooltip row for another series at the same x (crosshair readouts, dumbbell rows). */
export function siblingRow(spec: FigureSpec, seriesIndex: number, point: FigurePoint, styles: SeriesStyle[], signed?: boolean): TooltipRow {
  const text = describePoint(spec, point, signed);
  return { value: text.value, label: spec.series[seriesIndex].name, key: styles[seriesIndex]?.key };
}

export function xText(spec: FigureSpec, x: number | string): string {
  return formatX(spec, x);
}

export { sourceText };

/* --------------------------------------------------------------- boxes */

export interface PlotBox {
  width: number;
  height: number;
  left: number;
  right: number;
  top: number;
  bottom: number;
  /** Wrapped value-axis title lines, drawn above the plot at top-left. */
  valueTitle: string[];
  /** Wrapped category/x-axis title lines, drawn under the bottom axis. */
  xTitle: string[];
  xTitleY: number;
}

export function plotHeight(width: number, compact = false, height?: number): number {
  if (height) return height;
  return compact ? Math.max(120, Math.min(200, Math.round(width * 0.3))) : Math.max(180, Math.min(300, Math.round(width * 0.42)));
}

const TITLE_LINE = LABEL_SIZE + 3;

/** Wrap an axis title into at most two lines that fit `maxWidth`. */
export function titleLines(text: string | undefined, maxWidth: number, measure: Measurer): string[] {
  if (!text) return [];
  return wrapText(text, maxWidth, LABEL_SIZE, (t, s) => measure(t, s, 400, "sans"), 2).lines.slice(0, 2);
}

export function wrapLabel(text: string, maxWidth: number, measure: Measurer, maxLines = 3): { lines: string[]; fits: boolean } {
  return wrapText(text, maxWidth, TICK_SIZE, (t, s) => measure(t, s, 400, "sans"), maxLines);
}

export function widest(labels: readonly string[], measure: Measurer, size: number, font: "sans" | "mono"): number {
  return Math.max(0, ...labels.map((label) => measure(label, size, 400, font)));
}

/**
 * A plot with a vertical value axis on the left and a category/x axis at the
 * bottom. Gutters come from the measured tick labels; the value title sits
 * above the plot (never rotated), the x title under the tick labels.
 */
export function verticalBox({
  width,
  measure,
  tickLabels,
  valueTitle,
  xTitle,
  xLabelHeight,
  height,
  compact,
  rightPad = 16,
}: {
  width: number;
  measure: Measurer;
  tickLabels: string[];
  valueTitle?: string;
  xTitle?: string;
  /** Height of the x tick or category labels below the axis. */
  xLabelHeight: number;
  height?: number;
  compact?: boolean;
  rightPad?: number;
}): PlotBox {
  const left = Math.ceil(widest(tickLabels, measure, TICK_SIZE, "mono")) + 14;
  const right = width - rightPad;
  const vt = titleLines(valueTitle, Math.max(120, right - left), measure);
  const top = vt.length ? vt.length * TITLE_LINE + 10 : 12;
  const bottom = top + plotHeight(width, compact, height);
  const xt = titleLines(xTitle, Math.max(120, right - left), measure);
  const total = bottom + xLabelHeight + 8 + (xt.length ? xt.length * TITLE_LINE + 4 : 0) + 4;
  return { width, height: total, left, right, top, bottom, valueTitle: vt, xTitle: xt, xTitleY: total - 4 - (xt.length - 1) * TITLE_LINE - 2 };
}

/**
 * A plot of horizontal rows: category labels on the left (measured, wrapped,
 * capped at 42 % of the width), a value axis along the bottom with its title
 * under the ticks.
 */
export function horizontalBox({
  width,
  measure,
  categories,
  rowHeights,
  valueTitle,
  rightPad = 24,
  topPad = 10,
}: {
  width: number;
  measure: Measurer;
  categories: string[];
  rowHeights: number[];
  valueTitle?: string;
  rightPad?: number;
  topPad?: number;
}): PlotBox & { labels: { lines: string[] }[] } {
  const maxLabel = Math.min(Math.round(width * 0.42), 260);
  const labels = categories.map((c) => wrapLabel(c, maxLabel, measure));
  const labelWidth = Math.max(0, ...labels.flatMap((l) => l.lines.map((line) => measure(line, TICK_SIZE, 400, "sans"))));
  const left = Math.min(Math.ceil(labelWidth) + 16, width * 0.5);
  const right = width - rightPad;
  const top = topPad;
  const bottom = top + rowHeights.reduce((a, b) => a + b, 0);
  const xt = titleLines(valueTitle, Math.max(120, right - left), measure);
  const total = bottom + TICK_SIZE + 14 + (xt.length ? xt.length * TITLE_LINE + 4 : 0) + 2;
  return { width, height: total, left, right, top, bottom, valueTitle: [], xTitle: xt, xTitleY: total - 4 - (xt.length - 1) * TITLE_LINE, labels };
}

/** Row heights for horizontal kinds: enough for the lanes and for the wrapped label. */
export function rowHeightsFor(labels: { lines: string[] }[], lanes: number[], laneHeight: number, minHeight = 28): number[] {
  return labels.map((label, i) => Math.max(minHeight, 10 + lanes[i] * laneHeight, label.lines.length * (TICK_SIZE + 3) + 10));
}

/* -------------------------------------------------------------- markers */

/**
 * Draw a spec's reference markers: value markers as dashed lines (or bands
 * when they carry lo/hi or style "band") across the plot, x markers as
 * vertical lines when an x scale is given. Labels in ink.
 */
export function renderMarkers(
  spec: FigureSpec,
  valueScale: ValueScale,
  valueAxis: "y" | "x",
  box: PlotBox,
  xScale?: (x: number) => number,
): ReactNode[] {
  const out: ReactNode[] = [];
  (spec.markers ?? []).forEach((marker, index) => {
    if (marker.axis === "x") {
      if (!xScale) return;
      const at = xScale(marker.value);
      out.push(createElement(ReferenceLine, { key: `mx-${index}`, orientation: "v", at, from: box.top, to: box.bottom, label: marker.label, labelAt: at > (box.left + box.right) / 2 ? "end" : "start" }));
      return;
    }
    const at = valueScale(marker.value);
    if (valueAxis === "y") {
      if (marker.lo !== undefined && marker.hi !== undefined) {
        out.push(createElement(ReferenceBand, { key: `mb-${index}`, orientation: "h", from: valueScale(marker.lo), to: valueScale(marker.hi), start: box.left, end: box.right }));
      }
      if (marker.style !== "band") {
        out.push(createElement(ReferenceLine, { key: `my-${index}`, orientation: "h", at, from: box.left, to: box.right, label: marker.label, labelAt: "end" }));
      }
    } else {
      if (marker.lo !== undefined && marker.hi !== undefined) {
        out.push(createElement(ReferenceBand, { key: `mb-${index}`, orientation: "v", from: valueScale(marker.lo), to: valueScale(marker.hi), start: box.top, end: box.bottom }));
      }
      if (marker.style !== "band") {
        out.push(createElement(ReferenceLine, { key: `mv-${index}`, orientation: "v", at, from: box.top, to: box.bottom, label: marker.label, labelAt: at > (box.left + box.right) / 2 ? "end" : "start" }));
      }
    }
  });
  return out;
}

/** Props every kind accepts. */
export interface KindProps {
  spec: FigureSpec;
  id?: string;
  title?: string;
  height?: number;
  compact?: boolean;
  table?: ReactNode;
  /** Shared domains when the chart is one facet of several. */
  yDomain?: [number, number];
  xDomain?: [number, number];
  overrides?: readonly (SeriesOverride | undefined)[];
  /** Hide the legend (facets carry one legend for the figure). */
  noLegend?: boolean;
}

export function domainOf(values: readonly number[], override?: [number, number]): [number, number] {
  if (override) return override;
  const finite = values.filter((v) => Number.isFinite(v));
  if (finite.length === 0) return [0, 1];
  let lo = Math.min(...finite);
  let hi = Math.max(...finite);
  if (lo === hi) {
    lo -= Math.abs(lo) * 0.1 || 1;
    hi += Math.abs(hi) * 0.1 || 1;
  }
  return [lo, hi];
}
