/**
 * Board primitives for card art.
 *
 * Several approaches are about something that happens on the board itself: a
 * chain reaction, a column that cannot clear, a disc that reveals a gray one.
 * An art that draws one should draw a real board rather than an abstraction
 * of one, so this module holds the pieces: the grid, a numbered disc, a gray
 * disc, the highlight ring, and the `+7` score label the game itself shows
 * when a wave clears.
 *
 * The score labels come from the engine's `scoreForWave`, so an art can name
 * a wave depth and never a number: depth 1 is +7, depth 2 is +39, depth 3 is
 * +109. Nothing here invents a value.
 *
 * Server components; every colour is a token and every motion belongs to the
 * art's own stylesheet through the shared `.tart` contract in art.css.
 */
import { scoreForWave } from "../../../src/core/typescript/engine.ts";
import { ART_MONO } from "./FallbackArt";

export interface BoardGeometry {
  /** Left edge of the grid. */
  x: number;
  /** Top edge of the grid. */
  y: number;
  /** Side of one square cell. */
  cell: number;
  cols: number;
  rows: number;
}

/** A 7x7 board that leaves room above for the next disc and below for a caption. */
export const BOARD: BoardGeometry = { x: 16, y: 26, cell: 18, cols: 7, rows: 7 };

/** The same board pushed to the right half of the frame, for a split art. */
export const BOARD_RIGHT: BoardGeometry = { ...BOARD, x: 178 };

export function boardWidth(g: BoardGeometry = BOARD): number {
  return g.cols * g.cell;
}

export function boardHeight(g: BoardGeometry = BOARD): number {
  return g.rows * g.cell;
}

/** Centre of one cell, in the art's 320x180 user space. */
export function cellCenter(col: number, row: number, g: BoardGeometry = BOARD): [number, number] {
  return [g.x + (col + 0.5) * g.cell, g.y + (row + 0.5) * g.cell];
}

/** Centre x of a column, for a marker or a drop path above the board. */
export function columnX(col: number, g: BoardGeometry = BOARD): number {
  return g.x + (col + 0.5) * g.cell;
}

/** Radius of a disc that sits comfortably inside a cell. */
export function discRadius(g: BoardGeometry = BOARD): number {
  return g.cell * 0.4;
}

/** The board frame and its grid lines. Draw this first; discs go on top. */
export function ArtBoard({
  g = BOARD,
  className,
  children,
}: {
  g?: BoardGeometry;
  className?: string;
  children?: React.ReactNode;
}) {
  const lines = [
    ...Array.from({ length: g.cols - 1 }, (_, i) => `M${g.x + (i + 1) * g.cell} ${g.y}v${boardHeight(g)}`),
    ...Array.from({ length: g.rows - 1 }, (_, i) => `M${g.x} ${g.y + (i + 1) * g.cell}h${boardWidth(g)}`),
  ].join("");
  return (
    <g className={className ? `art-board ${className}` : "art-board"}>
      <rect
        x={g.x}
        y={g.y}
        width={boardWidth(g)}
        height={boardHeight(g)}
        rx={4}
        fill="var(--color-cell)"
        stroke="var(--color-rule-strong)"
      />
      <path d={lines} stroke="var(--color-rule)" strokeWidth={1} fill="none" />
      {children}
    </g>
  );
}

export interface DiscProps {
  col: number;
  row: number;
  g?: BoardGeometry;
  /** Passed straight through so the art's stylesheet can animate this disc. */
  "data-anim"?: string;
  className?: string;
  opacity?: number;
}

