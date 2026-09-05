"use client";
/**
 * The pointer + keyboard cursor model every chart kind shares. A kind places
 * its marks and hands the hook a list of targets in SVG user units; the hook
 * finds the target under the pointer (nearest x for crosshair kinds, nearest
 * mark for scatters, the containing rectangle for bars and cells, the row
 * band for horizontal kinds), moves a cursor with the arrow keys, and turns
 * the current target into a tooltip anchored to the mark or the crosshair
 * plus the text an aria-live region announces.
 *
 * The svg is the one tab stop: ArrowLeft/Right (or Up/Down) step the cursor,
 * Home/End jump to the ends, Escape clears. The lookups are the pure helpers
 * in web/lib/charts/nearest.ts.
 *
 * The caller owns the svg ref and passes it in, so nothing this hook returns
 * is a ref: a kind reads `cursor.tooltip` and spreads `cursor.handlers`
 * during render, which a returned ref would make illegal.
 */
import { useCallback, useLayoutEffect, useMemo, useState, type KeyboardEvent, type PointerEvent, type RefObject } from "react";
import { bandIndex, gridStep, isCursorKey, keyStep, nearestIndex, nearestPoint, rectIndex, type GridCursor, type Rect } from "@/lib/charts/nearest";
import type { TooltipContent, TooltipState } from "../frame/Tooltip";
import { CROSSHAIR_RADIUS, HIT_RADIUS } from "../tokens";

export interface CursorTarget {
  key: string;
  /** Tooltip anchor in SVG user units. */
  x: number;
  y: number;
  /** Hit rectangle for `hit: "rect"`. */
  rect?: Rect;
  /** Grid coordinates for 2-D keyboard stepping. */
  row?: number;
  col?: number;
  content: TooltipContent;
  /** What the live region announces (value first). */
  aria: string;
}

export interface CursorOptions {
  targets: CursorTarget[];
  /** How the pointer finds a target. */
  hit: "x" | "point" | "rect" | "row";
  /** Arrow pair for 1-D stepping; ignored when `grid` is set. */
  axis?: "x" | "y";
  /** 2-D stepping over targets that carry row/col. */
  grid?: { rows: number; cols: number; primary?: "row" | "col" };
  /** Row band edges (ascending y) for `hit: "row"`. */
  rowEdges?: number[];
  radius?: number;
  anchor?: "mark" | "crosshair";
  /** The y the crosshair tooltip attaches to (the plot top). */
  crosshairTop?: number;
  viewBox: { width: number; height: number };
  /** The svg element, owned by the kind that draws it. */
  svgRef: RefObject<SVGSVGElement | null>;
}

export interface ChartCursor {
  index: number | null;
  target: CursorTarget | null;
  tooltip: TooltipState | null;
  live: string;
  handlers: {
    onPointerMove: (event: PointerEvent<SVGSVGElement>) => void;
    onPointerLeave: () => void;
    onKeyDown: (event: KeyboardEvent<SVGSVGElement>) => void;
    onBlur: () => void;
  };
  clear: () => void;
}

