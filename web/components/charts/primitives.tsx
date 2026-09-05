"use client";
/**
 * Building blocks shared by every chart in the kit: numeric axes with grid
 * lines, whiskers, markers, and the pointer/keyboard hover model that feeds
 * the portal tooltip. Charts place their points in pixel space and hand the
 * list to `usePointHover`; nothing here knows about research records.
 */
import { useCallback, useState, type FocusEvent, type MouseEvent, type ReactNode } from "react";
import { localPoint } from "@visx/event";
import type { ScaleLinear, ScaleLogarithmic } from "d3-scale";
import { formatCompact } from "@/lib/charts/spec";
import { AXIS, FAINT, GRID, INK, MUTED, TICK_SIZE, LABEL_SIZE, ZERO } from "./theme";
import type { TooltipLine, TooltipState } from "./Tooltip";

export type NumericScale = ScaleLinear<number, number> | ScaleLogarithmic<number, number>;

export interface PlacedPoint {
  key: string;
  /** Marker centre in SVG pixels. */
  cx: number;
  cy: number;
  color: string;
  hollow?: boolean;
  diamond?: boolean;
  /** For bars: the rectangle, so hovering anywhere on the bar hits the point. */
  bar?: { x: number; y: number; w: number; h: number };
  lines: TooltipLine[];
}

const HIT_RADIUS = 26;

/**
 * Wraps a d3 scale so every coordinate it emits is rounded to 1/100 px.
 * Math.log and friends can differ in the last bit between the Node build
 * that renders the server HTML and the browser that hydrates it; without
 * rounding, a log axis produces a hydration mismatch on those attributes.
 */
export function rounded<S extends NumericScale>(scale: S): S {
  const wrapped = ((value: number) => Math.round(scale(value) * 100) / 100) as unknown as S & Record<string, unknown>;
  const source = scale as unknown as Record<string, unknown>;
  for (const key of ["domain", "range", "ticks", "nice", "base", "invert", "copy"]) {
    const member = source[key];
    if (typeof member === "function") wrapped[key] = (member as (...args: unknown[]) => unknown).bind(scale);
  }
  return wrapped as S;
}

/** Pointer-nearest and keyboard-focus hover over a list of placed points. */
export function usePointHover(points: PlacedPoint[]) {
  const [active, setActive] = useState<string | null>(null);
  const [tooltip, setTooltip] = useState<TooltipState | null>(null);

  const onMouseMove = useCallback(
    (event: MouseEvent<SVGSVGElement>) => {
      const local = localPoint(event.currentTarget, event);
      if (!local) return;
      let best: PlacedPoint | null = null;
      let bestDistance = Infinity;
      for (const point of points) {
        if (point.bar) {
          const { x, y, w, h } = point.bar;
          if (local.x >= x && local.x <= x + w && local.y >= y && local.y <= y + h) {
            best = point;
            bestDistance = 0;
            break;
          }
        }
        const dx = point.cx - local.x;
        const dy = point.cy - local.y;
        const distance = Math.hypot(dx, dy);
        if (distance < bestDistance) {
          best = point;
          bestDistance = distance;
        }
      }
      if (best && bestDistance <= HIT_RADIUS) {
        setActive(best.key);
        setTooltip({ x: event.clientX, y: event.clientY, lines: best.lines });
      } else {
        setActive(null);
        setTooltip(null);
      }
    },
    [points],
  );

  const onMouseLeave = useCallback(() => {
    setActive(null);
    setTooltip(null);
  }, []);

  const onFocusPoint = useCallback((point: PlacedPoint, event: FocusEvent<SVGGElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    setActive(point.key);
    setTooltip({ x: rect.left + rect.width / 2, y: rect.top + rect.height / 2, lines: point.lines });
  }, []);

  const onBlurPoint = useCallback(() => {
    setActive(null);
    setTooltip(null);
  }, []);

  return { active, tooltip, onMouseMove, onMouseLeave, onFocusPoint, onBlurPoint };
}

/** Tick values for a numeric scale: integers when the data are small integers, decades on a log axis. */
export function tickValues(scale: NumericScale, count: number, integer = false): number[] {
  const [lo, hi] = scale.domain() as [number, number];
  if (isLog(scale)) {
    const decades = Math.log10(hi) - Math.log10(lo);
    const raw = scale.ticks(count);
    if (decades >= 3) return raw.filter((v) => Number.isInteger(Math.log10(v)));
    return raw.filter((v) => {
      const mantissa = v / 10 ** Math.floor(Math.log10(v));
      return decades >= 1.5 ? [1, 2, 5].includes(Math.round(mantissa)) : true;
    });
  }
  if (integer && hi - lo <= 14) {
    const step = hi - lo > count ? Math.ceil((hi - lo) / count) : 1;
    const values: number[] = [];
    for (let v = Math.ceil(lo); v <= hi; v += step) values.push(v);
    return values;
  }
  return scale.ticks(count);
}

function isLog(scale: NumericScale): scale is ScaleLogarithmic<number, number> {
  return typeof (scale as ScaleLogarithmic<number, number>).base === "function";
}

