/**
 * The chart palette, as hex values, for scripts and tests. Components never
 * read these hexes: they paint with CSS custom properties (see
 * web/components/charts/tokens.ts), so a light theme can be added later under
 * :root[data-theme="light"] without touching a chart.
 *
 * Every set here was run through the dataviz palette validator on the site's
 * card surface (#111114) and raised surface (#18181c):
 *   categorical 8 slots, adjacent pairs: all checks pass (worst CVD Delta E
 *   8.4 protan, worst normal-vision 19.3, every slot >= 3:1);
 *   first three slots, all pairs: pass (scatter-like forms cap at three);
 *   sequential 5-step blue, --ordinal: pass (monotone, adjacent Delta L >= 0.06,
 *   darkest step 2.33:1).
 * web/lib/charts/palette.test.ts re-checks the computable parts of that.
 *
 * Rules: slots are assigned in fixed order and never cycled (seriesColor
 * throws past slot 8: fold the tail, facet, or use composite encoding); the
 * disc palette is never borrowed as a series colour; status colours are never
 * used as a series and always ship with an icon and a word.
 */

/** Categorical series, fixed order. The site exposes these as --color-series-1..8. */
export const SERIES_HEX = ["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300", "#9085e9", "#e66767"] as const;

/** Sequential blue, low -> high on the dark surface (heatmaps, ordinal ladders). */
export const SEQUENTIAL_HEX = ["#184f95", "#256abf", "#3987e5", "#6da7ec", "#9ec5f4"] as const;

/** Diverging: negative / neutral midpoint / positive. */
export const DIVERGING_HEX = { neg: "#e66767", mid: "#52525b", pos: "#3987e5" } as const;

/** Status, always drawn with an icon and the repository's own word. */
export const STATUS_HEX = { good: "#0ca30c", warning: "#fab219", serious: "#ec835a", critical: "#d03b3b" } as const;

/** Context / "other" series and the sparkline base. Below 3:1 by design. */
export const DEEMPHASIS_HEX = "#52525b";

/** The surfaces charts sit on (site tokens --color-surface and --color-raised). */
export const SURFACE_HEX = { surface: "#111114", raised: "#18181c" } as const;

export const MAX_SERIES = SERIES_HEX.length;

/**
 * Tokens web/components/charts/charts.css defines on :root, in that order.
 * palette.test.ts parses the CSS and asserts the two agree.
 */
export const CHART_TOKENS: Readonly<Record<string, string>> = {
  "--chart-seq-1": SEQUENTIAL_HEX[0],
  "--chart-seq-2": SEQUENTIAL_HEX[1],
  "--chart-seq-3": SEQUENTIAL_HEX[2],
  "--chart-seq-4": SEQUENTIAL_HEX[3],
  "--chart-seq-5": SEQUENTIAL_HEX[4],
  "--chart-div-neg": DIVERGING_HEX.neg,
  "--chart-div-mid": DIVERGING_HEX.mid,
  "--chart-div-pos": DIVERGING_HEX.pos,
  "--chart-status-good": STATUS_HEX.good,
  "--chart-status-warning": STATUS_HEX.warning,
  "--chart-status-serious": STATUS_HEX.serious,
  "--chart-status-critical": STATUS_HEX.critical,
  "--chart-deemphasis": DEEMPHASIS_HEX,
};

/** Site tokens the theme defines for the series slots; charts consume them by name. */
export const SERIES_TOKENS: readonly { name: string; hex: string }[] = SERIES_HEX.map((hex, i) => ({ name: `--color-series-${i + 1}`, hex }));

function checkSlot(index: number): void {
  if (!Number.isInteger(index) || index < 0 || index >= MAX_SERIES) {
    throw new Error(`series slot ${index} is out of range: the palette has ${MAX_SERIES} fixed slots and never cycles; fold the tail into "other", facet, or use composite encoding`);
  }
}

/** Hex for series slot `index` (0-based). Throws past slot 8 instead of cycling. */
export function seriesColor(index: number): string {
  checkSlot(index);
  return SERIES_HEX[index];
}

/** CSS variable reference for series slot `index` (0-based). Throws past slot 8. */
export function seriesToken(index: number): string {
  checkSlot(index);
  return `var(--color-series-${index + 1})`;
}

/** CSS variable reference for a sequential step (0-based, low -> high). */
export function sequentialToken(step: number): string {
  if (!Number.isInteger(step) || step < 0 || step >= SEQUENTIAL_HEX.length) throw new Error(`sequential step ${step} out of range 0..${SEQUENTIAL_HEX.length - 1}`);
  return `var(--chart-seq-${step + 1})`;
}

/** Parse `--name: #hex;` declarations out of a CSS text (any block). */
export function parseCssTokens(css: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const m of css.matchAll(/(--[a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\s*;/g)) out[m[1]] = m[2].toLowerCase();
  return out;
}
