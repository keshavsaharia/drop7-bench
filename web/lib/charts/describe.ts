/**
 * Text for tooltips and the live region, value first. A tooltip row leads
 * with the recorded number and follows with what it is; the details rows
 * (bounds, floor, wins-ties-losses, n, the point's note) come after, and the
 * source line last. Pure string building over validated spec objects, shared
 * by every chart kind and unit-tested in describe.test.ts.
 */
import { formatSigned, formatValue, formatX, isSignedKind, type FigureBin, type FigureCell, type FigureMarker, type FigurePoint, type FigureSeries, type FigureSpec } from "./spec.ts";

export interface PointText {
  /** The recorded value with its unit, signed for contrast kinds. */
  value: string;
  /** The x value or category as text. */
  x: string;
  /** Bounds, floor, W-T-L, n and the point's note, in that order. */
  details: string[];
  /** `record · field`. */
  source: string;
}

export function sourceText(record: string, field?: string): string {
  return field ? `${record} · ${field}` : record;
}

export function boundsDetail(point: { lo?: number; hi?: number }, unit?: string): string | null {
  if (point.lo !== undefined && point.hi !== undefined) return `95 % bounds ${formatValue(point.lo)} to ${formatValue(point.hi, unit)}`;
  if (point.lo !== undefined) return `95 % lower bound ${formatValue(point.lo, unit)}`;
  if (point.hi !== undefined) return `95 % upper bound ${formatValue(point.hi, unit)}`;
  return null;
}

/** Value-first text for one point of a series kind. */
export function describePoint(spec: FigureSpec, point: FigurePoint, signed = isSignedKind(spec)): PointText {
  const unit = spec.y?.unit;
  const details: string[] = [];
  const bounds = boundsDetail(point, unit);
  if (bounds) details.push(signed && point.lo !== undefined && point.hi !== undefined ? bounds.replace(`${formatValue(point.lo)} to`, `${formatSigned(point.lo)} to`).replace(formatValue(point.hi, unit), formatSigned(point.hi, unit)) : bounds);
  if (point.floor !== undefined) details.push(`detection floor ${formatValue(point.floor, unit)}`);
  if (point.wtl) details.push(`W-T-L ${point.wtl.join("-")}`);
  if (point.n !== undefined) details.push(`n = ${formatValue(point.n)} games`);
  if (point.censored) details.push("stopped at the move cap (censored)");
  if (point.label) details.push(point.label);
  return {
    value: signed ? formatSigned(point.y, unit) : formatValue(point.y, unit),
    x: formatX(spec, point.x),
    details,
    source: sourceText(point.sourceRecord, point.sourceField),
  };
}

export function describeBin(spec: FigureSpec, bin: FigureBin): PointText {
  const share = bin.count === undefined;
  const value = share ? formatValue(bin.share as number, spec.y?.unit) : formatValue(bin.count as number, spec.y?.unit ?? "waves");
  const details: string[] = [];
  if (bin.lo !== undefined || bin.hi !== undefined) details.push(bin.lo !== undefined && bin.hi !== undefined ? `${formatValue(bin.lo)} to ${formatValue(bin.hi)}` : bin.lo !== undefined ? `from ${formatValue(bin.lo)}` : `up to ${formatValue(bin.hi as number)}`);
  if (bin.note) details.push(bin.note);
  return { value, x: bin.label, details, source: sourceText(bin.sourceRecord, bin.sourceField) };
}

export function describeCell(spec: FigureSpec, cell: FigureCell): PointText {
  const details: string[] = [];
  if (cell.n !== undefined) details.push(`n = ${formatValue(cell.n)}`);
  return { value: formatValue(cell.value, spec.y?.unit), x: `${String(cell.row)}, ${String(cell.col)}`, details, source: sourceText(cell.sourceRecord, cell.sourceField) };
}

export function describeMarker(spec: FigureSpec, marker: FigureMarker): PointText {
  const unit = marker.axis === "x" ? spec.x?.unit : spec.y?.unit;
  const details: string[] = [];
  const bounds = boundsDetail(marker, unit);
  if (bounds) details.push(bounds);
  return { value: formatValue(marker.value, unit), x: marker.label, details, source: sourceText(marker.sourceRecord, marker.sourceField) };
}

/**
 * The sentence a screen reader hears for a focused mark: the value, the
 * series, the x, then the details. Never the source (it is in the table).
 */
export function ariaText(seriesName: string | null, text: PointText): string {
  const head = seriesName ? `${text.value}, ${seriesName}` : text.value;
  return [`${head}, ${text.x}`, ...text.details].join(". ");
}

/** Every series' value at one x, for the crosshair readout. */
export function seriesAt(spec: FigureSpec, x: number | string): { series: FigureSeries; index: number; point: FigurePoint }[] {
  const out: { series: FigureSeries; index: number; point: FigurePoint }[] = [];
  spec.series.forEach((series, index) => {
    const point = series.points.find((p) => p.x === x);
    if (point) out.push({ series, index, point });
  });
  return out;
}
