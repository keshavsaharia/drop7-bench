"use client";
/**
 * `line`: a value against a numeric, log or time x. Series roles set the
 * stroke (primary solid, control dashed, reference/context grey, band a
 * 10 % wash of lo..hi). Hover is a vertical crosshair snapping to the
 * nearest x among all series with one tooltip listing every series at that
 * x, value first. The svg is one tab stop: ArrowLeft/Right move the
 * crosshair, Home/End jump, Escape clears. Whiskers are ink; end labels are
 * drawn when the spec asks or when four or more series share the plot, and
 * a label that would collide is dropped rather than nudged.
 */
import { useMemo, useRef, useState } from "react";
import { describePoint } from "@/lib/charts/describe";
import { placeEndLabels } from "@/lib/charts/geometry";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis } from "../axes/BottomAxis";
import { LeftAxis } from "../axes/LeftAxis";
import { ChartFrame } from "../frame/ChartFrame";
import type { TooltipRow } from "../frame/Tooltip";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Band } from "../marks/Band";
import { Line, type Pt } from "../marks/Line";
import { Marker } from "../marks/Marker";
import { Crosshair } from "../marks/ReferenceLine";
import { Whisker } from "../marks/Whisker";
import { axisScale, scaleTicks } from "../scales";
import { LABEL_SIZE, TICK_SIZE, VALUE_SIZE } from "../tokens";
import { axisText, domainOf, legendItems, numericX, renderMarkers, seriesStyles, tickFormatter, valueRange, verticalBox, widest, type KindProps } from "./shared";

