"use client";
/**
 * Renders a research-figure spec (web/lib/charts/spec.ts) with visx.
 *
 * Why this replaced the string-templated SVG generator: that generator laid
 * text out from a 0.6 em-per-character guess inside a fixed 720x400 box, so
 * legends collided with axis labels, crowded category labels piled on top of
 * each other, and popovers were clamped inside the SVG. Here the title and
 * legend are HTML (they wrap), tick and category labels are measured with a
 * canvas after hydration, crowded categorical charts turn into horizontal
 * rows with a measured label gutter, and the tooltip lives in a portal.
 *
 * The component computes pixel positions and axis ranges only. Every number
 * it prints is a value from the spec, formatted.
 */
import { useMemo } from "react";
import { scaleLinear, scaleLog } from "@visx/scale";
import { LinePath } from "@visx/shape";
import { curveLinear } from "@visx/curve";
import { formatSigned, formatValue, type FigurePoint, type FigureSeries, type FigureSpec } from "@/lib/charts/spec";
import { useContainerWidth, useMeasurer, useMounted, wrapText, type Measurer } from "./layout";
import {
  BottomAxis,
  FocusablePoint,
  LeftAxis,
  Marker,
  Whisker,
  ZeroLine,
  rounded,
  tickValues,
  usePointHover,
  type NumericScale,
  type PlacedPoint,
} from "./primitives";
import { ChartTooltip, type TooltipLine } from "./Tooltip";
import { DEFAULT_WIDTH, LABEL_SIZE, MUTED, TICK_SIZE, seriesColor } from "./theme";

const TOP = 14;
const RIGHT = 16;
const MAX_VERTICAL_CATEGORIES = 6;

interface Placement {
  width: number;
  height: number;
  horizontal: boolean;
  points: PlacedPoint[];
  render: () => React.ReactNode;
}

function axisLabel(axis?: { label: string; unit?: string }): string | undefined {
  if (!axis) return undefined;
  return axis.unit ? `${axis.label} (${axis.unit})` : axis.label;
}

function tooltipLines(spec: FigureSpec, series: FigureSeries, point: FigurePoint, color: string): TooltipLine[] {
  const unit = spec.y?.unit;
  const lines: TooltipLine[] = [{ text: series.name, strong: true, swatch: color }];
  lines.push({ text: spec.kind === "line" ? `${spec.x?.label ?? "x"}: ${formatValue(point.x as number, spec.x?.unit)}` : String(point.x) });
  if (point.label) lines.push({ text: point.label });
  lines.push({ text: `${spec.y?.label ?? "value"}: ${spec.kind === "bar" ? formatSigned(point.y, unit) : formatValue(point.y, unit)}` });
  if (point.lo !== undefined && point.hi !== undefined) lines.push({ text: `bounds: ${formatValue(point.lo, unit)} to ${formatValue(point.hi, unit)}` });
  else if (point.lo !== undefined) lines.push({ text: `95% lower bound: ${formatValue(point.lo, unit)}` });
  else if (point.hi !== undefined) lines.push({ text: `95% upper bound: ${formatValue(point.hi, unit)}` });
  if (point.n !== undefined) lines.push({ text: `n = ${formatValue(point.n)} games` });
  lines.push({ text: `source: ${point.sourceRecord}${point.sourceField ? ` · ${point.sourceField}` : ""}`, muted: true });
  return lines;
}

function categories(spec: FigureSpec): string[] {
  const out: string[] = [];
  for (const series of spec.series) for (const point of series.points) if (!out.includes(String(point.x))) out.push(String(point.x));
  return out;
}

function valueDomain(spec: FigureSpec, includeZero: boolean): [number, number] {
  const values: number[] = [];
  for (const series of spec.series) {
    for (const point of series.points) {
      values.push(point.y);
      if (point.lo !== undefined) values.push(point.lo);
      if (point.hi !== undefined) values.push(point.hi);
    }
  }
  if (includeZero) values.push(0);
  let lo = Math.min(...values);
  let hi = Math.max(...values);
  if (lo === hi) {
    lo -= Math.abs(lo) * 0.1 || 1;
    hi += Math.abs(hi) * 0.1 || 1;
  }
  return [lo, hi];
}

