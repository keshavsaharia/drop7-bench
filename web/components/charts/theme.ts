/** Shared look for every chart in the console (dark theme, matches globals.css). */
export const PALETTE = [
  "#3987e5", // blue
  "#f59e0b", // amber
  "#22c55e", // green
  "#e879f9", // pink
  "#f87171", // red
  "#a3e635", // lime
  "#22d3ee", // cyan
  "#c084fc", // violet
];

export const INK = "#e4e4e7";
export const MUTED = "#a1a1aa";
export const FAINT = "#71717a";
export const GRID = "#27272a";
export const AXIS = "#3f3f46";
export const ZERO = "#71717a";
export const TOOLTIP_BG = "#18181b";
export const TOOLTIP_BORDER = "#3f3f46";

export const FONT = "ui-sans-serif, system-ui, -apple-system, 'Segoe UI', sans-serif";
export const TICK_SIZE = 11;
export const LABEL_SIZE = 12;

/** Width used for server rendering and for the first client render (before measurement). */
export const DEFAULT_WIDTH = 680;
export const MIN_WIDTH = 320;

export function seriesColor(index: number): string {
  return PALETTE[index % PALETTE.length];
}
