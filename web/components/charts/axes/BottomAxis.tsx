/**
 * The bottom axis in two forms: a value axis (ticks from a scale, optional
 * vertical gridlines for horizontal-value kinds) and a category axis (one
 * wrapped label per slot). Both draw the axis rule. Tick labels are mono,
 * category labels sans (charts.css .rchart-tick / .rchart-cat).
 */
import type { ValueScale } from "../scales";
import { TICK_SIZE } from "../tokens";

export function BottomAxis({
  scale,
  ticks,
  y,
  plotTop,
  plotLeft,
  plotRight,
  format,
  grid = false,
}: {
  scale: ValueScale;
  ticks: number[];
  y: number;
  plotTop: number;
  plotLeft: number;
  plotRight: number;
  format: (value: number) => string;
  grid?: boolean;
}) {
  return (
    <g className="rchart-bottom-axis" aria-hidden="true">
      {ticks.map((value) => {
        const x = scale(value);
        return (
          <g key={value}>
            {grid && <line className="rchart-grid" x1={x} x2={x} y1={plotTop} y2={y} />}
            <line className="rchart-axis" x1={x} x2={x} y1={y} y2={y + 4} />
            <text className="rchart-tick" x={x} y={y + 8} dy="0.9em" textAnchor="middle">
              {format(value)}
            </text>
          </g>
        );
      })}
      <line className="rchart-axis" x1={plotLeft} x2={plotRight} y1={y} y2={y} />
    </g>
  );
}

export interface CategoryLabel {
  x: number;
  lines: string[];
}

export function BottomCategories({ items, y, plotLeft, plotRight, rule = true }: { items: CategoryLabel[]; y: number; plotLeft: number; plotRight: number; rule?: boolean }) {
  return (
    <g className="rchart-bottom-axis" aria-hidden="true">
      {rule && <line className="rchart-axis" x1={plotLeft} x2={plotRight} y1={y} y2={y} />}
      {items.map((item, index) => (
        <text key={index} className="rchart-cat" x={item.x} y={y + 8} textAnchor="middle">
          {item.lines.map((line, li) => (
            <tspan key={li} x={item.x} dy={li === 0 ? "0.9em" : `${TICK_SIZE + 3}px`}>
              {line}
            </tspan>
          ))}
        </text>
      ))}
    </g>
  );
}
