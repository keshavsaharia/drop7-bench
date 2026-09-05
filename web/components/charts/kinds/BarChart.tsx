"use client";
/**
 * `bar`: counts and magnitudes by category. Vertical columns when the
 * categories are few and their labels fit their slots, otherwise horizontal
 * rows with a measured label gutter (or as the spec's `orientation` says).
 * Bars are at most 24 px thick, square at the baseline and rounded at the
 * data end, grouped with a 2 px surface gap, full opacity, no marker dot.
 * Whiskers are ink and sit beside the bar when the bound falls inside it.
 * The bar (plus its gap) is the hit target; keyboard walks categories with
 * one arrow pair and series with the other.
 */
import { useMemo, useRef, useState } from "react";
import { describePoint } from "@/lib/charts/describe";
import { groupLayout, rowBands } from "@/lib/charts/geometry";
import { isSignedKind } from "@/lib/charts/spec";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis, BottomCategories } from "../axes/BottomAxis";
import { LeftAxis } from "../axes/LeftAxis";
import { RowLabels } from "../axes/RowLabels";
import { ChartFrame } from "../frame/ChartFrame";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Bar } from "../marks/Bar";
import { ZeroLine } from "../marks/ReferenceLine";
import { Whisker } from "../marks/Whisker";
import { axisScale, bandScale, scaleTicks } from "../scales";
import { BAR_MAX, LABEL_SIZE, TICK_SIZE, VALUE_SIZE } from "../tokens";
import { axisText, categoriesOf, domainOf, horizontalBox, legendItems, pointIn, pointTarget, renderMarkers, rowHeightsFor, seriesIn, seriesStyles, tickFormatter, valueRange, verticalBox, wrapLabel, type KindProps } from "./shared";

const MAX_VERTICAL = 6;

