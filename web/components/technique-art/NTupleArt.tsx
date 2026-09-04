/**
 * N-tuple network card art. A 4x4 board, three 1x4 windows over it, a
 * five-row lookup table and a sum box. On play each window lights in turn,
 * a line shoots from it to its table row, the row lights, and the sum steps
 * up. At rest all three windows and rows are lit and the total shows.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./n-tuple.css";

const CELL = 26;
const OX = 22;
const OY = 34;
const TABLE_X = 180;
const TABLE_Y = 34;
const ROW_H = 22;

const DISCS: { col: number; row: number; value: number }[] = [
  { col: 0, row: 1, value: 3 },
  { col: 2, row: 1, value: 5 },
  { col: 1, row: 3, value: 2 },
  { col: 3, row: 3, value: 7 },
];

const WINDOWS: { key: string; x: number; y: number; w: number; h: number; colour: string; row: number }[] = [
  { key: "win-1", x: OX, y: OY + CELL, w: 4 * CELL, h: CELL, colour: "var(--color-accent)", row: 1 },
  { key: "win-2", x: OX + 3 * CELL, y: OY, w: CELL, h: 4 * CELL, colour: "var(--color-series-2)", row: 3 },
  { key: "win-3", x: OX, y: OY + 3 * CELL, w: 4 * CELL, h: CELL, colour: "var(--color-series-3)", row: 4 },
];

const GRID = "M48 34v104M74 34v104M100 34v104M22 60h104M22 86h104M22 112h104";
const ROW_LINES = "M180 56h64M180 78h64M180 100h64M180 122h64";
const SUMS = ["0", "0.3", "0.7"];

export function NTupleArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "n-tuple",
        "A board read through three fixed windows, each looking up one row of a table, with the rows summed",
        props,
      )}
    >
      <g className="board">
        <rect
          x={OX}
          y={OY}
          width={4 * CELL}
          height={4 * CELL}
          rx={3}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={GRID} stroke="var(--color-rule)" fill="none" />
        {DISCS.map(({ col, row, value }) => {
          const cx = OX + CELL / 2 + col * CELL;
          const cy = OY + CELL / 2 + row * CELL;
          return (
            <g key={`${col}${row}`}>
              <circle cx={cx} cy={cy} r={9} fill={`var(--color-disc-${value})`} />
              <text
                x={cx}
                y={cy}
                textAnchor="middle"
                dominantBaseline="central"
                fontSize={10}
                fontWeight={700}
                fontFamily={ART_MONO}
                fill={`var(--color-disc-${value}-fg)`}
              >
                {value}
              </text>
            </g>
          );
        })}
      </g>
      <g className="windows" fill="none" strokeWidth={2}>
        {WINDOWS.map((w) => (
          <rect key={w.key} data-anim={w.key} x={w.x} y={w.y} width={w.w} height={w.h} rx={3} stroke={w.colour} />
        ))}
      </g>
      <g className="links" fill="none" strokeWidth={1.5} strokeDasharray={64}>
        <path data-anim="link-1" d="M126 73L180 67" stroke="var(--color-accent)" />
        <path data-anim="link-2" d="M126 86L180 111" stroke="var(--color-series-2)" />
        <path data-anim="link-3" d="M126 125L180 133" stroke="var(--color-series-3)" />
      </g>
      <g className="table">
        <rect
          x={TABLE_X}
          y={TABLE_Y}
          width={64}
          height={5 * ROW_H}
          rx={3}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={ROW_LINES} stroke="var(--color-rule)" fill="none" />
        {WINDOWS.map((w, i) => (
          <rect
            key={w.key}
            data-anim={`row-${i + 1}`}
            x={TABLE_X + 1}
            y={TABLE_Y + w.row * ROW_H + 1}
            width={62}
            height={ROW_H - 2}
            fill={w.colour}
            fillOpacity={0.45}
          />
        ))}
      </g>
      <g className="sum" fontFamily={ART_MONO} textAnchor="middle">
        <line x1={244} y1={89} x2={262} y2={89} stroke="var(--color-ink-3)" />
        <rect x={262} y={71} width={44} height={36} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <text x={284} y={62} fontSize={9} fill="var(--color-ink-3)">
          sum
        </text>
        {SUMS.map((value, i) => (
          <text
            key={value}
            data-anim={`sum-${i}`}
            x={284}
            y={89}
            dominantBaseline="central"
            fontSize={13}
            fontWeight={700}
            fill="var(--color-ink)"
            opacity={0}
          >
            {value}
          </text>
        ))}
        <g className="tart-final" data-anim="sum-3">
          <text
            x={284}
            y={89}
            dominantBaseline="central"
            fontSize={13}
            fontWeight={700}
            fill="var(--color-ink)"
          >
            1.2
          </text>
        </g>
      </g>
    </svg>
  );
}
