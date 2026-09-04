"use client";
/**
 * The shared renderer for the three row kinds around zero:
 *   delta  - a bar from zero to the recorded delta, blue above and red below
 *            zero (the diverging pair), an ink whisker over the bar with a
 *            surface ring, a translucent detection-floor band behind the row;
 *   forest - the same rows drawn as a dot with whiskers, hollow diamond for
 *            a reference series, floors as bands;
 *   dot    - categorical rows with every series on ONE baseline (a hairline
 *            connects the leftmost and rightmost value: a dumbbell).
 * Rows keep the spec order. The row is the hit target; Up/Down step rows and
 * Left/Right step the series inside a row.
 */
import { useMemo, useRef, useState } from "react";
import { describePoint } from "@/lib/charts/describe";
import { groupLayout, rowBands } from "@/lib/charts/geometry";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomAxis } from "../axes/BottomAxis";
import { RowLabels } from "../axes/RowLabels";
import { ChartFrame } from "../frame/ChartFrame";
import type { LegendItem } from "../frame/Legend";
import type { TooltipRow } from "../frame/Tooltip";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Bar } from "../marks/Bar";
import { Marker } from "../marks/Marker";
import { ReferenceBand, ZeroLine } from "../marks/ReferenceLine";
import { Whisker } from "../marks/Whisker";
import { axisScale, scaleTicks } from "../scales";
import { DELTA_BAR_MAX, DIV_NEG, DIV_POS, DOT_R } from "../tokens";
import { axisText, categoriesOf, domainOf, horizontalBox, legendItems, pointIn, pointTarget, renderMarkers, rowHeightsFor, seriesIn, seriesStyles, siblingRow, tickFormatter, valueRange, type KindProps } from "./shared";

