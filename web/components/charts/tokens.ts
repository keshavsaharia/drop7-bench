/**
 * The chart kit's design tokens, as CSS variable references. Every colour a
 * chart paints is one of these names (`fill="var(--color-series-1)"` is a
 * valid SVG presentation value), so the hex values live in one place:
 * globals.css for the site tokens and charts.css for the chart-only ones,
 * mirrored for scripts and tests by web/lib/charts/palette.ts. No hex here.
 *
 * Fonts: ticks and values are set in the mono face, titles and category
 * labels in the sans face. A canvas 2D context cannot resolve a CSS variable,
 * so text measurement reads the resolved stacks with `measureFont()`.
 */
import { seriesToken, sequentialToken } from "@/lib/charts/palette";

/** Series slot `index` (0-based), fixed order, never cycled (throws past slot 8). */
export const SERIES = (index: number): string => seriesToken(index);
/** Sequential step `step` (0-based, low -> high). */
export const SEQ = (step: number): string => sequentialToken(step);

export const INK = "var(--color-ink)";
export const INK_1 = "var(--color-ink-1)";
export const INK_2 = "var(--color-ink-2)";
export const INK_3 = "var(--color-ink-3)";
export const RULE = "var(--color-rule)";
export const RULE_STRONG = "var(--color-rule-strong)";
export const SURFACE = "var(--color-surface)";
export const RAISED = "var(--color-raised)";
export const ACCENT = "var(--color-accent)";

export const DEEMPHASIS = "var(--chart-deemphasis)";
export const DIV_POS = "var(--chart-div-pos)";
export const DIV_NEG = "var(--chart-div-neg)";
export const DIV_MID = "var(--chart-div-mid)";

export const FONT_SANS = "var(--font-sans)";
export const FONT_MONO = "var(--font-mono)";

export const TICK_SIZE = 11;
export const LABEL_SIZE = 12;
export const VALUE_SIZE = 11;

/** Width used for server rendering and for the first client render (before measurement). */
export const DEFAULT_WIDTH = 680;
export const MIN_WIDTH = 300;

export const BAR_MAX = 24;
export const DELTA_BAR_MAX = 18;
export const STACK_MAX = 22;
export const MARKER_R = 4;
export const MARKER_R_HOT = 5.5;
export const DOT_R = 5;
export const WHISKER_CAP = 4;
/** Pixel bucket that stacks strip dots. */
export const STRIP_BUCKET = 7;
/** Pointer radius for nearest-mark hits. */
export const HIT_RADIUS = 24;
/** Pointer distance for nearest-x crosshair hits. */
export const CROSSHAIR_RADIUS = 48;

const FALLBACK_SANS = 'ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif';
const FALLBACK_MONO = 'ui-monospace, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace';

export type FontKind = "sans" | "mono";

/**
 * The resolved font stack for canvas text measurement. On the server, or
 * before the stylesheet is available, it is the system fallback.
 */
export function measureFont(kind: FontKind): string {
  const fallback = kind === "mono" ? FALLBACK_MONO : FALLBACK_SANS;
  if (typeof document === "undefined") return fallback;
  try {
    const stack = getComputedStyle(document.documentElement).getPropertyValue(kind === "mono" ? "--font-mono" : "--font-sans").trim();
    return stack || fallback;
  } catch {
    return fallback;
  }
}
