/**
 * Card art for the native (C++) engine: a 7x7 board drawn as bits. A column
 * mask lights, one row's occupancy mask is looked up in the run-length table,
 * and gravity compacts the column in a single step instead of a rescan.
 *
 * Server component. Motion lives in engine-native.css (opacity, transform
 * and stroke-dashoffset only); the markup is the resting frame.
 */
import "./engine-native.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const CELL = 13;
const GRID = { x: 22, y: 44 };
const COLUMN = 2;
const ROW = 5;

function gridPath(x: number, y: number, cols: number, rows: number, s: number): string {
  const parts = [`M${x},${y} h${cols * s} v${rows * s} h${-cols * s} z`];
  for (let c = 1; c < cols; c++) parts.push(`M${x + c * s},${y} v${rows * s}`);
  for (let r = 1; r < rows; r++) parts.push(`M${x},${y + r * s} h${cols * s}`);
  return parts.join(" ");
}

/** One filled square per set bit, as a single path. */
function bitsPath(cells: ReadonlyArray<readonly [number, number]>, s = CELL, inset = 2): string {
  const side = s - inset * 2;
  return cells
    .map(([c, r]) => `M${GRID.x + c * s + inset},${GRID.y + r * s + inset} h${side} v${side} h${-side} z`)
    .join(" ");
}

/** Occupied cells (column, row from the top) in every column but the one being compacted. */
const OTHER_BITS: ReadonlyArray<readonly [number, number]> = [
  [0, 4], [0, 5], [0, 6],
  [1, 5], [1, 6],
  [3, 3], [3, 4], [3, 5], [3, 6],
  [4, 6],
  [5, 4], [5, 5], [5, 6],
  [6, 5], [6, 6],
];
const COLUMN_BEFORE: ReadonlyArray<readonly [number, number]> = [[COLUMN, 2], [COLUMN, 3], [COLUMN, 5]];
const COLUMN_AFTER: ReadonlyArray<readonly [number, number]> = [[COLUMN, 4], [COLUMN, 5], [COLUMN, 6]];

/** Row 5 occupancy, left to right, and the run length at each position. */
const ROW_MASK = [1, 1, 1, 1, 0, 1, 1] as const;
const RUN_LENGTHS = [4, 4, 4, 4, 0, 2, 2] as const;

const MASK = { x: 150, y: 54, s: 12 };
const TABLE = { x: 150, y: 100, s: 12 };

export function EngineNativeArt({ mode = "hover", title, className }: ArtProps) {
  const maskBits = ROW_MASK.map((bit, i) => (bit ? `M${MASK.x + i * MASK.s + 2},${MASK.y + 2} h8 v8 h-8 z` : "")).join(" ");
  return (
    <svg
      className={["tart", "tart--engine-native", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Native engine: a board as bits, a column mask, a run-length table lookup, and gravity in one step"}
    >
      <g className="board">
        <path d={gridPath(GRID.x, GRID.y, 7, 7, CELL)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <rect
          className="colmask"
          data-anim="colmask"
          x={GRID.x + COLUMN * CELL}
          y={GRID.y}
          width={CELL}
          height={7 * CELL}
          fill="var(--color-accent-soft)"
        />
        <rect
          className="rowmask"
          data-anim="rowmask"
          x={GRID.x}
          y={GRID.y + ROW * CELL}
          width={7 * CELL}
          height={CELL}
          fill="var(--color-accent-soft)"
        />
        <path d={bitsPath(OTHER_BITS)} fill="var(--color-ink-3)" opacity="0.7" />
        <path className="before" data-anim="before" d={bitsPath(COLUMN_BEFORE)} fill="var(--color-series-1)" opacity="0" />
        <path className="after" data-anim="after" d={bitsPath(COLUMN_AFTER)} fill="var(--color-series-1)" />
      </g>
      <g className="lookup">
        <path d={gridPath(MASK.x, MASK.y, 7, 1, MASK.s)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <path className="maskbits" data-anim="maskbits" d={maskBits} fill="var(--color-series-2)" />
        <path
          className="arrow"
          data-anim="arrow"
          d="M192,72 v18 m-4,-5 l4,5 l4,-5"
          pathLength={1}
          strokeDasharray="1"
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
        <path d={gridPath(TABLE.x, TABLE.y, 7, 1, TABLE.s)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <rect className="hit" data-anim="hit" x={TABLE.x} y={TABLE.y} width={7 * TABLE.s} height={TABLE.s} fill="var(--color-accent-soft)" />
        <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)" textAnchor="middle">
          {RUN_LENGTHS.map((length, i) => (
            <text key={i} x={TABLE.x + i * TABLE.s + TABLE.s / 2} y={TABLE.y + 9.5}>
              {length}
            </text>
          ))}
        </g>
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="150" y="48">row 5 mask 1111011</text>
        <text x="204" y="85">RUN_LENGTH[mask]</text>
        <text x="150" y="128">gravity: pext(word, mask)</text>
        <text x="22" y="152">column word, four bits a cell</text>
      </g>
      <g className="tart-final">
        <text x="22" y="172" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          masks in, lookups out, no rescans
        </text>
      </g>
    </svg>
  );
}
