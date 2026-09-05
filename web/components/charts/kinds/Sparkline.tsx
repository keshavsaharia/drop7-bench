"use client";
/**
 * A sparkline for a stat tile: 12-40 recorded points, a 2 px line in the
 * de-emphasis grey with the last point marked in the accent slot, no axes,
 * an optional recorded reference as a dashed hairline. Hover shows a
 * crosshair tooltip (value, x, source); the svg is focusable and the arrow
 * keys walk the points. Never rendered without the parent tile's label and
 * source, which is why it takes them as props.
 */
import "../charts.css";
import { useMemo, useRef } from "react";
import { formatValue } from "@/lib/charts/spec";
import { ChartTooltip } from "../frame/Tooltip";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Line, type Pt } from "../marks/Line";
import { Marker } from "../marks/Marker";
import { Crosshair, ReferenceLine } from "../marks/ReferenceLine";
import { linearScale } from "../scales";
import { DEEMPHASIS, SERIES } from "../tokens";

export interface SparkPoint {
  x: number | string;
  y: number;
  sourceRecord?: string;
  sourceField?: string;
}

export interface SparklineProps {
  points: SparkPoint[];
  label: string;
  unit?: string;
  source?: string;
  reference?: { value: number; label: string };
  height?: number;
}

export function Sparkline({ points, label, unit, source, reference, height = 28 }: SparklineProps) {
  const { ref, width } = useChartLayout(120);

  const layout = useMemo(() => {
    const ys = points.map((p) => p.y);
    if (reference) ys.push(reference.value);
    const lo = Math.min(...ys);
    const hi = Math.max(...ys);
    const pad = (hi - lo || 1) * 0.1;
    const yScale = linearScale([lo - pad, hi + pad], [height - 3, 3]);
    const xScale = linearScale([0, Math.max(points.length - 1, 1)], [3, width - 4]);
    const pts: Pt[] = points.map((p, i) => [xScale(i), yScale(p.y)]);
    return { yScale, xScale, pts };
  }, [points, reference, width, height]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      points.map((p, i) => {
        const value = formatValue(p.y, unit);
        const at = String(p.x);
        return {
          key: String(i),
          x: layout.pts[i][0],
          y: 0,
          content: { head: label, rows: [{ value, label: at, hot: true }], source: p.sourceRecord ? `${p.sourceRecord}${p.sourceField ? ` · ${p.sourceField}` : ""}` : source },
          aria: `${value}, ${label}, ${at}`,
        };
      }),
    [points, layout.pts, label, unit, source],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "x", axis: "x", anchor: "crosshair", crosshairTop: 0, radius: 24, viewBox: { width, height } });
  const last = layout.pts[layout.pts.length - 1];

  return (
    <div className="rchart rchart-spark" ref={ref}>
      <svg ref={svgRef} className="rchart-svg" width={width} height={height} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`${label}: ${points.length} points`} tabIndex={0} {...cursor.handlers}>
        {reference && <ReferenceLine orientation="h" at={layout.yScale(reference.value)} from={3} to={width - 4} />}
        <Line points={layout.pts} color={DEEMPHASIS} />
        {last && <Marker x={last[0]} y={last[1]} r={3} color={SERIES(0)} />}
        {cursor.index !== null && (
          <>
            <Crosshair x={layout.pts[cursor.index][0]} top={0} bottom={height} />
            <Marker x={layout.pts[cursor.index][0]} y={layout.pts[cursor.index][1]} r={3.5} color={SERIES(0)} hot />
          </>
        )}
      </svg>
      <div className="rchart-live" aria-live="polite">
        {cursor.live}
      </div>
      <ChartTooltip state={cursor.tooltip} />
    </div>
  );
}