function tickWidth(scale: NumericScale, ticks: number[], measure: Measurer): number {
  return Math.max(0, ...ticks.map((t) => measure(formatCompactSafe(t), TICK_SIZE)));
}

function formatCompactSafe(value: number): string {
  // Mirror of formatCompact for width measurement; imported lazily to keep this file's imports tidy.
  const abs = Math.abs(value);
  if (abs >= 1e6) return `${(value / 1e6).toFixed(1)}M`;
  if (abs >= 1e3) return `${(value / 1e3).toFixed(1)}k`;
  return value.toFixed(abs >= 1 ? 2 : 3);
}

/** Series that actually have a point in a category share its slot. */
function present(spec: FigureSpec, category: string): FigureSeries[] {
  return spec.series.filter((series) => series.points.some((point) => String(point.x) === category));
}

/* ------------------------------------------------------------------ line */

function placeLine(spec: FigureSpec, width: number, measure: Measurer, active: string | null): Placement {
  const xs = spec.series.flatMap((s) => s.points.map((p) => p.x as number));
  const allInteger = xs.every((x) => Number.isInteger(x));
  const log = spec.x?.scale === "log" && xs.every((x) => x > 0);
  const [yLo, yHi] = valueDomain(spec, false);
  const innerHeight = Math.max(180, Math.min(300, Math.round(width * 0.42)));

  const yScale = rounded(scaleLinear<number>({ domain: [yLo, yHi], nice: 5 }));
  const yTicks = tickValues(yScale, 5);
  const leftGutter = tickWidth(yScale, yTicks, measure) + 14 + (spec.y ? 18 : 0);
  const xLabel = axisLabel(spec.x);
  const bottomGutter = TICK_SIZE + 12 + (xLabel ? LABEL_SIZE + 8 : 0);
  const height = TOP + innerHeight + bottomGutter;
  const plotLeft = leftGutter;
  const plotRight = width - RIGHT - 12;
  const plotTop = TOP;
  const plotBottom = TOP + innerHeight;
  yScale.range([plotBottom, plotTop]);

  const xMin = Math.min(...xs);
  const xMax = Math.max(...xs);
  const pad = log ? 0 : (xMax - xMin || 1) * 0.06;
  const xScale: NumericScale = rounded(
    log
      ? scaleLog<number>({ domain: [xMin / 1.3, xMax * 1.3], range: [plotLeft + 8, plotRight - 8] })
      : scaleLinear<number>({ domain: [xMin - pad, xMax + pad], range: [plotLeft + 8, plotRight - 8] }),
  );
  const xTicks = tickValues(xScale, Math.max(2, Math.floor((plotRight - plotLeft) / 90)), allInteger);

  const points: PlacedPoint[] = [];
  const whiskers: React.ReactNode[] = [];
  const paths: React.ReactNode[] = [];
  spec.series.forEach((series, index) => {
    const color = seriesColor(index);
    const sorted = [...series.points].sort((a, b) => (a.x as number) - (b.x as number));
    if (sorted.length > 1) {
      paths.push(
        <LinePath<FigurePoint>
          key={`path-${index}`}
          data={sorted}
          x={(p) => xScale(p.x as number)}
          y={(p) => yScale(p.y)}
          stroke={color}
          strokeWidth={2}
          strokeDasharray={series.dashed ? "6 4" : undefined}
          curve={curveLinear}
        />,
      );
    }
    sorted.forEach((point, pi) => {
      const cx = xScale(point.x as number);
      const cy = yScale(point.y);
      if (point.lo !== undefined || point.hi !== undefined) {
        whiskers.push(
          <Whisker key={`w-${index}-${pi}`} x1={cx} x2={cx} y1={yScale(point.lo ?? point.y)} y2={yScale(point.hi ?? point.y)} color={color} />,
        );
      }
      points.push({ key: `${index}-${pi}`, cx, cy, color, hollow: series.dashed, lines: tooltipLines(spec, series, point, color) });
    });
  });

  return {
    width,
    height,
    horizontal: false,
    points,
    render: () => (
      <>
        <LeftAxis scale={yScale} ticks={yTicks} x={plotLeft} width={plotRight - plotLeft} label={axisLabel(spec.y)} plotTop={plotTop} plotBottom={plotBottom} />
        <BottomAxis scale={xScale} ticks={xTicks} y={plotBottom} plotTop={plotTop} plotLeft={plotLeft} plotRight={plotRight} label={xLabel} labelY={height - 4} grid={false} />
        {paths}
        {whiskers}
        {points.map((point) => (
          <Marker key={point.key} point={point} active={active === point.key} />
        ))}
      </>
    ),
  };
}