export function RowChart({ spec, id, title, compact, table, yDomain, overrides, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  const kind = spec.kind === "delta" ? "delta" : spec.kind === "forest" ? "forest" : "dot";
  const signed = kind !== "dot";
  const styles = useMemo(() => seriesStyles(spec, kind === "dot" || kind === "forest" ? "mark" : "bar", overrides), [spec, kind, overrides]);

  const layout = useMemo(() => {
    const cats = categoriesOf(spec);
    const lanes = cats.map((c) => seriesIn(spec, c).length);
    const probe = horizontalBox({ width, measure, categories: cats, rowHeights: cats.map(() => 0), valueTitle: axisText(spec.y) });
    const laneHeight = kind === "delta" ? DELTA_BAR_MAX + 4 : kind === "dot" ? 0 : 14;
    const rowHeights = kind === "dot" ? rowHeightsFor(probe.labels, cats.map(() => 1), 22, 30) : rowHeightsFor(probe.labels, lanes, laneHeight, kind === "delta" ? 30 : 28);
    const box = horizontalBox({ width, measure, categories: cats, rowHeights, valueTitle: axisText(spec.y) });
    const values = valueRange(spec, kind !== "dot", kind !== "dot");
    const vDom = domainOf(values, yDomain ?? spec.y?.domain);
    const vScale = axisScale(spec.y, vDom, [box.left, box.right], yDomain || spec.y?.domain ? 0 : 5);
    const vTicks = scaleTicks(vScale, Math.max(3, Math.floor((box.right - box.left) / 80)));
    const bands = rowBands(box.top, rowHeights);
    return { cats, lanes, box, vScale, vTicks, rowHeights, bands, labels: box.labels, format: tickFormatter(spec.y) };
  }, [spec, width, measure, kind, yDomain]);

  const { cats, box, vScale, bands } = layout;
  const zero = vScale(0);

  interface Placed {
    si: number;
    ci: number;
    cy: number;
    cx: number;
    thick: number;
    value: number;
    lo?: number;
    hi?: number;
    floor?: number;
  }

  const placed = useMemo<Placed[]>(() => {
    const out: Placed[] = [];
    cats.forEach((cat, ci) => {
      const present = seriesIn(spec, cat);
      const rowTop = bands.tops[ci];
      const rowH = layout.rowHeights[ci];
      const group = kind === "dot" ? null : groupLayout(present.length, rowH, kind === "delta" ? DELTA_BAR_MAX : 12, kind === "delta" ? 3 : 4, 0.85);
      present.forEach((si, lane) => {
        const p = pointIn(spec, si, cat)!;
        const cy = group ? rowTop + rowH / 2 + group.offsets[lane] + group.thick / 2 : rowTop + rowH / 2;
        out.push({ si, ci, cy, cx: vScale(p.y), thick: group?.thick ?? 0, value: p.y, lo: p.lo, hi: p.hi, floor: p.floor });
      });
    });
    return out;
  }, [cats, spec, bands, layout.rowHeights, kind, vScale]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      placed.map((m) => {
        const p = pointIn(spec, m.si, cats[m.ci])!;
        const siblings: TooltipRow[] = kind === "dot" ? placed.filter((o) => o.ci === m.ci && o.si !== m.si).map((o) => siblingRow(spec, o.si, pointIn(spec, o.si, cats[o.ci])!, styles, false)) : [];
        const key = kind === "delta" ? { color: m.value >= 0 ? DIV_POS : DIV_NEG, shape: "rect" as const } : undefined;
        return pointTarget(`${m.si}-${m.ci}`, m.cx, m.cy, spec, m.si, p, styles, { row: m.ci, col: m.si, signed, head: cats[m.ci], siblings, key, named: spec.series.length > 1 });
      }),
    [placed, spec, cats, styles, kind, signed],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "row", rowEdges: bands.edges, grid: { rows: cats.length, cols: spec.series.length, primary: "row" }, anchor: "mark", viewBox: { width: box.width, height: box.height } });
  const hotKey = cursor.target?.key ?? null;
  const crossesZero = (vScale.domain()[0] as number) < 0 && (vScale.domain()[1] as number) > 0;
  const hasFloor = placed.some((m) => m.floor !== undefined);

  const legend: LegendItem[] | undefined = useMemo(() => {
    if (noLegend) return undefined;
    if (kind !== "delta") return legendItems(spec, styles);
    const items: LegendItem[] = [
      { name: "positive: candidate above its reference", key: { color: DIV_POS, shape: "rect" } },
      { name: "negative: candidate below its reference", key: { color: DIV_NEG, shape: "rect" } },
    ];
    if (hasFloor) items.push({ name: "recorded detection floor (±)", key: { color: "var(--chart-div-mid)", shape: "band" } });
    return items;
  }, [noLegend, kind, spec, styles, hasFloor]);

  const titleId = id ? `${id}-title` : undefined;
  const role = kind === "delta" ? "paired-delta chart" : kind === "forest" ? "forest plot" : "dot plot";

  return (
    <ChartFrame id={id} title={title} frameRef={ref} legend={legend} legendActive={dim} onLegendHover={setDim} evidence={spec.evidence} tooltip={cursor.tooltip} live={cursor.live} table={table} compact={compact}>
      <svg
        ref={svgRef}
        className="rchart-svg"
        width={box.width}
        height={box.height}
        viewBox={`0 0 ${box.width} ${box.height}`}
        role="group"
        aria-roledescription={role}
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        {/* detection-floor bands behind the rows */}
        {placed
          .filter((m) => m.floor !== undefined)
          .map((m) => (
            <ReferenceBand key={`floor-${m.si}-${m.ci}`} orientation="v" from={vScale(-(m.floor as number))} to={vScale(m.floor as number)} start={bands.tops[m.ci] + 2} end={bands.edges[m.ci + 1] - 2} />
          ))}
        <RowLabels rows={cats.map((_, ci) => ({ y: bands.tops[ci] + layout.rowHeights[ci] / 2, bottom: bands.edges[ci + 1], lines: layout.labels[ci].lines }))} x={box.left} plotRight={box.right} />
        <BottomAxis scale={vScale} ticks={layout.vTicks} y={box.bottom} plotTop={box.top} plotLeft={box.left} plotRight={box.right} format={layout.format} grid />
        <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
        {crossesZero && <ZeroLine orientation="v" at={zero} from={box.top} to={box.bottom} />}
        {renderMarkers(spec, vScale, "x", box)}
        {/* dumbbell hairlines: one baseline per row */}
        {kind === "dot" &&
          cats.map((_, ci) => {
            const row = placed.filter((m) => m.ci === ci);
            if (row.length < 2) return null;
            const xs = row.map((m) => m.cx);
            return <line key={`hair-${ci}`} className="rchart-hair" x1={Math.min(...xs)} x2={Math.max(...xs)} y1={row[0].cy} y2={row[0].cy} />;
          })}
        {placed.map((m) => {
          const key = `${m.si}-${m.ci}`;
          const dimmed = dim !== null && dim !== m.si;
          const style = styles[m.si];
          const hot = hotKey === key;
          const hasBound = m.lo !== undefined || m.hi !== undefined;
          const open = m.hi === undefined ? "end" : m.lo === undefined ? "start" : null;
          if (kind === "delta") {
            const color = m.value >= 0 ? DIV_POS : DIV_NEG;
            const x = Math.min(m.cx, zero);
            const w = Math.abs(m.cx - zero);
            return (
              <g key={key}>
                <Bar x={x} y={m.cy - m.thick / 2} w={w} h={m.thick} end={m.value >= 0 ? "right" : "left"} color={color} hot={hot} dim={dimmed} />
                {hasBound && <Whisker x1={vScale(m.lo ?? m.value)} x2={vScale(m.hi ?? m.value)} y1={m.cy} y2={m.cy} open={open} ring strong dim={dimmed} />}
              </g>
            );
          }
          return (
            <g key={key}>
              {hasBound && <Whisker x1={vScale(m.lo ?? m.value)} x2={vScale(m.hi ?? m.value)} y1={m.cy} y2={m.cy} open={open} dim={dimmed} />}
              <Marker x={m.cx} y={m.cy} color={style.color} r={DOT_R} hot={hot} hollow={style.hollow} diamond={style.diamond} dim={dimmed} />
            </g>
          );
        })}
        {/* extremes labels on dumbbell rows */}
        {kind === "dot" &&
          spec.valueLabels === "extremes" &&
          cats.map((cat, ci) => {
            const row = placed.filter((m) => m.ci === ci);
            if (row.length === 0) return null;
            const min = row.reduce((a, b) => (b.value < a.value ? b : a));
            const max = row.reduce((a, b) => (b.value > a.value ? b : a));
            return (
              <g key={`ext-${ci}`}>
                <text className="rchart-value-label" x={min.cx - DOT_R - 6} y={min.cy} dy="0.35em" textAnchor="end">
                  {describePoint(spec, pointIn(spec, min.si, cat)!, false).value}
                </text>
                {max !== min && (
                  <text className="rchart-value-label" x={max.cx + DOT_R + 6} y={max.cy} dy="0.35em">
                    {describePoint(spec, pointIn(spec, max.si, cat)!, false).value}
                  </text>
                )}
              </g>
            );
          })}
      </svg>
    </ChartFrame>
  );
}
