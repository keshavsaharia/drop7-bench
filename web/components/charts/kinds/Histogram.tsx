"use client";
/**
 * `histogram`: pre-binned counts or shares from the record. Bins are
 * contiguous ordinal slots in the record's order, touching with a 2 px
 * surface gap; several series draw grouped bars inside each bin. The chart
 * never bins: a spec with raw values is refused by the validator.
 */
import { useMemo, useRef, useState } from "react";
import { ariaText, describeBin } from "@/lib/charts/describe";
import { groupLayout } from "@/lib/charts/geometry";
import { AxisTitle } from "../axes/AxisTitle";
import { BottomCategories } from "../axes/BottomAxis";
import { LeftAxis } from "../axes/LeftAxis";
import { ChartFrame } from "../frame/ChartFrame";
import type { LegendItem } from "../frame/Legend";
import { useChartCursor, type CursorTarget } from "../hover/useChartCursor";
import { useChartLayout } from "../layout";
import { Bar } from "../marks/Bar";
import { linearScale, scaleTicks } from "../scales";
import { BAR_MAX, LABEL_SIZE, SERIES, TICK_SIZE } from "../tokens";
import { axisText, tickFormatter, verticalBox, wrapLabel, type KindProps } from "./shared";

export function Histogram({ spec, id, title, height, compact, table, yDomain, noLegend }: KindProps) {
  const { ref, width, measure } = useChartLayout();
  const [dim, setDim] = useState<number | null>(null);
  // One array identity per spec, so the memos below do not re-run each render.
  const histograms = useMemo(() => spec.bins ?? [], [spec.bins]);

  const layout = useMemo(() => {
    const labels: string[] = [];
    for (const h of histograms) for (const b of h.bins) if (!labels.includes(b.label)) labels.push(b.label);
    const values = histograms.flatMap((h) => h.bins.map((b) => (b.count ?? b.share) as number));
    const vDom: [number, number] = yDomain ?? spec.y?.domain ?? [0, Math.max(0, ...values) || 1];
    const vScale0 = linearScale(vDom, [1, 0], yDomain || spec.y?.domain ? 0 : 5);
    const vTicks = scaleTicks(vScale0, compact ? 4 : 5);
    const format = tickFormatter(spec.y);
    const provisionalSlot = (width - 80) / Math.max(labels.length, 1);
    const slotLabels = labels.map((l) => wrapLabel(l, provisionalSlot - 6, measure, 3));
    const labelLines = Math.max(1, ...slotLabels.map((l) => l.lines.length));
    const box = verticalBox({ width, measure, tickLabels: vTicks.map(format), valueTitle: axisText(spec.y), xTitle: axisText(spec.x), xLabelHeight: labelLines * (TICK_SIZE + 3) + 6, height, compact });
    const vScale = linearScale(vScale0.domain() as [number, number], [box.bottom, box.top]);
    const slot = (box.right - box.left) / Math.max(labels.length, 1);
    const group = groupLayout(histograms.length, slot - 2, BAR_MAX, 2, 1);
    return { labels, slotLabels, box, vScale, vTicks, format, slot, group };
  }, [histograms, spec, width, measure, height, compact, yDomain]);

  const { box, vScale, labels, slot, group } = layout;
  const zero = vScale(Math.max(0, vScale.domain()[0] as number));

  const placed = useMemo(() => {
    const out: { hi: number; bi: number; x: number; y: number; w: number; h: number; cx: number }[] = [];
    histograms.forEach((h, hIndex) => {
      h.bins.forEach((b) => {
        const bi = labels.indexOf(b.label);
        const centre = box.left + slot * (bi + 0.5);
        const x = centre + group.offsets[hIndex];
        const vy = vScale((b.count ?? b.share) as number);
        out.push({ hi: hIndex, bi, x, y: Math.min(vy, zero), w: group.thick, h: Math.abs(zero - vy), cx: x + group.thick / 2 });
      });
    });
    return out;
  }, [histograms, labels, box.left, slot, group, vScale, zero]);

  const targets = useMemo<CursorTarget[]>(
    () =>
      placed.map((bar) => {
        const h = histograms[bar.hi];
        const bin = h.bins.find((b) => labels.indexOf(b.label) === bar.bi)!;
        const text = describeBin(spec, bin);
        const named = histograms.length > 1;
        return {
          key: `${bar.hi}-${bar.bi}`,
          x: bar.cx,
          y: bar.y,
          rect: { x: bar.x - 2, y: bar.y - 3, w: bar.w + 4, h: bar.h + 6 },
          row: bar.bi,
          col: bar.hi,
          content: { head: bin.label, rows: [{ value: text.value, label: named ? h.series : undefined, key: { color: SERIES(bar.hi), shape: "rect" }, hot: true }], details: text.details, source: text.source },
          aria: ariaText(named ? h.series : null, text),
        };
      }),
    [placed, histograms, labels, spec],
  );

  const svgRef = useRef<SVGSVGElement | null>(null);
  const cursor = useChartCursor({ targets, svgRef, hit: "rect", grid: { rows: labels.length, cols: histograms.length, primary: "col" }, anchor: "mark", viewBox: { width: box.width, height: box.height } });
  const hotKey = cursor.target?.key ?? null;
  const legend: LegendItem[] = histograms.map((h, i) => ({ name: h.series, key: { color: SERIES(i), shape: "rect" } }));
  const titleId = id ? `${id}-title` : undefined;

  return (
    <ChartFrame id={id} title={title} frameRef={ref} legend={noLegend ? undefined : legend} legendActive={dim} onLegendHover={setDim} evidence={spec.evidence} tooltip={cursor.tooltip} live={cursor.live} table={table} compact={compact}>
      <svg
        ref={svgRef}
        className="rchart-svg"
        width={box.width}
        height={box.height}
        viewBox={`0 0 ${box.width} ${box.height}`}
        role="group"
        aria-roledescription="histogram"
        aria-labelledby={titleId}
        aria-label={titleId ? undefined : spec.title}
        tabIndex={0}
        {...cursor.handlers}
      >
        <AxisTitle x={box.left} y={LABEL_SIZE} lines={box.valueTitle} />
        <LeftAxis scale={vScale} ticks={layout.vTicks} x={box.left} width={box.right - box.left} plotTop={box.top} plotBottom={box.bottom} format={layout.format} />
        <BottomCategories items={labels.map((label, bi) => ({ x: box.left + slot * (bi + 0.5), lines: layout.slotLabels[bi]?.lines ?? [label] }))} y={box.bottom} plotLeft={box.left} plotRight={box.right} />
        <AxisTitle x={(box.left + box.right) / 2} y={box.xTitleY} lines={box.xTitle} anchor="middle" />
        {placed.map((bar) => {
          const key = `${bar.hi}-${bar.bi}`;
          return <Bar key={key} x={bar.x} y={bar.y} w={bar.w} h={bar.h} end="top" color={SERIES(bar.hi)} hot={hotKey === key} dim={dim !== null && dim !== bar.hi} />;
        })}
      </svg>
    </ChartFrame>
  );
}