/* ---------------------------------------------------------- categorical */

interface LabelLayout {
  lines: string[];
  fits: boolean;
}

function placeCategorical(spec: FigureSpec, width: number, measure: Measurer, active: string | null): Placement {
  const isBar = spec.kind === "bar";
  const cats = categories(spec);
  const [vLo, vHi] = valueDomain(spec, isBar);
  const valueLabel = axisLabel(spec.y);

  // Decide orientation: few categories whose labels wrap into their slot stay
  // vertical; anything crowded becomes rows with a measured label gutter.
  let horizontal = spec.kind === "forest" || cats.length > MAX_VERTICAL_CATEGORIES;
  let slotLabels: LabelLayout[] = [];
  if (!horizontal) {
    const provisionalLeft = 60 + (valueLabel ? 18 : 0);
    const slot = (width - RIGHT - provisionalLeft) / cats.length;
    slotLabels = cats.map((c) => wrapText(c, slot - 8, TICK_SIZE, measure, 3));
    if (slotLabels.some((l) => !l.fits)) horizontal = true;
  }

  if (horizontal) return placeRows(spec, cats, width, measure, active, vLo, vHi);

  const vScale = rounded(scaleLinear<number>({ domain: [vLo, vHi], nice: 5 }));
  const vTicks = tickValues(vScale, 5);
  const leftGutter = tickWidth(vScale, vTicks, measure) + 14 + (valueLabel ? 18 : 0);
  const labelLines = Math.max(...slotLabels.map((l) => l.lines.length));
  const xLabel = axisLabel(spec.x);
  const hasPointLabels = spec.kind === "dot" && spec.series.some((s) => s.points.some((p) => p.label));
  const bottomGutter = labelLines * (TICK_SIZE + 2) + 12 + (xLabel ? LABEL_SIZE + 8 : 0);
  const innerHeight = Math.max(180, Math.min(300, Math.round(width * 0.42)));
  const plotTop = TOP + (hasPointLabels ? 12 : 0);
  const plotBottom = plotTop + innerHeight;
  const height = plotBottom + bottomGutter;
  const plotLeft = leftGutter;
  const plotRight = width - RIGHT;
  vScale.range([plotBottom, plotTop]);
  const slot = (plotRight - plotLeft) / cats.length;

  const points: PlacedPoint[] = [];
  const shapes: React.ReactNode[] = [];
  const zeroY = vScale(0);
  spec.series.forEach((series, si) => {
    const color = seriesColor(si);
    series.points.forEach((point, pi) => {
      const category = String(point.x);
      const ci = cats.indexOf(category);
      const sharing = present(spec, category);
      const lane = sharing.indexOf(series);
      const laneWidth = slot / (sharing.length + 1);
      const cx = plotLeft + slot * ci + laneWidth * (lane + 1);
      const cy = vScale(point.y);
      const key = `${si}-${pi}`;
      const placed: PlacedPoint = { key, cx, cy, color, hollow: series.dashed, lines: tooltipLines(spec, series, point, color) };
      if (isBar) {
        const barWidth = Math.min(40, laneWidth * 0.8);
        const top = Math.min(cy, zeroY);
        const h = Math.abs(zeroY - cy);
        placed.bar = { x: cx - barWidth / 2, y: top, w: barWidth, h };
        shapes.push(
          <rect key={`bar-${key}`} x={cx - barWidth / 2} y={top} width={barWidth} height={h} fill={color} opacity={active === key ? 0.95 : 0.75} rx={2} />,
        );
      }
      if (point.lo !== undefined || point.hi !== undefined) {
        shapes.push(<Whisker key={`w-${key}`} x1={cx} x2={cx} y1={vScale(point.lo ?? point.y)} y2={vScale(point.hi ?? point.y)} color={color} />);
      }
      if (spec.kind === "dot" && point.label) {
        shapes.push(
          <text key={`label-${key}`} x={cx} y={Math.min(cy, vScale(point.hi ?? point.y)) - 10} textAnchor="middle" fontSize={10} fill={MUTED}>
            {point.label}
          </text>,
        );
      }
      points.push(placed);
    });
  });

  return {
    width,
    height,
    horizontal: false,
    points,
    render: () => (
      <>
        <LeftAxis scale={vScale} ticks={vTicks} x={plotLeft} width={plotRight - plotLeft} label={valueLabel} plotTop={plotTop} plotBottom={plotBottom} />
        {isBar && vLo < 0 && <ZeroLine x1={plotLeft} x2={plotRight} y1={zeroY} y2={zeroY} />}
        <line x1={plotLeft} x2={plotRight} y1={plotBottom} y2={plotBottom} stroke="#3f3f46" />
        {cats.map((category, ci) => {
          const cx = plotLeft + slot * (ci + 0.5);
          return (
            <text key={category} x={cx} y={plotBottom + 8} textAnchor="middle" fontSize={TICK_SIZE} fill={MUTED}>
              {slotLabels[ci].lines.map((line, li) => (
                <tspan key={li} x={cx} dy={li === 0 ? "0.9em" : "1.15em"}>
                  {line}
                </tspan>
              ))}
            </text>
          );
        })}
        {xLabel && (
          <text x={(plotLeft + plotRight) / 2} y={height - 4} textAnchor="middle" fontSize={LABEL_SIZE} fill={MUTED}>
            {xLabel}
          </text>
        )}
        {shapes}
        {points.map((point) => (
          <Marker key={point.key} point={point} r={isBar ? 3 : 5} active={active === point.key} />
        ))}
      </>
    ),
  };
}

