/**
 * Scales for the chart kit: linear, log, time and band, every one wrapped in
 * `rounded` so the coordinates it emits are identical on the server and in
 * the browser (a log axis otherwise produces a hydration mismatch in the last
 * bit). A scale maps a recorded value to a pixel; it never changes a value.
 */
import { scaleBand, scaleLinear, scaleLog } from "@visx/scale";
import type { FigureAxis } from "@/lib/charts/spec";
import { rounded, tickValues, type TickScale } from "@/lib/charts/layout";
import { timeTicks } from "@/lib/charts/geometry";

export interface ValueScale extends TickScale {
  range(): number[];
  invert?(pixel: number): number;
}

export function linearScale(domain: [number, number], range: [number, number], nice = 0): ValueScale {
  return rounded(scaleLinear<number>({ domain, range, nice: nice > 0 ? nice : false }) as unknown as ValueScale);
}

export function logScale(domain: [number, number], range: [number, number]): ValueScale {
  return rounded(scaleLog<number>({ domain, range }) as unknown as ValueScale);
}

/**
 * A time axis over UTC milliseconds. Linear underneath; `ticks` returns day
 * or month boundaries (web/lib/charts/geometry.ts timeTicks).
 */
export function timeScale(domain: [number, number], range: [number, number]): ValueScale {
  const base = scaleLinear<number>({ domain, range });
  const fn = ((value: number) => Math.round(base(value) * 100) / 100) as ValueScale;
  fn.domain = () => [...domain];
  fn.range = () => [...range];
  fn.ticks = (count: number) => timeTicks(domain[0], domain[1], count);
  fn.invert = (pixel: number) => base.invert(pixel);
  return fn;
}

/** Linear, log or time by the axis declaration; `nice` widens a linear domain to clean ticks. */
export function axisScale(axis: FigureAxis | undefined, domain: [number, number], range: [number, number], nice = 5): ValueScale {
  if (axis?.scale === "log") return logScale(domain, range);
  if (axis?.scale === "time") return timeScale(domain, range);
  return linearScale(domain, range, nice);
}

export interface BandScale {
  (category: string): number;
  bandwidth(): number;
  step(): number;
  domain(): string[];
  range(): number[];
}

/** Equal slots for categories, with `padding` as a fraction of the step left as air. */
export function bandScale(categories: string[], range: [number, number], padding = 0.2): BandScale {
  const scale = scaleBand<string>({ domain: categories, range, paddingInner: padding, paddingOuter: padding / 2 });
  const fn = ((category: string) => Math.round((scale(category) ?? 0) * 100) / 100) as BandScale;
  fn.bandwidth = () => Math.round(scale.bandwidth() * 100) / 100;
  fn.step = () => Math.round(scale.step() * 100) / 100;
  fn.domain = () => [...categories];
  fn.range = () => [...range];
  return fn;
}

/** Ticks for a value scale: clean numbers, integers when asked, decades on log, dates on time. */
export function scaleTicks(scale: ValueScale, count: number, integer = false): number[] {
  return tickValues(scale, count, integer);
}