export function BarChart({ spec, id, title, height, compact, table, yDomain, overrides, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  const styles = useMemo(() => seriesStyles(spec, "bar", overrides), [spec, overrides]);
  const signed = isSignedKind(spec);
  const isTime = spec.x?.scale === "time";

  const layout = useMemo(() => {
    const cats = categoriesOf(spec);
    const lanes = cats.map((c) => seriesIn(spec, c).length);
    const values = valueRange(spec, true);
    const vDom = domainOf(values, yDomain ?? spec.y?.domain);
    const format = tickFormatter(spec.y);
    let horizontal = spec.orientation === "horizontal" || (spec.orientation !== "vertical" && cats.length > MAX_VERTICAL);
    let slotLabels: { lines: string[]; fits: boolean }[] = [];
    if (!horizontal) {
      const provisionalLeft = 60;
      const slot = (width - 16 - provisionalLeft) / cats.length;
      slotLabels = cats.map((c) => wrapLabel(c, slot - 8, measure, 3));
      if (spec.orientation !== "vertical" && slotLabels.some((l) => !l.fits)) horizontal = true;
    }
    if (horizontal) {
      const rowHeightsGuess = cats.map(() => 0);
      const probe = horizontalBox({ width, measure, categories: cats, rowHeights: rowHeightsGuess, valueTitle: axisText(spec.y) });
      const laneHeight = Math.min(BAR_MAX, 18) + 4;
      const rowHeights = rowHeightsFor(probe.labels, lanes, laneHeight, 30);
      const box = horizontalBox({ width, measure, categories: cats, rowHeights, valueTitle: axisText(spec.y) });
      const vScale = axisScale(spec.y, vDom, [box.left, box.right], yDomain ? 0 : 5);
      const vTicks = scaleTicks(vScale, Math.max(3, Math.floor((box.right - box.left) / 80)));
      const bands = rowBands(box.top, rowHeights);
      return { horizontal: true as const, cats, lanes, box, vScale, vTicks, format, rowHeights, bands, labels: box.labels, slotLabels: [] as { lines: string[] }[], band: null };
    }
    const vScale0 = axisScale(spec.y, vDom, [1, 0], yDomain ? 0 : 5);
    const vTicks = scaleTicks(vScale0, compact ? 4 : 5);
    const labelLines = Math.max(1, ...slotLabels.map((l) => l.lines.length));
    const tipLabels = spec.valueLabels === "tip" ? VALUE_SIZE + 6 : 0;
    const box = verticalBox({ width, measure, tickLabels: vTicks.map(format), valueTitle: axisText(spec.y), xTitle: axisText(spec.x), xLabelHeight: labelLines * (TICK_SIZE + 3) + 6, height, compact });
    const plotTop = box.top + tipLabels;
    const vScale = axisScale(spec.y, vScale0.domain() as [number, number], [box.bottom, plotTop], 0);
    const band = bandScale(cats, [box.left, box.right], 0.3);
    return { horizontal: false as const, cats, lanes, box: { ...box, top: plotTop }, vScale, vTicks, format, rowHeights: [] as number[], bands: null, labels: [] as { lines: string[] }[], slotLabels, band };
  }, [spec, width, measure, height, compact, yDomain]);

  const { box, vScale, cats } = layout;
  const zero = vScale(0);

  const placed = useMemo(() => {
    const bars: { si: number; ci: number; x: number; y: number; w: number; h: number; end: "top" | "bottom" | "left" | "right"; value: number; cx: number; cy: number; lo?: number; hi?: number; thick: number }[] = [];
    cats.forEach((cat, ci) => {
      const present = seriesIn(spec, cat);
      if (layout.horizontal) {
        const rowTop = layout.bands!.tops[ci];
        const rowH = layout.rowHeights[ci];
        const group = groupLayout(present.length, rowH, 18, 2, 0.8);
        present.forEach((si, lane) => {
          const p = pointIn(spec, si, cat)!;
          const y = rowTop + rowH / 2 + group.offsets[lane];
          const vx = vScale(p.y);
          const x = Math.min(vx, zero);
          const w = Math.abs(vx - zero);
          bars.push({ si, ci, x, y, w, h: group.thick, end: p.y >= 0 ? "right" : "left", value: p.y, cx: vx, cy: y + group.thick / 2, lo: p.lo, hi: p.hi, thick: group.thick });
        });
      } else {
        const band = layout.band!;
        const centre = band(cat) + band.bandwidth() / 2;
        const group = groupLayout(present.length, band.bandwidth(), BAR_MAX, 2, 1);
        present.forEach((si, lane) => {
          const p = pointIn(spec, si, cat)!;
          const x = centre + group.offsets[lane];
          const vy = vScale(p.y);
          const y = Math.min(vy, zero);
          const h = Math.abs(zero - vy);
          bars.push({ si, ci, x, y, w: group.thick, h, end: p.y >= 0 ? "top" : "bottom", value: p.y, cx: x + group.thick / 2, cy: vy, lo: p.lo, hi: p.hi, thick: group.thick });
        });
      }
    });
    return bars;
  }, [cats, spec, layout, vScale, zero]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      placed.map((bar) => {
        const p = pointIn(spec, bar.si, cats[bar.ci])!;
        const pad = 3;
        return pointTarget(`${bar.si}-${bar.ci}`, bar.cx, layout.horizontal ? bar.cy : bar.y, spec, bar.si, p, styles, {
          rect: { x: bar.x - pad, y: bar.y - pad, w: bar.w + pad * 2, h: bar.h + pad * 2 },
          row: bar.ci,
          col: bar.si,
          signed,
          head: isTime ? describePoint(spec, p, signed).x : cats[bar.ci],
        });
      }),
    [placed, spec, cats, styles, layout.horizontal, signed, isTime],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({
    targets,
    svgRef,
    hit: "rect",
    grid: { rows: cats.length, cols: spec.series.length, primary: layout.horizontal ? "row" : "col" },
    anchor: "mark",
    viewBox: { width: box.width, height: box.height },
  });
  const hotKey = cursor.target?.key ?? null;
  const crossesZero = (vScale.domain()[0] as number) < 0 && (vScale.domain()[1] as number) > 0;
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
        aria-roledescription="bar chart"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        {layout.horizontal ? (
          <>
            <RowLabels rows={cats.map((_, ci) => ({ y: layout.bands!.tops[ci] + layout.rowHeights[ci] / 2, bottom: layout.bands!.edges[ci + 1], lines: layout.labels[ci].lines }))} x={box.left} plotRight={box.right} />
            <BottomAxis scale={vScale} ticks={layout.vTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={layout.format} grid />
            <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
            {crossesZero && <ZeroLine orientation="v" at={zero} from={box.top} to={box.bottom} />}
            {renderMarkers(spec, vScale, "x", box)}
          </>
        ) : (
          <>
            <AxisTitle x={box.left} y={LABEL_SIZE} lines={box.valueTitle} />
            <LeftAxis scale={vScale} ticks={layout.vTicks} x={box.left} width={box.right - box.left} plotTop={box.top} plotBottom={box.bottom} format={layout.format} />
            <BottomCategories items={cats.map((cat, ci) => ({ x: layout.band!(cat) + layout.band!.bandwidth() / 2, lines: layout.slotLabels[ci]?.lines ?? [cat] }))} y={box.bottom} plotLeft={box.left} plotRight={box.right} />
            <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
            {crossesZero && <ZeroLine orientation="h" at={zero} from={box.left} to={box.right} />}
            {renderMarkers(spec, vScale, "y", box)}
          </>
        )}
        {placed.map((bar) => {
          const key = `${bar.si}-${bar.ci}`;
          const dimmed = dim !== null && dim !== bar.si;
          const style = styles[bar.si];
          const hasBound = bar.lo !== undefined || bar.hi !== undefined;
          let whisker: React.ReactNode = null;
          if (hasBound) {
            const lo = bar.lo ?? bar.value;
            const hi = bar.hi ?? bar.value;
            const inside = (b: number) => b >= Math.min(0, bar.value) && b <= Math.max(0, bar.value);
            const beside = (bar.lo !== undefined && inside(bar.lo)) || (bar.hi !== undefined && inside(bar.hi));
            const open = bar.hi === undefined ? "end" : bar.lo === undefined ? "start" : null;
            if (layout.horizontal) {
              const wy = beside ? bar.y + bar.h + 6 : bar.cy;
              whisker = <Whisker x1={vScale(lo)} x2={vScale(hi)} y1={wy} y2={wy} open={open} dim={dimmed} />;
            } else {
              const wx = beside ? bar.x + bar.w + 6 : bar.cx;
              whisker = <Whisker x1={wx} x2={wx} y1={vScale(lo)} y2={vScale(hi)} open={open} dim={dimmed} />;
            }
          }
          return (
            <g key={key}>
              <Bar x={bar.x} y={bar.y} w={bar.w} h={bar.h} end={bar.end} color={style.color} hot={hotKey === key} dim={dimmed} />
              {whisker}
              {spec.valueLabels === "tip" &&
                (layout.horizontal ? (
                  <text className="rchart-value-label" x={bar.end === "right" ? bar.x + bar.w + 6 : bar.x - 6} y={bar.cy} dy="0.35em" textAnchor={bar.end === "right" ? "start" : "end"}>
                    {describePoint(spec, pointIn(spec, bar.si, cats[bar.ci])!, signed).value}
                  </text>
                ) : (
                  <text className="rchart-value-label" x={bar.cx} y={bar.end === "top" ? bar.y - 5 : bar.y + bar.h + VALUE_SIZE + 2} textAnchor="middle">
                    {describePoint(spec, pointIn(spec, bar.si, cats[bar.ci])!, signed).value}
                  </text>
                ))}
            </g>
          );
        })}
      </svg>
    </ChartFrame>
  );
}