export function LineChart({ spec, id, title, height, compact, table, yDomain, xDomain, overrides, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  const styles = useMemo(() => seriesStyles(spec, "line", overrides), [spec, overrides]);
  const isTime = spec.x?.scale === "time";
  const isLog = spec.x?.scale === "log";

  const layout = useMemo(() => {
    const xs = spec.series.flatMap((s) => s.points.map((p) => numericX(p, spec.x)));
    const xMarkers = (spec.markers ?? []).filter((m) => m.axis === "x").map((m) => m.value);
    const [xMin, xMax] = domainOf([...xs, ...xMarkers], xDomain);
    const allInteger = !isTime && xs.every((x) => Number.isInteger(x));
    const yValues = valueRange(spec, false);
    const yDom = domainOf(yValues, yDomain ?? spec.y?.domain);
    const yScale0 = axisScale(spec.y, yDom, [1, 0], yDomain || spec.y?.domain ? 0 : 5);
    const yTicks = scaleTicks(yScale0, compact ? 4 : 5);
    const format = tickFormatter(spec.y);
    const wantEndLabels = spec.valueLabels === "end" || spec.series.filter((s) => s.role !== "band").length >= 4;
    const endLabelWidth = wantEndLabels ? Math.ceil(widest(spec.series.map((s) => s.name), measure, VALUE_SIZE, "sans")) + 10 : 0;
    const box = verticalBox({ width, measure, tickLabels: yTicks.map(format), valueTitle: axisText(spec.y), xTitle: axisText(spec.x), xLabelHeight: TICK_SIZE + 8, height, compact, rightPad: 16 + endLabelWidth });
    const yScale = axisScale(spec.y, yScale0.domain() as [number, number], [box.bottom, box.top], 0);
    const pad = isLog ? 0 : isTime ? 43_200_000 : (xMax - xMin || 1) * 0.06;
    const xScale = axisScale(spec.x, isLog ? [xMin / 1.3, xMax * 1.3] : [xMin - pad, xMax + pad], [box.left + 8, box.right - 8], 0);
    const xTicks = scaleTicks(xScale, Math.max(2, Math.floor((box.right - box.left) / 90)), allInteger);
    const columns = [...new Set(xs)].sort((a, b) => a - b);
    return { box, yScale, xScale, yTicks, xTicks, columns, format, wantEndLabels };
  }, [spec, width, measure, height, compact, yDomain, xDomain, isTime, isLog]);

  const { box, yScale, xScale, columns } = layout;

  const targets = useMemo<CursorTarget[]>(
    () =>
      columns.map((x, ci) => {
        const rows: TooltipRow[] = [];
        const details: string[] = [];
        const sources = new Set<string>();
        let head = "";
        let aria = "";
        spec.series.forEach((series, si) => {
          if (styles[si].band) return;
          const point = series.points.find((p) => numericX(p, spec.x) === x);
          if (!point) return;
          const text = describePoint(spec, point, false);
          head = text.x;
          rows.push({ value: text.value, label: series.name, key: styles[si].key });
          if (spec.series.length === 1) details.push(...text.details);
          else if (text.details.length) details.push(`${series.name}: ${text.details.join("; ")}`);
          sources.add(text.source);
          aria += `${aria ? "; " : ""}${text.value} ${series.name}`;
        });
        return { key: `c-${ci}`, x: xScale(x), y: box.top, content: { head, rows, details, source: [...sources].join(" · ") }, aria: `${head}: ${aria}` };
      }),
    [columns, spec, styles, xScale, box.top],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "x", axis: "x", anchor: "crosshair", crosshairTop: box.top, viewBox: { width: box.width, height: box.height } });
  const hotX = cursor.index !== null ? columns[cursor.index] : null;

  const endLabels = useMemo(() => {
    if (!layout.wantEndLabels) return [];
    const entries = spec.series
      .map((series, si) => {
        if (styles[si].band || series.points.length === 0) return null;
        const last = [...series.points].sort((a, b) => numericX(a, spec.x) - numericX(b, spec.x))[series.points.length - 1];
        return { si, x: xScale(numericX(last, spec.x)), y: yScale(last.y), name: series.name };
      })
      .filter((e): e is { si: number; x: number; y: number; name: string } => e !== null);
    const keep = placeEndLabels(
      entries.map((e) => e.y),
      VALUE_SIZE + 2,
    );
    return entries.filter((_, i) => keep[i]);
  }, [layout.wantEndLabels, spec, styles, xScale, yScale]);

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
        aria-roledescription="line chart"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        <AxisTitle x={box.left} y={LABEL_SIZE} lines={box.valueTitle} />
        <LeftAxis scale={yScale} ticks={layout.yTicks} x={box.left} width={box.right - box.left} plotTop={box.top} plotBottom={box.bottom} format={layout.format} />
        <BottomAxis scale={xScale} ticks={layout.xTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={tickFormatter(spec.x)} />
        <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
        {renderMarkers(spec, yScale, "y", box, (x) => xScale(x))}
        {spec.series.map((series, si) => {
          const style = styles[si];
          const sorted = [...series.points].sort((a, b) => numericX(a, spec.x) - numericX(b, spec.x));
          const dimmed = dim !== null && dim !== si;
          if (style.band) {
            const upper: Pt[] = sorted.map((p) => [xScale(numericX(p, spec.x)), yScale(p.hi ?? p.y)]);
            const lower: Pt[] = sorted.map((p) => [xScale(numericX(p, spec.x)), yScale(p.lo ?? p.y)]);
            return <Band key={si} upper={upper} lower={lower} color={style.color} dim={dimmed} />;
          }
          const pts: Pt[] = sorted.map((p) => [xScale(numericX(p, spec.x)), yScale(p.y)]);
          return (
            <g key={si} className={dimmed ? "rchart-series rchart-dim" : "rchart-series"}>
              <Line points={pts} color={style.color} dashed={style.dashed} thin={style.thin} />
              {sorted.map((p, pi) => {
                const cx = xScale(numericX(p, spec.x));
                return (
                  <g key={pi}>
                    {(p.lo !== undefined || p.hi !== undefined) && <Whisker x1={cx} x2={cx} y1={yScale(p.lo ?? p.y)} y2={yScale(p.hi ?? p.y)} open={p.hi === undefined ? "end" : p.lo === undefined ? "start" : null} />}
                    <Marker x={cx} y={yScale(p.y)} color={style.color} hollow={style.hollow} hot={hotX !== null && numericX(p, spec.x) === hotX} />
                  </g>
                );
              })}
            </g>
          );
        })}
        {endLabels.map((label) => (
          <text key={label.si} className="rchart-end-label" x={label.x + 8} y={label.y} dy="0.35em">
            {label.name}
          </text>
        ))}
        {hotX !== null && <Crosshair x={xScale(hotX)} top={box.top} bottom={box.bottom} />}
      </svg>
    </ChartFrame>
  );
}