/** Horizontal grid + left tick labels for a vertical value axis. */
export function LeftAxis({
  scale,
  ticks,
  x,
  width,
  label,
  plotTop,
  plotBottom,
}: {
  scale: NumericScale;
  ticks: number[];
  x: number;
  width: number;
  label?: string;
  plotTop: number;
  plotBottom: number;
}) {
  return (
    <g>
      {ticks.map((value) => {
        const y = scale(value);
        return (
          <g key={value}>
            <line x1={x} x2={x + width} y1={y} y2={y} stroke={GRID} strokeWidth={1} />
            <text x={x - 8} y={y} dy="0.35em" textAnchor="end" fontSize={TICK_SIZE} fill={MUTED}>
              {formatCompact(value)}
            </text>
          </g>
        );
      })}
      <line x1={x} x2={x} y1={plotTop} y2={plotBottom} stroke={AXIS} />
      {label && (
        <text
          transform={`translate(12 ${(plotTop + plotBottom) / 2}) rotate(-90)`}
          textAnchor="middle"
          fontSize={LABEL_SIZE}
          fill={MUTED}
        >
          {label}
        </text>
      )}
    </g>
  );
}

/** Vertical grid + bottom tick labels for a horizontal value axis. */
export function BottomAxis({
  scale,
  ticks,
  y,
  plotTop,
  plotLeft,
  plotRight,
  label,
  labelY,
  grid = true,
}: {
  scale: NumericScale;
  ticks: number[];
  y: number;
  plotTop: number;
  plotLeft: number;
  plotRight: number;
  label?: string;
  labelY: number;
  grid?: boolean;
}) {
  return (
    <g>
      {ticks.map((value) => {
        const x = scale(value);
        return (
          <g key={value}>
            {grid && <line x1={x} x2={x} y1={plotTop} y2={y} stroke={GRID} strokeWidth={1} />}
            <line x1={x} x2={x} y1={y} y2={y + 4} stroke={AXIS} />
            <text x={x} y={y + 8} dy="0.9em" textAnchor="middle" fontSize={TICK_SIZE} fill={MUTED}>
              {formatCompact(value)}
            </text>
          </g>
        );
      })}
      <line x1={plotLeft} x2={plotRight} y1={y} y2={y} stroke={AXIS} />
      {label && (
        <text x={(plotLeft + plotRight) / 2} y={labelY} textAnchor="middle" fontSize={LABEL_SIZE} fill={MUTED}>
          {label}
        </text>
      )}
    </g>
  );
}

export function ZeroLine(props: { x1: number; x2: number; y1: number; y2: number }) {
  return <line {...props} stroke={ZERO} strokeWidth={1} strokeDasharray="3 3" />;
}

export function Whisker({
  x1,
  y1,
  x2,
  y2,
  color,
  cap = 4,
}: {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  color: string;
  cap?: number;
}) {
  const vertical = x1 === x2;
  return (
    <g stroke={color} strokeWidth={1.5} opacity={0.85}>
      <line x1={x1} y1={y1} x2={x2} y2={y2} />
      {vertical ? (
        <>
          <line x1={x1 - cap} x2={x1 + cap} y1={y1} y2={y1} />
          <line x1={x1 - cap} x2={x1 + cap} y1={y2} y2={y2} />
        </>
      ) : (
        <>
          <line x1={x1} x2={x1} y1={y1 - cap} y2={y1 + cap} />
          <line x1={x2} x2={x2} y1={y2 - cap} y2={y2 + cap} />
        </>
      )}
    </g>
  );
}

export function Marker({
  point,
  r = 4.5,
  active,
}: {
  point: PlacedPoint;
  r?: number;
  active: boolean;
}) {
  const { cx, cy, color, hollow, diamond } = point;
  const fill = hollow ? "#101014" : color;
  const stroke = active ? INK : color;
  if (diamond) {
    const d = r + 1.5;
    return (
      <polygon
        points={`${cx},${cy - d} ${cx + d},${cy} ${cx},${cy + d} ${cx - d},${cy}`}
        fill={fill}
        stroke={stroke}
        strokeWidth={active ? 2 : 1.5}
      />
    );
  }
  return <circle cx={cx} cy={cy} r={active ? r + 1.5 : r} fill={fill} stroke={stroke} strokeWidth={active ? 2 : 1.5} />;
}

/** Wraps a marker in a focusable group so keyboard users get the tooltip too. */
export function FocusablePoint({
  point,
  onFocus,
  onBlur,
  children,
}: {
  point: PlacedPoint;
  onFocus: (point: PlacedPoint, event: FocusEvent<SVGGElement>) => void;
  onBlur: () => void;
  children: ReactNode;
}) {
  return (
    <g
      tabIndex={0}
      role="img"
      aria-label={point.lines.map((line) => line.text).join(". ")}
      onFocus={(event) => onFocus(point, event)}
      onBlur={onBlur}
      style={{ outline: "none", cursor: "default" }}
    >
      {children}
    </g>
  );
}

export function EmptyNote({ children }: { children: ReactNode }) {
  return (
    <p className="rchart-empty" style={{ color: FAINT }}>
      {children}
    </p>
  );
}