/* ---------------------------------------------------------------- rows */

function placeRows(
  spec: FigureSpec,
  cats: string[],
  width: number,
  measure: Measurer,
  active: string | null,
  vLo: number,
  vHi: number,
): Placement {
  const isBar = spec.kind === "bar";
  const isDot = spec.kind === "dot";
  const maxLabelWidth = Math.min(Math.round(width * 0.38), 240);
  const labels = cats.map((c) => wrapText(c, maxLabelWidth, TICK_SIZE, measure, 3));
  const gutter = Math.max(...labels.flatMap((l) => l.lines.map((line) => measure(line, TICK_SIZE)))) + 16;
  const plotLeft = Math.min(gutter, width * 0.5);
  const plotRight = width - RIGHT - 24;
  const valueLabel = axisLabel(spec.y);

  const rowHeights = cats.map((category, ci) => {
    const lanes = present(spec, category).length;
    const byLanes = 10 + lanes * (isBar ? 16 : 12);
    const byLabel = labels[ci].lines.length * (TICK_SIZE + 3) + 8;
    return Math.max(28, byLanes, byLabel);
  });
  const plotTop = TOP;
  const plotBottom = plotTop + rowHeights.reduce((a, b) => a + b, 0);
  const bottomGutter = TICK_SIZE + 12 + (valueLabel ? LABEL_SIZE + 8 : 0);
  const height = plotBottom + bottomGutter;

  const vScale = rounded(scaleLinear<number>({ domain: [vLo, vHi], nice: 5, range: [plotLeft, plotRight] }));
  const vTicks = tickValues(vScale, Math.max(3, Math.floor((plotRight - plotLeft) / 80)));
  const zeroX = vScale(0);
  const rowTop: number[] = [];
  let acc = plotTop;
  for (const h of rowHeights) {
    rowTop.push(acc);
    acc += h;
  }

  const points: PlacedPoint[] = [];
  const shapes: React.ReactNode[] = [];
  spec.series.forEach((series, si) => {
    const color = seriesColor(si);
    series.points.forEach((point, pi) => {
      const category = String(point.x);
      const ci = cats.indexOf(category);
      const sharing = present(spec, category);
      const lane = sharing.indexOf(series);
      const laneHeight = rowHeights[ci] / (sharing.length + 1);
      const cy = rowTop[ci] + laneHeight * (lane + 1);
      const cx = vScale(point.y);
      const key = `${si}-${pi}`;
      const placed: PlacedPoint = { key, cx, cy, color, hollow: series.dashed, diamond: spec.kind === "forest" && series.dashed, lines: tooltipLines(spec, series, point, color) };
      if (isBar) {
        const barHeight = Math.min(18, laneHeight * 0.8);
        const left = Math.min(cx, zeroX);
        const w = Math.abs(zeroX - cx);
        placed.bar = { x: left, y: cy - barHeight / 2, w, h: barHeight };
        shapes.push(<rect key={`bar-${key}`} x={left} y={cy - barHeight / 2} width={w} height={barHeight} fill={color} opacity={active === key ? 0.95 : 0.75} rx={2} />);
      }
      if (point.lo !== undefined || point.hi !== undefined) {
        shapes.push(<Whisker key={`w-${key}`} x1={vScale(point.lo ?? point.y)} x2={vScale(point.hi ?? point.y)} y1={cy} y2={cy} color={color} />);
      }
      if (isDot && point.label) {
        const anchorX = vScale(point.hi ?? point.y) + 9;
        shapes.push(
          <text key={`label-${key}`} x={anchorX} y={cy} dy="0.35em" fontSize={10} fill={MUTED}>
            {point.label}
          </text>,
        );
      }
      points.push(placed);
    });
  });

  return {
    width,
    height,
    horizontal: true,
    points,
    render: () => (
      <>
        {cats.map((category, ci) => {
          const y = rowTop[ci];
          const h = rowHeights[ci];
          const lines = labels[ci].lines;
          const firstY = y + h / 2 - ((lines.length - 1) * (TICK_SIZE + 3)) / 2;
          return (
            <g key={category}>
              <line x1={plotLeft} x2={plotRight} y1={y + h} y2={y + h} stroke="#1f1f23" />
              <text x={plotLeft - 8} y={firstY} dy="0.35em" textAnchor="end" fontSize={TICK_SIZE} fill={MUTED}>
                {lines.map((line, li) => (
                  <tspan key={li} x={plotLeft - 8} dy={li === 0 ? 0 : TICK_SIZE + 3}>
                    {line}
                  </tspan>
                ))}
              </text>
            </g>
          );
        })}
        <BottomAxis scale={vScale} ticks={vTicks} y={plotBottom} plotTop={plotTop} plotLeft={plotLeft} plotRight={plotRight} label={valueLabel} labelY={height - 4} />
        {vLo < 0 && vHi > 0 && <ZeroLine x1={zeroX} x2={zeroX} y1={plotTop} y2={plotBottom} />}
        {shapes}
        {points.map((point) => (
          <Marker key={point.key} point={point} r={isBar ? 3 : 5} active={active === point.key} />
        ))}
      </>
    ),
  };
}