/** One numbered disc, 1 through 7, drawn in its own colour with its numeral. */
export function ArtDisc({ value, col, row, g = BOARD, className, ...rest }: DiscProps & { value: number }) {
  const [cx, cy] = cellCenter(col, row, g);
  const r = discRadius(g);
  return (
    <g className={className} {...rest}>
      <circle cx={cx} cy={cy} r={r} fill={`var(--color-disc-${value})`} />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={r * 1.25}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${value}-fg)`}
      >
        {value}
      </text>
    </g>
  );
}

/**
 * A gray disc: solid (two hits from clearing) or cracked (one hit away from
 * revealing the number underneath).
 */
export function ArtGray({ cracked = false, col, row, g = BOARD, className, ...rest }: DiscProps & { cracked?: boolean }) {
  const [cx, cy] = cellCenter(col, row, g);
  const r = discRadius(g);
  return (
    <g className={className} {...rest}>
      {cracked ? (
        <>
          <circle
            cx={cx}
            cy={cy}
            r={r}
            fill="var(--color-disc-gray-core)"
            stroke="var(--color-disc-gray)"
            strokeWidth={r * 0.28}
            strokeDasharray={`${r * 0.55} ${r * 0.42}`}
          />
          <circle cx={cx} cy={cy} r={r * 0.36} fill="var(--color-disc-gray)" />
        </>
      ) : (
        <>
          <circle cx={cx} cy={cy} r={r} fill="var(--color-disc-gray)" />
          <circle cx={cx} cy={cy} r={r * 0.68} fill="var(--color-disc-gray-core)" />
          <circle cx={cx} cy={cy} r={r * 0.5} fill="var(--color-disc-gray)" />
        </>
      )}
    </g>
  );
}

/** Ring around a cell, for the disc under consideration or the cell about to clear. */
export function ArtRing({ col, row, g = BOARD, className, ...rest }: DiscProps) {
  const [cx, cy] = cellCenter(col, row, g);
  return (
    <rect
      className={className}
      x={cx - g.cell / 2 + 1}
      y={cy - g.cell / 2 + 1}
      width={g.cell - 2}
      height={g.cell - 2}
      rx={2}
      fill="none"
      stroke="var(--color-highlight)"
      strokeWidth={1.6}
      {...rest}
    />
  );
}

/**
 * The score label the game floats over a clearing disc. `depth` is the wave's
 * position in the chain, and the number is `scoreForWave(depth)` from the
 * engine: +7 for the first wave, +39 for the second, +109 for the third.
 */
export function ArtScore({
  depth,
  col,
  row,
  g = BOARD,
  className,
  ...rest
}: DiscProps & { depth: number }) {
  const [cx, cy] = cellCenter(col, row, g);
  return (
    <text
      className={className}
      x={cx}
      y={cy}
      textAnchor="middle"
      dominantBaseline="central"
      fontSize={g.cell * 0.62}
      fontWeight={700}
      fontFamily={ART_MONO}
      fill="var(--color-highlight)"
      {...rest}
    >
      +{scoreForWave(depth)}
    </text>
  );
}

/** The running total after `depth` waves of `discs` discs each, as the game counts it. */
export function waveTotal(waves: readonly { depth: number; discs: number }[]): number {
  return waves.reduce((sum, wave) => sum + wave.discs * scoreForWave(wave.depth), 0);
}

export { scoreForWave };

/**
 * A whole board from the engine's cell encoding, row-major from the top:
 * 0 empty, 1-7 a numbered disc, 8 solid gray, 9 cracked gray. Cells may be
 * given as a 49-character string or an array.
 */
export function ArtCells({
  cells,
  g = BOARD,
  className,
}: {
  cells: string | readonly number[];
  g?: BoardGeometry;
  className?: string;
}) {
  const values = typeof cells === "string" ? [...cells].map(Number) : cells;
  return (
    <g className={className}>
      {values.map((value, index) => {
        if (!value) return null;
        const col = index % g.cols;
        const row = Math.floor(index / g.cols);
        const key = `${col}-${row}`;
        if (value === 8) return <ArtGray key={key} col={col} row={row} g={g} />;
        if (value === 9) return <ArtGray key={key} cracked col={col} row={row} g={g} />;
        return <ArtDisc key={key} value={value} col={col} row={row} g={g} />;
      })}
    </g>
  );
}
