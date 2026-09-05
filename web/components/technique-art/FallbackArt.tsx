/**
 * The generic card art: a quiet 7x7 board with three discs and one disc
 * dropping into a column. Used for any technique name the registry does not
 * know, and as the placeholder while an art is still being drawn.
 *
 * This file also holds `artSvgProps`, the root <svg> attributes every art
 * shares. It lives here rather than in the registry so the arts never import
 * the registry at runtime (the registry imports them).
 */
import type { ArtProps } from "./registry";

export const ART_VIEWBOX = "0 0 320 180";
export const ART_MONO = "var(--font-mono)";

/** Root <svg> attributes for a card art named `name`, described by `label`. */
export function artSvgProps(
  name: string,
  label: string,
  { mode = "hover", title, className }: ArtProps,
) {
  return {
    className: className ? `tart tart--${name} ${className}` : `tart tart--${name}`,
    viewBox: ART_VIEWBOX,
    role: "img",
    "aria-label": title ?? label,
    "data-mode": mode,
  };
}

const CELL = 16;
const ORIGIN_X = 104;
const ORIGIN_Y = 44;

function centre(col: number, row: number): [number, number] {
  return [ORIGIN_X + CELL / 2 + col * CELL, ORIGIN_Y + CELL / 2 + row * CELL];
}

function Disc({ col, row, value }: { col: number; row: number; value: number }) {
  const [cx, cy] = centre(col, row);
  return (
    <>
      <circle cx={cx} cy={cy} r={6.5} fill={`var(--color-disc-${value})`} />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={8}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${value}-fg)`}
      >
        {value}
      </text>
    </>
  );
}

const GRID_LINES = [
  ...[1, 2, 3, 4, 5, 6].map((i) => `M${ORIGIN_X + i * CELL} ${ORIGIN_Y}v${7 * CELL}`),
  ...[1, 2, 3, 4, 5, 6].map((i) => `M${ORIGIN_X} ${ORIGIN_Y + i * CELL}h${7 * CELL}`),
].join("");

export function FallbackArt(props: ArtProps) {
  return (
    <svg {...artSvgProps("fallback", "A Drop7 board with a disc dropping into a column", props)}>
      <g className="board">
        <rect
          x={ORIGIN_X}
          y={ORIGIN_Y}
          width={7 * CELL}
          height={7 * CELL}
          rx={4}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={GRID_LINES} stroke="var(--color-rule)" strokeWidth={1} fill="none" />
      </g>
      <g className="discs">
        <Disc col={1} row={6} value={3} />
        <Disc col={4} row={6} value={5} />
        <Disc col={4} row={5} value={2} />
      </g>
      <g className="tart-final" data-anim="drop">
        <Disc col={2} row={6} value={4} />
      </g>
    </svg>
  );
}
