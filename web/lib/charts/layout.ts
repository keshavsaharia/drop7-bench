/**
 * Pure layout helpers for the chart kit: text estimation and word wrapping,
 * tick selection, value domains, and the `rounded` scale wrapper that keeps
 * server and client coordinates bit-identical. No React, no DOM; the hooks
 * that measure real text live in web/components/charts/layout.ts.
 */

export type Measurer = (text: string, size: number, weight?: number) => number;

/** Estimate used on the server and before hydration: a proportional sans averages ~0.56 em per character. */
export function estimateText(text: string, size: number, weight = 400): number {
  return text.length * size * (weight >= 600 ? 0.6 : 0.56);
}

/**
 * Greedy word wrap into lines no wider than `maxWidth`. A single word wider
 * than the limit is kept on its own line and `fits` reports false so the
 * caller can choose another layout (for example rows instead of columns).
 */
export function wrapText(text: string, maxWidth: number, size: number, measure: Measurer, maxLines = Infinity): { lines: string[]; fits: boolean } {
  const words = text.split(/\s+/).filter(Boolean);
  const lines: string[] = [];
  let line = "";
  let fits = true;
  for (const word of words) {
    const candidate = line ? `${line} ${word}` : word;
    if (measure(candidate, size) <= maxWidth || !line) {
      line = candidate;
      if (!line.includes(" ") && measure(line, size) > maxWidth) fits = false;
    } else {
      lines.push(line);
      line = word;
      if (measure(word, size) > maxWidth) fits = false;
    }
  }
  if (line) lines.push(line);
  if (lines.length > maxLines) fits = false;
  return { lines, fits };
}

/** The part of a d3 scale the kit relies on. */
export interface TickScale {
  (value: number): number;
  domain(): number[];
  ticks(count: number): number[];
  base?: () => number;
}

export function isLogScale(scale: TickScale): boolean {
  return typeof scale.base === "function";
}

/**
 * Tick values for a numeric scale: integers when the data are small integers,
 * decades on a log axis, d3's clean ticks otherwise. Never more than the data
 * needs; never a value the axis cannot show.
 */
export function tickValues(scale: TickScale, count: number, integer = false): number[] {
  const [lo, hi] = scale.domain() as [number, number];
  if (isLogScale(scale)) {
    const decades = Math.log10(hi) - Math.log10(lo);
    const raw = scale.ticks(count);
    if (decades >= 3) return raw.filter((v) => Number.isInteger(Math.round(Math.log10(v) * 1e9) / 1e9));
    return raw.filter((v) => {
      const mantissa = v / 10 ** Math.floor(Math.log10(v));
      return decades >= 1.5 ? [1, 2, 5].includes(Math.round(mantissa)) : true;
    });
  }
  if (integer && hi - lo <= 14) {
    const step = hi - lo > count ? Math.ceil((hi - lo) / count) : 1;
    const values: number[] = [];
    for (let v = Math.ceil(lo); v <= hi; v += step) values.push(v);
    return values;
  }
  return scale.ticks(count);
}

/**
 * Wraps a scale so every coordinate it emits is rounded to 1/100 px.
 * Math.log and friends can differ in the last bit between the Node build
 * that renders the server HTML and the browser that hydrates it; without
 * rounding, a log axis produces a hydration mismatch on those attributes.
 * Idempotent: rounding a rounded scale changes nothing.
 */
export function rounded<S extends TickScale>(scale: S): S {
  const wrapped = ((value: number) => Math.round(scale(value) * 100) / 100) as unknown as S & Record<string, unknown>;
  const source = scale as unknown as Record<string, unknown>;
  for (const key of ["domain", "range", "ticks", "nice", "base", "invert", "copy", "bandwidth", "step", "paddingInner", "paddingOuter"]) {
    const member = source[key];
    if (typeof member === "function") {
      (wrapped as Record<string, unknown>)[key] = (member as (...args: unknown[]) => unknown).bind(scale);
    }
  }
  return wrapped as S;
}

/**
 * The [min, max] of a set of recorded values, optionally forced to include
 * zero, with a small pad when every value is the same so a flat series still
 * has a visible axis. This is an axis range, not a number the chart reports.
 */
export function valueExtent(values: readonly number[], includeZero = false): [number, number] {
  const finite = values.filter((v) => Number.isFinite(v));
  if (includeZero) finite.push(0);
  if (finite.length === 0) return [0, 1];
  let lo = Math.min(...finite);
  let hi = Math.max(...finite);
  if (lo === hi) {
    lo -= Math.abs(lo) * 0.1 || 1;
    hi += Math.abs(hi) * 0.1 || 1;
  }
  return [lo, hi];
}

/** Parse a YYYY-MM-DD date into UTC milliseconds, or null when it is not one. */
export function parseIsoDate(value: unknown): number | null {
  if (typeof value !== "string") return null;
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!m) return null;
  const ms = Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3]));
  const check = new Date(ms);
  if (check.getUTCFullYear() !== Number(m[1]) || check.getUTCMonth() !== Number(m[2]) - 1 || check.getUTCDate() !== Number(m[3])) return null;
  return ms;
}

/** Format UTC milliseconds back to YYYY-MM-DD (tick labels on a time axis). */
export function formatIsoDate(ms: number): string {
  const d = new Date(ms);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`;
}
