"use client";
/**
 * `strip`: one dot per game. Two orientations:
 *   horizontal (default) - the value runs along x and dots that share a 7 px
 *     bucket stack upward, one lane per series: the heavy-tail picture, with
 *     recorded quantiles and the target as vertical marker lines;
 *   vertical - the value runs along y and each dot sits at its own x (a game
 *     index, a seed in order, a generation): a scatter in recorded order.
 * Censored games are hollow. Hover finds the nearest dot within 24 px; the
 * keyboard walks dots in x order. Nothing is binned or summarised here.
 */
import { useMemo, useRef, useState } from "react";
import { bucketLevels } from "@/lib/charts/geometry";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis } from "../axes/BottomAxis";
import { LeftAxis } from "../axes/LeftAxis";
import { RowLabels } from "../axes/RowLabels";
import { ChartFrame } from "../frame/ChartFrame";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Marker } from "../marks/Marker";
import { axisScale, linearScale, scaleTicks } from "../scales";
import { LABEL_SIZE, STRIP_BUCKET, TICK_SIZE } from "../tokens";
import { axisText, domainOf, legendItems, pointTarget, renderMarkers, seriesStyles, tickFormatter, valueRange, verticalBox, widest, type KindProps } from "./shared";

export function Strip({ spec, id, title, height, compact, table, yDomain, xDomain, overrides, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  const styles = useMemo(() => seriesStyles(spec, "mark", overrides), [spec, overrides]);
  const vertical = spec.orientation === "vertical";
  const total = spec.series.reduce((n, s) => n + s.points.length, 0);
  const r = total > 300 ? 2.6 : total > 120 ? 3.2 : 4;

  const layout = useMemo(() => {
    const values = valueRange(spec, false);
    const vDom = domainOf(values, yDomain ?? spec.y?.domain);
    const format = tickFormatter(spec.y);
    if (vertical) {
      const vScale0 = axisScale(spec.y, vDom, [1, 0], yDomain ? 0 : 5);
      const vTicks = scaleTicks(vScale0, compact ? 4 : 5);
      const box = verticalBox({ width, measure, tickLabels: vTicks.map(format), valueTitle: axisText(spec.y), xTitle: axisText(spec.x), xLabelHeight: TICK_SIZE + 8, height, compact });
      const vScale = axisScale(spec.y, vScale0.domain() as [number, number], [box.bottom, box.top], 0);
      const numericX = spec.series.every((s) => s.points.every((p) => typeof p.x === "number"));
      const xOf = (si: number, pi: number): number => (numericX ? (spec.series[si].points[pi].x as number) : pi + 1);
      const xs = spec.series.flatMap((s, si) => s.points.map((_, pi) => xOf(si, pi)));
      const xMarkers = (spec.markers ?? []).filter((m) => m.axis === "x").map((m) => m.value);
      const [xMin, xMax] = domainOf([...xs, ...xMarkers], xDomain);
      const xScale = linearScale([xMin - 0.5, xMax + 0.5], [box.left + 6, box.right - 6]);
      const xTicks = scaleTicks(xScale, Math.max(2, Math.floor((box.right - box.left) / 70)), true).filter((t) => t >= xMin && t <= xMax);
      return { vertical: true as const, box, vScale, vTicks, format, xScale, xTicks, xOf, lanes: null };
    }
    // Horizontal: lanes stacked by bucket.
    const vProbe = axisScale(spec.y, vDom, [0, 1], yDomain ? 0 : 5);
    const vTicks = scaleTicks(vProbe, Math.max(3, Math.floor(width / 90)));
    const labelWidth = spec.series.length > 1 ? Math.ceil(widest(spec.series.map((s) => s.name), measure, TICK_SIZE, "sans")) + 16 : 12;
    const left = Math.min(labelWidth, width * 0.4);
    const right = width - 20;
    const vScale = axisScale(spec.y, vProbe.domain() as [number, number], [left, right], 0);
    const laneLevels = spec.series.map((s) => bucketLevels(s.points.map((p) => vScale(p.y)), STRIP_BUCKET));
    const laneHeights = laneLevels.map((levels) => Math.max(40, (Math.max(0, ...levels) + 1) * STRIP_BUCKET + 18));
    const hasMarkers = (spec.markers ?? []).some((m) => m.axis !== "x");
    const top = hasMarkers ? 18 : 8;
    const tops: number[] = [];
    let acc = top;
    for (const h of laneHeights) {
      tops.push(acc);
      acc += h;
    }
    const bottom = acc;
    const xt = axisText(spec.y);
    const titleH = xt ? LABEL_SIZE + 8 : 0;
    const totalH = bottom + TICK_SIZE + 14 + titleH + 2;
    const box = { width, height: totalH, left, right, top, bottom, valueTitle: [] as string[], xTitle: xt ? [xt] : [], xTitleY: totalH - 4 };
    return { vertical: false as const, box, vScale, vTicks, format, xScale: null, xTicks: null, xOf: null, lanes: { levels: laneLevels, heights: laneHeights, tops } };
  }, [spec, vertical, width, measure, height, compact, yDomain, xDomain]);

  const { box, vScale } = layout;

  const placed = useMemo(() => {
    const out: { si: number; pi: number; cx: number; cy: number }[] = [];
    spec.series.forEach((s, si) => {
      s.points.forEach((p, pi) => {
        if (layout.vertical) {
          out.push({ si, pi, cx: layout.xScale!(layout.xOf!(si, pi)), cy: vScale(p.y) });
        } else {
          const lane = layout.lanes!;
          const level = lane.levels[si][pi];
          const laneBottom = lane.tops[si] + lane.heights[si] - 8;
          out.push({ si, pi, cx: vScale(p.y), cy: laneBottom - r - level * STRIP_BUCKET });
        }
      });
    });
    return out.sort((a, b) => a.cx - b.cx || a.cy - b.cy);
  }, [spec, layout, vScale, r]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      placed.map((m) => {
        const p = spec.series[m.si].points[m.pi];
        const head = typeof p.x === "string" ? p.x : `${spec.x?.label ?? "x"} ${String(p.x)}`;
        return pointTarget(`${m.si}-${m.pi}`, m.cx, m.cy, spec, m.si, p, styles, { head, signed: false, named: spec.series.length > 1 });
      }),
    [placed, spec, styles],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "point", axis: "x", anchor: "mark", viewBox: { width: box.width, height: box.height } });
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
        aria-roledescription="strip plot"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        {layout.vertical ? (
          <>
            <AxisTitle x={box.left} y={LABEL_SIZE} lines={box.valueTitle} />
            <LeftAxis scale={vScale} ticks={layout.vTicks} x={box.left} width={box.right - box.left} plotTop={box.top} plotBottom={box.bottom} format={layout.format} />
            <BottomAxis scale={layout.xScale!} ticks={layout.xTicks!} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={tickFormatter(spec.x)} />
            <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
            {renderMarkers(spec, vScale, "y", box, (x) => layout.xScale!(x))}
          </>
        ) : (
          <>
            {spec.series.length > 1 && <RowLabels rows={spec.series.map((s, si) => ({ y: layout.lanes!.tops[si] + layout.lanes!.heights[si] / 2, bottom: layout.lanes!.tops[si] + layout.lanes!.heights[si], lines: [s.name] }))} x={box.left} plotRight={box.right} />}
            <BottomAxis scale={vScale} ticks={layout.vTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={layout.format} grid />
            <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
            {renderMarkers(spec, vScale, "x", box)}
          </>
        )}
        {placed.map((m) => {
          const p = spec.series[m.si].points[m.pi];
          const style = styles[m.si];
          const key = `${m.si}-${m.pi}`;
          return <Marker key={key} x={m.cx} y={m.cy} r={r} color={style.color} hollow={style.hollow || Boolean(p.censored)} hot={hotKey === key} dim={dim !== null && dim !== m.si} />;
        })}
      </svg>
    </ChartFrame>
  );
}
