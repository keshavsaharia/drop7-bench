"use client";
/**
 * `heatmap`: a rows x cols grid of recorded per-cell values (a 7 x 7 board
 * occupancy map, a generation x candidate grid). Square cells with 2 px
 * surface gaps, the five-step sequential blue (low = darkest on the dark
 * surface) or the diverging pair through the neutral midpoint, a scale
 * legend, the value printed inside a cell only when the cell is >= 34 px
 * and the text fits. Missing cells stay surface-coloured, never zero. Arrow
 * keys move the cell cursor in two dimensions.
 */
import { useMemo, useRef } from "react";
import { ariaText, describeCell } from "@/lib/charts/describe";
import { inCellInk, sequentialStep } from "@/lib/charts/geometry";
import { formatCompact, formatValue } from "@/lib/charts/spec";
import { RowLabels } from "../axes/RowLabels";
import { ScaleLegend } from "../axes/ScaleLegend";
import { ChartFrame } from "../frame/ChartFrame";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Cell } from "../marks/Cell";
import { DIV_MID, DIV_NEG, DIV_POS, SEQ, TICK_SIZE, VALUE_SIZE } from "../tokens";
import { widest, type KindProps } from "./shared";

const STEPS = 5;

export function Heatmap({ spec, id, title, compact, table }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  // `?? []` builds a new array each render, which would re-run every memo
  // below it; hold one identity per spec instead.
  const rows = useMemo(() => spec.rows ?? [], [spec.rows]);
  const cols = useMemo(() => spec.cols ?? [], [spec.cols]);
  const cells = useMemo(() => spec.cells ?? [], [spec.cells]);
  const diverging = spec.scale === "diverging";

  const layout = useMemo(() => {
    const values = cells.map((c) => c.value);
    let [lo, hi] = spec.y?.domain ?? [Math.min(...values), Math.max(...values)];
    if (diverging && !spec.y?.domain) {
      const m = Math.max(Math.abs(lo), Math.abs(hi));
      lo = -m;
      hi = m;
    }
    const rowLabelWidth = Math.ceil(widest(rows, measure, TICK_SIZE, "sans")) + 16;
    const legendWide = width >= 520;
    const legendWidth = legendWide ? 14 + 6 + Math.ceil(widest([formatCompact(lo), formatCompact(hi)], measure, TICK_SIZE, "mono")) + 16 : 0;
    const left = Math.min(rowLabelWidth, width * 0.4);
    const available = width - left - legendWidth - 12;
    const size = Math.max(12, Math.min(44, Math.floor(available / Math.max(cols.length, 1))));
    const top = 8;
    const gridBottom = top + size * rows.length;
    const colLabelHeight = TICK_SIZE + 10;
    const legendBelow = legendWide ? 0 : 14 + TICK_SIZE + 18;
    const height = gridBottom + colLabelHeight + legendBelow + 4;
    const right = left + size * cols.length;
    return { lo, hi, left, right, top, gridBottom, size, height, legendWide, legendX: right + 12, legendY: top, legendBelowY: gridBottom + colLabelHeight };
  }, [cells, spec.y?.domain, diverging, rows, cols.length, width, measure]);

  const colorOf = (value: number): { color: string; step: number } => {
    if (diverging) {
      if (value > 0) return { color: DIV_POS, step: 4 };
      if (value < 0) return { color: DIV_NEG, step: 4 };
      return { color: DIV_MID, step: 2 };
    }
    const step = sequentialStep(value, layout.lo, layout.hi, STEPS);
    return { color: SEQ(step), step };
  };

  const targets = useMemo<CursorTarget[]>(
    () =>
      cells.map((cell) => {
        const ri = rows.indexOf(String(cell.row));
        const ci = cols.indexOf(String(cell.col));
        const x = layout.left + ci * layout.size;
        const y = layout.top + ri * layout.size;
        const text = describeCell(spec, cell);
        return {
          key: `${ri}-${ci}`,
          x: x + layout.size / 2,
          y,
          rect: { x, y, w: layout.size, h: layout.size },
          row: ri,
          col: ci,
          content: { head: `${String(cell.row)}, ${String(cell.col)}`, rows: [{ value: text.value, key: { color: colorOf(cell.value).color, shape: "rect" }, hot: true }], details: text.details, source: text.source },
          aria: ariaText(null, text),
        };
      }),
    // colorOf is derived from layout and the spec's scale, both listed.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [cells, rows, cols, layout, spec],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "rect", grid: { rows: rows.length, cols: cols.length, primary: "row" }, anchor: "mark", viewBox: { width, height: layout.height } });
  const hotKey = cursor.target?.key ?? null;
  const titleId = id ? `${id}-title` : undefined;
  const legendColors = diverging ? [DIV_NEG, DIV_MID, DIV_POS] : Array.from({ length: STEPS }, (_, i) => SEQ(i));

  return (
    <ChartFrame id={id} title={title} frameRef={ref} evidence={spec.evidence} tooltip={cursor.tooltip} live={cursor.live} table={table} compact={compact}>
      <svg
        ref={svgRef}
        className="rchart-svg"
        width={width}
        height={layout.height}
        viewBox={`0 0 ${width} ${layout.height}`}
        role="group"
        aria-roledescription="heatmap"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        <RowLabels rows={rows.map((r, ri) => ({ y: layout.top + ri * layout.size + layout.size / 2, bottom: layout.top + (ri + 1) * layout.size, lines: [r] }))} x={layout.left} plotRight={layout.right} rules={false} />
        {cols.map((c, ci) => (
          <text key={c} className="rchart-cat" x={layout.left + ci * layout.size + layout.size / 2} y={layout.gridBottom + 6} dy="0.9em" textAnchor="middle">
            {c}
          </text>
        ))}
        {cells.map((cell) => {
          const ri = rows.indexOf(String(cell.row));
          const ci = cols.indexOf(String(cell.col));
          const { color, step } = colorOf(cell.value);
          const label = formatValue(cell.value, undefined);
          const fits = layout.size >= 34 && measure(label, VALUE_SIZE, 600, "mono") + 8 <= layout.size;
          const key = `${ri}-${ci}`;
          return <Cell key={key} x={layout.left + ci * layout.size} y={layout.top + ri * layout.size} size={layout.size} color={color} label={fits ? label : undefined} ink={diverging ? "light" : inCellInk(step)} hot={hotKey === key} delay={Math.min(ri * 20, 200)} />;
        })}
        {layout.legendWide ? (
          <ScaleLegend x={layout.legendX} y={layout.legendY} colors={legendColors} min={formatCompact(layout.lo)} max={formatCompact(layout.hi)} />
        ) : (
          <ScaleLegend x={layout.left} y={layout.legendBelowY} colors={legendColors} min={formatCompact(layout.lo)} max={formatCompact(layout.hi)} vertical={false} />
        )}
      </svg>
    </ChartFrame>
  );
}
