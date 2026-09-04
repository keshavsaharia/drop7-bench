"use client";
/**
 * `stacked`: part-to-whole. One horizontal 100 % bar per category (row),
 * at most 22 px thick, segments in series order separated by 2 px surface
 * gaps, a value printed inside a segment only when the measured text fits
 * with 8 px padding on each side. Shares are drawn exactly as recorded; a
 * row that does not sum to the whole is reported by the validator, never
 * rescaled here.
 */
import { useMemo, useRef, useState } from "react";
import { describePoint } from "@/lib/charts/describe";
import { rowBands, stackSegments } from "@/lib/charts/geometry";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis } from "../axes/BottomAxis";
import { RowLabels } from "../axes/RowLabels";
import { ChartFrame } from "../frame/ChartFrame";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { linearScale } from "../scales";
import { STACK_MAX, VALUE_SIZE } from "../tokens";
import { axisText, categoriesOf, horizontalBox, legendItems, pointIn, pointTarget, rowHeightsFor, seriesStyles, type KindProps } from "./shared";

export function Stacked({ spec, id, title, compact, table, overrides, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  const styles = useMemo(() => seriesStyles(spec, "bar", overrides), [spec, overrides]);
  const whole = spec.y?.unit === "%" || spec.y?.unit === "percent" ? 100 : 1;

  const layout = useMemo(() => {
    const cats = categoriesOf(spec);
    const probe = horizontalBox({ width, measure, categories: cats, rowHeights: cats.map(() => 0), valueTitle: axisText(spec.y) });
    const rowHeights = rowHeightsFor(probe.labels, cats.map(() => 1), STACK_MAX + 10, 34);
    const box = horizontalBox({ width, measure, categories: cats, rowHeights, valueTitle: axisText(spec.y) });
    const vScale = linearScale([0, whole], [box.left, box.right]);
    const vTicks = whole === 100 ? [0, 25, 50, 75, 100] : [0, 0.25, 0.5, 0.75, 1];
    const bands = rowBands(box.top, rowHeights);
    return { cats, box, vScale, vTicks, rowHeights, bands, labels: box.labels };
  }, [spec, width, measure, whole]);

  const { cats, box, vScale, bands } = layout;

  const segments = useMemo(() => {
    const out: { si: number; ci: number; x0: number; x1: number; y: number; h: number; value: number }[] = [];
    cats.forEach((cat, ci) => {
      const shares = spec.series.map((_, si) => pointIn(spec, si, cat)?.y ?? 0);
      const stack = stackSegments(shares, whole);
      const h = Math.min(STACK_MAX, layout.rowHeights[ci] - 12);
      const y = bands.tops[ci] + (layout.rowHeights[ci] - h) / 2;
      stack.forEach(([start, end], si) => {
        if (!pointIn(spec, si, cat)) return;
        out.push({ si, ci, x0: vScale(start * whole), x1: vScale(end * whole), y, h, value: shares[si] });
      });
    });
    return out;
  }, [cats, spec, whole, layout.rowHeights, bands, vScale]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      segments.map((seg) => {
        const p = pointIn(spec, seg.si, cats[seg.ci])!;
        return pointTarget(`${seg.si}-${seg.ci}`, (seg.x0 + seg.x1) / 2, seg.y, spec, seg.si, p, styles, { rect: { x: seg.x0, y: seg.y - 2, w: Math.max(0, seg.x1 - seg.x0), h: seg.h + 4 }, row: seg.ci, col: seg.si, signed: false, head: cats[seg.ci], named: true });
      }),
    [segments, spec, cats, styles],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "rect", grid: { rows: cats.length, cols: spec.series.length, primary: "row" }, anchor: "mark", viewBox: { width: box.width, height: box.height } });
  const hotKey = cursor.target?.key ?? null;
  const titleId = id ? `${id}-title` : undefined;

  return (
    <ChartFrame id={id} title={title} frameRef={ref} legend={noLegend ? undefined : legendItems(spec, styles)} legendActive={dim} onLegendHover={setDim} evidence={spec.evidence} tooltip={cursor.tooltip} live={cursor.live} table={table} compact={compact}>
      <svg
        ref={svgRef}
        className="rchart-svg"
        width={box.width}
        height={box.height}
        viewBox={`0 0 ${box.width} ${box.height}`}
        role="group"
        aria-roledescription="stacked bar chart"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        <RowLabels rows={cats.map((_, ci) => ({ y: bands.tops[ci] + layout.rowHeights[ci] / 2, bottom: bands.edges[ci + 1], lines: layout.labels[ci].lines }))} x={box.left} plotRight={box.right} />
        <BottomAxis scale={vScale} ticks={layout.vTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={(v) => (whole === 100 ? `${v}%` : String(v))} />
        <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
        {segments.map((seg) => {
          const key = `${seg.si}-${seg.ci}`;
          const inner = Math.max(0, seg.x1 - seg.x0 - 2);
          if (inner <= 0) return null;
          const text = describePoint(spec, pointIn(spec, seg.si, cats[seg.ci])!, false).value;
          const fits = measure(text, VALUE_SIZE, 600, "mono") + 16 <= inner && styles[seg.si].slot !== 5;
          const dimmed = dim !== null && dim !== seg.si;
          return (
            <g key={key} className={dimmed ? "rchart-dim" : undefined}>
              <rect className={hotKey === key ? "rchart-bar is-horizontal is-hot" : "rchart-bar is-horizontal"} x={seg.x0 + 1} y={seg.y} width={inner} height={seg.h} fill={styles[seg.si].color} rx={2} style={{ transformOrigin: "0% 50%" }} />
              {fits && (
                <text className="rchart-cell-label is-dark" x={(seg.x0 + seg.x1) / 2} y={seg.y + seg.h / 2} dy="0.35em" textAnchor="middle">
                  {text}
                </text>
              )}
            </g>
          );
        })}
      </svg>
    </ChartFrame>
  );
}