export function useChartCursor(options: CursorOptions): ChartCursor {
  const { targets, hit, axis = "x", grid, rowEdges, anchor = "mark", crosshairTop = 0, viewBox, svgRef } = options;
  const radius = options.radius ?? (hit === "x" ? CROSSHAIR_RADIUS : HIT_RADIUS);
  const [rawIndex, setIndex] = useState<number | null>(null);
  // A target list that shrinks under the cursor (a resize, a filter) drops it.
  const index = rawIndex !== null && rawIndex >= targets.length ? null : rawIndex;
  const [tooltip, setTooltip] = useState<TooltipState | null>(null);

  const xs = useMemo(() => targets.map((t) => t.x), [targets]);
  const points = useMemo(() => targets.map((t) => ({ x: t.x, y: t.y })), [targets]);
  const rects = useMemo(() => targets.map((t) => t.rect ?? { x: t.x - 6, y: t.y - 6, w: 12, h: 12 }), [targets]);

  const toLocal = useCallback(
    (clientX: number, clientY: number): { x: number; y: number } | null => {
      const svg = svgRef.current;
      if (!svg) return null;
      const box = svg.getBoundingClientRect();
      if (box.width <= 0 || box.height <= 0) return null;
      return { x: ((clientX - box.left) * viewBox.width) / box.width, y: ((clientY - box.top) * viewBox.height) / box.height };
    },
    [viewBox.width, viewBox.height, svgRef],
  );

  const find = useCallback(
    (local: { x: number; y: number }): number | null => {
      if (targets.length === 0) return null;
      switch (hit) {
        case "x":
          return nearestIndex(xs, local.x, radius);
        case "point":
          return nearestPoint(points, local, radius);
        case "rect":
          return rectIndex(rects, local, 2);
        case "row": {
          if (!rowEdges) return null;
          const row = bandIndex(rowEdges, local.y);
          if (row === null) return null;
          let best: number | null = null;
          let bestDistance = Infinity;
          targets.forEach((t, i) => {
            if (t.row !== row) return;
            const distance = Math.abs(t.x - local.x);
            if (distance < bestDistance) {
              best = i;
              bestDistance = distance;
            }
          });
          return best;
        }
      }
    },
    [hit, targets, xs, points, rects, rowEdges, radius],
  );

  const onPointerMove = useCallback(
    (event: PointerEvent<SVGSVGElement>) => {
      const local = toLocal(event.clientX, event.clientY);
      if (!local) return;
      setIndex(find(local));
    },
    [toLocal, find],
  );

  const clear = useCallback(() => setIndex(null), []);

  const atGrid = useCallback(
    (cursor: GridCursor): number | null => {
      let exact: number | null = null;
      let nearestCol: number | null = null;
      let nearestDistance = Infinity;
      targets.forEach((t, i) => {
        if (t.row !== cursor.row) return;
        if (t.col === cursor.col) exact = i;
        const distance = Math.abs((t.col ?? 0) - cursor.col);
        if (distance < nearestDistance) {
          nearestDistance = distance;
          nearestCol = i;
        }
      });
      return exact ?? nearestCol;
    },
    [targets],
  );

  const onKeyDown = useCallback(
    (event: KeyboardEvent<SVGSVGElement>) => {
      if (!isCursorKey(event.key) || targets.length === 0) return;
      if (grid) {
        const current = index !== null ? { row: targets[index].row ?? 0, col: targets[index].col ?? 0 } : null;
        const next = gridStep(event.key, current, grid.rows, grid.cols, grid.primary ?? "row");
        if (next === undefined) return;
        event.preventDefault();
        if (next === null) {
          setIndex(null);
          return;
        }
        const found = atGrid(next);
        if (found !== null) setIndex(found);
        return;
      }
      const next = keyStep(event.key, index, targets.length, axis);
      if (next === undefined) return;
      event.preventDefault();
      setIndex(next);
    },
    [targets, grid, index, axis, atGrid],
  );

  // Anchor the tooltip to the current target in viewport coordinates.
  useLayoutEffect(() => {
    const svg = svgRef.current;
    if (index === null || !svg || !targets[index]) {
      setTooltip(null);
      return;
    }
    const target = targets[index];
    const box = svg.getBoundingClientRect();
    const sx = box.width / viewBox.width;
    const sy = box.height / viewBox.height;
    const ax = box.left + target.x * sx;
    const ay = box.top + (anchor === "crosshair" ? crosshairTop : target.y) * sy;
    setTooltip({ x: ax, y: ay, anchor, content: target.content });
  }, [index, targets, viewBox.width, viewBox.height, anchor, crosshairTop, svgRef]);

  const target = index !== null ? (targets[index] ?? null) : null;
  return {
    index,
    target,
    tooltip,
    live: target?.aria ?? "",
    handlers: { onPointerMove, onPointerLeave: clear, onKeyDown, onBlur: clear },
    clear,
  };
}
