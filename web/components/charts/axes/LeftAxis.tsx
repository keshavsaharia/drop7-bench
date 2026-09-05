/**
 * A vertical value axis on the left: horizontal hairline gridlines across
 * the plot, right-aligned mono tick labels, and the axis rule. Gridlines are
 * solid and one step off the surface (charts.css .rchart-grid).
 */
import type { ValueScale } from "../scales";

export function LeftAxis({
  scale,
  ticks,
  x,
  width,
  plotTop,
  plotBottom,
  format,
  grid = true,
}: {
  scale: ValueScale;
  ticks: number[];
  x: number;
  width: number;
  plotTop: number;
  plotBottom: number;
  format: (value: number) => string;
  grid?: boolean;
}) {
  return (
    <g className="rchart-left-axis" aria-hidden="true">
      {ticks.map((value) => {
        const y = scale(value);
        return (
          <g key={value}>
            {grid && <line className="rchart-grid" x1={x} x2={x + width} y1={y} y2={y} />}
            <text className="rchart-tick" x={x - 8} y={y} dy="0.35em" textAnchor="end">
              {format(value)}
            </text>
          </g>
        );
      })}
      <line className="rchart-axis" x1={x} x2={x} y1={plotTop} y2={plotBottom} />
    </g>
  );
}
