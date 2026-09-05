/**
 * Pure hover and keyboard lookups shared by every chart kind: nearest x for a
 * crosshair, nearest mark for a scatter, row lookup for horizontal kinds, a
 * 2-D cell cursor for grids, and the arrow-key stepping rules. Nothing here
 * knows about React or SVG; web/components/charts/hover/useChartCursor.ts
 * wires these to pointer and keyboard events. Unit-tested in nearest.test.ts.
 */

export interface Point2 {
  x: number;
  y: number;
}

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

/**
 * Index of the position nearest `target`, or null when none is within
 * `maxDistance`. Ties resolve to the lower index so the result is stable.
 */
export function nearestIndex(positions: readonly number[], target: number, maxDistance = Infinity): number | null {
  let best: number | null = null;
  let bestDistance = Infinity;
  positions.forEach((position, index) => {
    const distance = Math.abs(position - target);
    if (distance < bestDistance) {
      best = index;
      bestDistance = distance;
    }
  });
  return best !== null && bestDistance <= maxDistance ? best : null;
}

/** Index of the point nearest `target` in the plane, or null beyond `radius`. */
export function nearestPoint(points: readonly Point2[], target: Point2, radius = Infinity): number | null {
  let best: number | null = null;
  let bestDistance = Infinity;
  points.forEach((point, index) => {
    const distance = Math.hypot(point.x - target.x, point.y - target.y);
    if (distance < bestDistance) {
      best = index;
      bestDistance = distance;
    }
  });
  return best !== null && bestDistance <= radius ? best : null;
}

/**
 * Which band of `edges` (ascending: band i spans edges[i]..edges[i+1])
 * contains `value`; null outside the first and last edge.
 */
export function bandIndex(edges: readonly number[], value: number): number | null {
  if (edges.length < 2) return null;
  if (value < edges[0] || value > edges[edges.length - 1]) return null;
  for (let i = 0; i < edges.length - 1; i += 1) {
    if (value >= edges[i] && value < edges[i + 1]) return i;
  }
  return edges.length - 2;
}

/** First rectangle (grown by `pad` on every side) containing the point, or null. */
export function rectIndex(rects: readonly Rect[], target: Point2, pad = 0): number | null {
  for (let i = 0; i < rects.length; i += 1) {
    const r = rects[i];
    if (target.x >= r.x - pad && target.x <= r.x + r.w + pad && target.y >= r.y - pad && target.y <= r.y + r.h + pad) return i;
  }
  return null;
}

/**
 * Move a 1-D cursor by `delta` inside 0..count-1, clamped. From no cursor,
 * a forward step lands on the first item and a backward step on the last.
 */
export function stepIndex(index: number | null, delta: number, count: number): number | null {
  if (count <= 0) return null;
  if (index === null) return delta >= 0 ? 0 : count - 1;
  return Math.max(0, Math.min(count - 1, index + delta));
}

/** The keys the cursor model listens to. */
export type CursorKey = "ArrowLeft" | "ArrowRight" | "ArrowUp" | "ArrowDown" | "Home" | "End" | "Escape";

export function isCursorKey(key: string): key is CursorKey {
  return key === "ArrowLeft" || key === "ArrowRight" || key === "ArrowUp" || key === "ArrowDown" || key === "Home" || key === "End" || key === "Escape";
}

/**
 * 1-D keyboard rule. `axis` says which arrow pair walks the sequence
 * ("x": Left/Right, "y": Up/Down). Home/End jump to the ends; Escape clears
 * (returns null); the other arrow pair and unknown keys return undefined so
 * the caller can route them elsewhere (for example to the series within a
 * category).
 */
export function keyStep(key: string, index: number | null, count: number, axis: "x" | "y" = "x"): number | null | undefined {
  if (count <= 0) return undefined;
  const back = axis === "x" ? "ArrowLeft" : "ArrowUp";
  const forward = axis === "x" ? "ArrowRight" : "ArrowDown";
  switch (key) {
    case back:
      return stepIndex(index, -1, count);
    case forward:
      return stepIndex(index, 1, count);
    case "Home":
      return 0;
    case "End":
      return count - 1;
    case "Escape":
      return null;
    default:
      return undefined;
  }
}

export interface GridCursor {
  row: number;
  col: number;
}

/**
 * 2-D keyboard rule for row x column kinds (grouped bars, dot rows, heatmap
 * cells). `primary` names the axis the Up/Down pair walks: "row" (Up/Down
 * change the row, Left/Right the column) or "col" (the transpose, for
 * vertical grouped bars where Left/Right walk the categories). Home/End jump
 * to the ends of the current row's secondary axis. Escape clears.
 */
export function gridStep(key: string, cursor: GridCursor | null, rows: number, cols: number, primary: "row" | "col" = "row"): GridCursor | null | undefined {
  if (rows <= 0 || cols <= 0) return undefined;
  if (key === "Escape") return null;
  const start = cursor ?? { row: 0, col: 0 };
  const isVertical = key === "ArrowUp" || key === "ArrowDown";
  const isHorizontal = key === "ArrowLeft" || key === "ArrowRight";
  const rowKey = primary === "row" ? isVertical : isHorizontal;
  const colKey = primary === "row" ? isHorizontal : isVertical;
  const backward = key === "ArrowUp" || key === "ArrowLeft";
  if (cursor === null && (isVertical || isHorizontal)) return start;
  if (rowKey) return { row: Math.max(0, Math.min(rows - 1, start.row + (backward ? -1 : 1))), col: start.col };
  if (colKey) return { row: start.row, col: Math.max(0, Math.min(cols - 1, start.col + (backward ? -1 : 1))) };
  if (key === "Home") return { row: start.row, col: 0 };
  if (key === "End") return { row: start.row, col: cols - 1 };
  return undefined;
}
