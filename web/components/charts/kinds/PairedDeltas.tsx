"use client";
/**
 * `paired`: per-game paired deltas in seed order. Thin vertical bars from
 * zero (2-10 px), blue above and red below (the diverging pair); the
 * recorded mean delta as a solid ink line with its band (lo..hi) when the
 * record carries one; x markers (the half-split rule) as dashed hairlines.
 * Hover is a crosshair by game; the tooltip leads with the signed delta.
 */
import { useMemo, useRef } from "react";
import { describePoint } from "@/lib/charts/describe";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis } from "../axes/BottomAxis";
import { LeftAxis } from "../axes/LeftAxis";
import { ChartFrame } from "../frame/ChartFrame";
import type { LegendItem } from "../frame/Legend";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Bar } from "../marks/Bar";
import { Crosshair, ReferenceBand, ReferenceLine, ZeroLine } from "../marks/ReferenceLine";
import { axisScale, linearScale, scaleTicks } from "../scales";
import { DIV_NEG, DIV_POS, LABEL_SIZE, TICK_SIZE } from "../tokens";
import { axisText, domainOf, tickFormatter, valueRange, verticalBox, type KindProps } from "./shared";

export function PairedDeltas({ spec, id, title, height, compact, table, yDomain, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const series = spec.series[0];
  const points = series.points;
  const numericX = points.every((p) => typeof p.x === "number");

  const layout = useMemo(() => {
    const values = valueRange(spec, true);
    const yDom = domainOf(values, yDomain ?? spec.y?.domain);
    const yScale0 = axisScale(spec.y, yDom, [1, 0], yDomain ? 0 : 5);
    const yTicks = scaleTicks(yScale0, compact ? 4 : 5);
    const format = tickFormatter(spec.y);
    const box = verticalBox({ width, measure, tickLabels: yTicks.map(format), valueTitle: axisText(spec.y), xTitle: axisText(spec.x), xLabelHeight: TICK_SIZE + 8, height, compact });
    const yScale = axisScale(spec.y, yScale0.domain() as [number, number], [box.bottom, box.top], 0);
    const positions = points.map((p, i) => (numericX ? (p.x as number) : i + 1));
    const [xMin, xMax] = domainOf(positions);
    const xScale = linearScale([xMin - 0.5, xMax + 0.5], [box.left + 4, box.right - 4]);
    const xTicks = scaleTicks(xScale, Math.max(2, Math.floor((box.right - box.left) / 70)), true).filter((t) => t >= xMin && t <= xMax);
    const barWidth = Math.max(2, Math.min(10, ((box.right - box.left) / Math.max(points.length, 1)) * 0.7));
    return { box, yScale, xScale, yTicks, xTicks, format, positions, barWidth };
  }, [spec, points, numericX, width, measure, height, compact, yDomain]);

  const { box, yScale, xScale, positions, barWidth } = layout;
  const zero = yScale(0);

  const targets = useMemo<CursorTarget[]>(
    () =>
      points.map((p, i) => {
        const text = describePoint(spec, p, true);
        const head = numericX ? `game ${text.x}` : `game ${i + 1}, seed ${String(p.x)}`;
        return {
          key: `g-${i}`,
          x: xScale(positions[i]),
          y: box.top,
          content: { head, rows: [{ value: text.value, key: { color: p.y >= 0 ? DIV_POS : DIV_NEG, shape: "rect" }, hot: true }], details: text.details, source: text.source },
          aria: `${head}: ${text.value}${text.details.length ? `. ${text.details.join(". ")}` : ""}`,
        };
      }),
    [points, spec, numericX, xScale, positions, box.top],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "x", axis: "x", anchor: "crosshair", crosshairTop: box.top, radius: Math.max(12, barWidth * 2), viewBox: { width: box.width, height: box.height } });
  const legend: LegendItem[] = [
    { name: "candidate scored higher on that game", key: { color: DIV_POS, shape: "rect" } },
    { name: "reference scored higher on that game", key: { color: DIV_NEG, shape: "rect" } },
  ];
  const valueMarkers = (spec.markers ?? []).filter((m) => m.axis !== "x");
  const xMarkers = (spec.markers ?? []).filter((m) => m.axis === "x");
  if (valueMarkers.length) legend.push({ name: valueMarkers.map((m) => m.label).join("; "), key: { color: "var(--color-ink-1)", shape: "line", thin: true } });
  const titleId = id ? `${id}-title` : undefined;

  return (
    <ChartFrame id={id} title={title} frameRef={ref} legend={noLegend ? undefined : legend} evidence={spec.evidence} tooltip={cursor.tooltip} live={cursor.live} table={table} compact={compact}>
      <svg
        ref={svgRef}
        className="rchart-svg"
        width={box.width}
        height={box.height}
        viewBox={`0 0 ${box.width} ${box.height}`}
        role="group"
        aria-roledescription="paired-delta chart"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        <AxisTitle x={box.left} y={LABEL_SIZE} lines={box.valueTitle} />
        <LeftAxis scale={yScale} ticks={layout.yTicks} x={box.left} width={box.right - box.left} plotTop={box.top} plotBottom={box.bottom} format={layout.format} />
        <BottomAxis scale={xScale} ticks={layout.xTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={(v) => String(v)} />
        <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
        {valueMarkers.map((m, i) => (
          <g key={`vm-${i}`}>
            {m.lo !== undefined && m.hi !== undefined && <ReferenceBand orientation="h" from={yScale(m.lo)} to={yScale(m.hi)} start={box.left} end={box.right} />}
            {m.style !== "band" && <ReferenceLine orientation="h" at={yScale(m.value)} from={box.left} to={box.right} label={m.label} solid labelAt="end" />}
          </g>
        ))}
        {xMarkers.map((m, i) => (
          <ReferenceLine key={`xm-${i}`} orientation="v" at={xScale(m.value)} from={box.top} to={box.bottom} label={m.label} labelAt={xScale(m.value) > (box.left + box.right) / 2 ? "end" : "start"} />
        ))}
        <ZeroLine orientation="h" at={zero} from={box.left} to={box.right} />
        {points.map((p, i) => {
          const cx = xScale(positions[i]);
          const cy = yScale(p.y);
          return <Bar key={i} x={cx - barWidth / 2} y={Math.min(cy, zero)} w={barWidth} h={Math.abs(zero - cy)} end={p.y >= 0 ? "top" : "bottom"} color={p.y >= 0 ? DIV_POS : DIV_NEG} hot={cursor.index === i} radius={Math.min(2, barWidth / 2)} />;
        })}
        {cursor.index !== null && <Crosshair x={xScale(positions[cursor.index])} top={box.top} bottom={box.bottom} />}
      </svg>
    </ChartFrame>
  );
}
