/**
 * Category labels for horizontal (row) kinds: right-aligned at `x`, wrapped
 * into tspans, vertically centred on each row, with a hairline row rule
 * under every row.
 */
import { TICK_SIZE } from "../tokens";

export interface RowLabel {
  /** Row centre. */
  y: number;
  /** Row bottom edge, for the rule. */
  bottom: number;
  lines: string[];
}

export function RowLabels({ rows, x, plotRight, rules = true }: { rows: RowLabel[]; x: number; plotRight: number; rules?: boolean }) {
  const lineHeight = TICK_SIZE + 3;
  return (
    <g className="rchart-row-labels" aria-hidden="true">
      {rows.map((row, index) => {
        const firstY = row.y - ((row.lines.length - 1) * lineHeight) / 2;
        return (
          <g key={index}>
            {rules && <line className="rchart-row-rule" x1={x} x2={plotRight} y1={row.bottom} y2={row.bottom} />}
            <text className="rchart-cat" x={x - 8} y={firstY} dy="0.35em" textAnchor="end">
              {row.lines.map((line, li) => (
                <tspan key={li} x={x - 8} dy={li === 0 ? 0 : lineHeight}>
                  {line}
                </tspan>
              ))}
            </text>
          </g>
        );
      })}
    </g>
  );
}
