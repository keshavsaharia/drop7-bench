/**
 * Compatibility re-export of ./tokens for older imports. New code imports
 * ./tokens directly; the legacy names below are removed once nothing under
 * components/charts imports them.
 */
import { seriesToken } from "@/lib/charts/palette";
import { FONT_MONO, INK_1, INK_2, INK_3, RAISED, RULE, RULE_STRONG } from "./tokens";

export * from "./tokens";

/** Legacy names (old ResearchChart / primitives / EvolutionCharts). */
export const PALETTE = Array.from({ length: 8 }, (_, index) => seriesToken(index));
export const MUTED = INK_2;
export const FAINT = INK_3;
export const GRID = RULE;
export const AXIS = RULE_STRONG;
export const ZERO = INK_3;
export const TOOLTIP_BG = RAISED;
export const TOOLTIP_BORDER = RULE_STRONG;
export const HIGHLIGHT = "var(--color-highlight)";
export const DANGER = "var(--color-danger)";
export const FONT = FONT_MONO;
export const LEGACY_INK = INK_1;
export const FALLBACK_MONO = 'ui-monospace, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace';

export function seriesColor(index: number): string {
  return PALETTE[index % PALETTE.length];
}