/* ------------------------------------------------------------ component */

export function ResearchChart({ spec, id }: { spec: FigureSpec; id?: string }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  // Placement depends on the hovered key only for highlight opacity; compute
  // geometry once per width/spec and re-render cheaply on hover.
  const base = useMemo(() => (spec.kind === "line" ? placeLine(spec, width, measure, null) : placeCategorical(spec, width, measure, null)), [spec, width, measure]);
  const hover = usePointHover(base.points);
  const placement = useMemo(
    () => (hover.active === null ? base : spec.kind === "line" ? placeLine(spec, width, measure, hover.active) : placeCategorical(spec, width, measure, hover.active)),
    [base, hover.active, spec, width, measure],
  );
  const titleId = id ? `${id}-title` : undefined;

  return (
    <div className="rchart" ref={ref}>
      <h4 className="rchart-title" id={titleId}>
        {spec.title}
      </h4>
      <svg
        width={placement.width}
        height={placement.height}
        viewBox={`0 0 ${placement.width} ${placement.height}`}
        role="img"
        aria-labelledby={titleId}
        className="rchart-svg"
        onMouseMove={hover.onMouseMove}
        onMouseLeave={hover.onMouseLeave}
      >
        {placement.render()}
        {placement.points.map((point) => (
          <FocusablePoint key={`focus-${point.key}`} point={point} onFocus={hover.onFocusPoint} onBlur={hover.onBlurPoint}>
            <circle cx={point.cx} cy={point.cy} r={9} fill="transparent" />
          </FocusablePoint>
        ))}
      </svg>
      <ul className="rchart-legend">
        {spec.series.map((series, index) => (
          <li key={series.name}>
            <span
              className={"rchart-swatch" + (series.dashed ? " is-dashed" : "")}
              style={series.dashed ? { borderColor: seriesColor(index) } : { background: seriesColor(index) }}
            />
            {series.name}
          </li>
        ))}
      </ul>
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}
