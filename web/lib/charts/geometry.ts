/**
 * Pure pixel-geometry helpers for the chart kit: bar outlines with one
 * rounded data end, grouped-bar slot layout, stacked-segment offsets, strip
 * bucketing, end-label collision, and time-axis ticks. Everything here maps
 * recorded values that the caller has already scaled into pixels onto
 * positions; nothing here produces a number a chart reports. No DOM, no
 * React; unit-tested in geometry.test.ts.
 */

/** Which end of a bar carries the data (the other end sits on the baseline). */
export type BarEnd = "top" | "bottom" | "left" | "right";

/**
 * SVG path for a bar of `w` x `h` at (x, y) whose data end is rounded with
 * radius `r` and whose baseline end is square. A bar thinner than 2r in the
 * rounded direction gets a proportionally smaller radius so the corners never
 * overlap. Zero-size bars return an empty path.
 */
export function barPath(x: number, y: number, w: number, h: number, r: number, end: BarEnd): string {
  if (w <= 0 || h <= 0) return "";
  const f = (n: number) => Math.round(n * 100) / 100;
  const across = end === "top" || end === "bottom" ? w : h;
  const along = end === "top" || end === "bottom" ? h : w;
  const rr = Math.max(0, Math.min(r, across / 2, along));
  const x2 = x + w;
  const y2 = y + h;
  switch (end) {
    case "top":
      return `M${f(x)},${f(y2)} V${f(y + rr)} Q${f(x)},${f(y)} ${f(x + rr)},${f(y)} H${f(x2 - rr)} Q${f(x2)},${f(y)} ${f(x2)},${f(y + rr)} V${f(y2)} Z`;
    case "bottom":
      return `M${f(x)},${f(y)} V${f(y2 - rr)} Q${f(x)},${f(y2)} ${f(x + rr)},${f(y2)} H${f(x2 - rr)} Q${f(x2)},${f(y2)} ${f(x2)},${f(y2 - rr)} V${f(y)} Z`;
    case "right":
      return `M${f(x)},${f(y)} H${f(x2 - rr)} Q${f(x2)},${f(y)} ${f(x2)},${f(y + rr)} V${f(y2 - rr)} Q${f(x2)},${f(y2)} ${f(x2 - rr)},${f(y2)} H${f(x)} Z`;
    case "left":
      return `M${f(x2)},${f(y)} H${f(x + rr)} Q${f(x)},${f(y)} ${f(x)},${f(y + rr)} V${f(y2 - rr)} Q${f(x)},${f(y2)} ${f(x + rr)},${f(y2)} H${f(x2)} Z`;
  }
}

export interface GroupLayout {
  /** Thickness of each bar. */
  thick: number;
  /** Offset of each bar's leading edge from the slot's centre. */
  offsets: number[];
}

/**
 * Lay `count` grouped bars inside a slot of `slotSize` pixels: bars are
 * capped at `maxThick`, separated by `gap`, and centred in the slot. With one
 * bar the offset is -thick/2 (centred).
 */
export function groupLayout(count: number, slotSize: number, maxThick = 24, gap = 2, fill = 0.8): GroupLayout {
  if (count <= 0) return { thick: 0, offsets: [] };
  const usable = Math.max(0, slotSize * fill - gap * (count - 1));
  const thick = Math.max(1, Math.min(maxThick, usable / count));
  const total = thick * count + gap * (count - 1);
  const start = -total / 2;
  const offsets = Array.from({ length: count }, (_, i) => Math.round((start + i * (thick + gap)) * 100) / 100);
  return { thick: Math.round(thick * 100) / 100, offsets };
}

/**
 * Cumulative [start, end] positions of stacked segments, as fractions of
 * `whole` (100 for percentages, 1 for shares). Shares are used exactly as
 * recorded; a row that does not sum to the whole simply leaves a gap or
 * overflows, which the validator reports separately.
 */
export function stackSegments(shares: readonly number[], whole: number): [number, number][] {
  const out: [number, number][] = [];
  let acc = 0;
  for (const share of shares) {
    const start = acc / whole;
    acc += Math.max(0, share);
    out.push([start, acc / whole]);
  }
  return out;
}

/**
 * Stack marks that fall into the same pixel bucket along an axis (a strip
 * plot). `positions` are already-scaled pixel coordinates; the result gives
 * each mark a level (0 = on the baseline, 1 = one mark up, ...) inside its
 * `bucket`-wide bin, in input order so ties are stable.
 */
export function bucketLevels(positions: readonly number[], bucket: number): number[] {
  const counts = new Map<number, number>();
  return positions.map((p) => {
    const bin = Math.floor(p / Math.max(1, bucket));
    const level = counts.get(bin) ?? 0;
    counts.set(bin, level + 1);
    return level;
  });
}

/**
 * Which end labels can be drawn without colliding: labels are taken in the
 * given order (the caller puts the series that matters first) and one is
 * dropped when its centre is closer than `minGap` to an already-placed
 * label. Labels are never nudged, so each stays on its own line.
 */
export function placeEndLabels(centres: readonly number[], minGap: number): boolean[] {
  const placed: number[] = [];
  return centres.map((c) => {
    if (placed.some((p) => Math.abs(p - c) < minGap)) return false;
    placed.push(c);
    return true;
  });
}

const DAY = 86_400_000;

/**
 * Tick positions (UTC milliseconds) for a time axis spanning [lo, hi]: every
 * day when the span fits, otherwise every 2 or 7 days, then month starts.
 * Never more than `count + 1` ticks, always inside the domain.
 */
export function timeTicks(lo: number, hi: number, count: number): number[] {
  if (!(hi > lo) || count <= 0) return [lo];
  const days = (hi - lo) / DAY;
  const stepDays = days <= count ? 1 : days <= count * 2 ? 2 : days <= count * 7 ? 7 : 0;
  const out: number[] = [];
  if (stepDays > 0) {
    const start = Math.ceil(lo / DAY) * DAY;
    for (let t = start; t <= hi; t += stepDays * DAY) out.push(t);
    return out;
  }
  const first = new Date(lo);
  let year = first.getUTCFullYear();
  let month = first.getUTCMonth();
  const monthsSpanned = Math.ceil(days / 30);
  const stepMonths = Math.max(1, Math.ceil(monthsSpanned / count));
  for (;;) {
    const t = Date.UTC(year, month, 1);
    if (t > hi) break;
    if (t >= lo) out.push(t);
    month += stepMonths;
    while (month >= 12) {
      month -= 12;
      year += 1;
    }
  }
  return out.length ? out : [lo];
}

/**
 * Split a total height into rows of `min` px or more, one per category,
 * returning each row's top edge and the edges list used for hit testing.
 */
export function rowBands(top: number, heights: readonly number[]): { tops: number[]; edges: number[] } {
  const tops: number[] = [];
  const edges: number[] = [top];
  let acc = top;
  for (const h of heights) {
    tops.push(acc);
    acc += h;
    edges.push(acc);
  }
  return { tops, edges };
}

/**
 * Text colour for a label inside a filled sequential cell: light text on the
 * two darkest steps (steps 0 and 1 of the blue ramp clear 4.5:1 with white),
 * dark ink on the lighter steps.
 */
export function inCellInk(step: number): "light" | "dark" {
  return step <= 1 ? "light" : "dark";
}

/** Map a value into one of `steps` ordinal bins over [lo, hi]; clamped. */
export function sequentialStep(value: number, lo: number, hi: number, steps: number): number {
  if (!(hi > lo)) return 0;
  const t = (value - lo) / (hi - lo);
  return Math.max(0, Math.min(steps - 1, Math.floor(t * steps)));
}
