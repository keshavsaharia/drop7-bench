/**
 * An axis title, always horizontal: the value-axis title sits above the
 * plot at its top-left (never rotated, which returned ~40 px of plot width
 * on narrow screens); the category/x title is centred below the ticks.
 */
import { LABEL_SIZE } from "../tokens";

export function AxisTitle({ x, y, lines, anchor = "start" }: { x: number; y: number; lines: string[]; anchor?: "start" | "middle" | "end" }) {
  if (lines.length === 0) return null;
  return (
    <text className="rchart-axis-title" x={x} y={y} textAnchor={anchor} aria-hidden="true">
      {lines.map((line, index) => (
        <tspan key={index} x={x} dy={index === 0 ? 0 : LABEL_SIZE + 3}>
          {line}
        </tspan>
      ))}
    </text>
  );
}
