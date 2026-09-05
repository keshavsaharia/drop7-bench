/**
 * Card art for constructive planning: one column of a mini-board stacks four
 * identical high discs above a gray row. The fourth lands and nothing
 * happens; the board rises one row; all four flash and clear; the gray disc
 * beneath them cracks. The resting frame (the SVG's own attributes) is the
 * stacked column; the first keyframe holds the fourth disc above the stack.
 *
 * Server component. Motion is CSS in constructive-planning.css on the
 * data-anim elements; the shared play/pause contract lives in art.css.
 */
import "./constructive-planning.css";
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";

const CELL = 16;
const COLS = 3;
const ROWS = 7;
const ORIGIN_X = 160 - (COLS * CELL) / 2;
const ORIGIN_Y = 34;
const STACK_COL = 1;
const VALUE = 6;

function centre(col: number, row: number): [number, number] {
  return [ORIGIN_X + CELL / 2 + col * CELL, ORIGIN_Y + CELL / 2 + row * CELL];
}

function Six({ row }: { row: number }) {
  const [cx, cy] = centre(STACK_COL, row);
  return (
    <g data-anim="pop">
      <circle cx={cx} cy={cy} r={6.5} fill={`var(--color-disc-${VALUE})`} />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={8}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${VALUE}-fg)`}
      >
        {VALUE}
      </text>
    </g>
  );
}

function Gray({ col, row }: { col: number; row: number }) {
  const [cx, cy] = centre(col, row);
  return (
    <circle cx={cx} cy={cy} r={6.5} fill="var(--color-disc-gray)" stroke="var(--color-disc-gray-core)" strokeWidth={2} />
  );
}

const GRID_LINES = [
  ...[1, 2].map((i) => `M${ORIGIN_X + i * CELL} ${ORIGIN_Y}v${ROWS * CELL}`),
  ...[1, 2, 3, 4, 5, 6].map((i) => `M${ORIGIN_X} ${ORIGIN_Y + i * CELL}h${COLS * CELL}`),
].join("");

export function ConstructivePlanningArt(props: ArtProps) {
  const [crackX, crackY] = centre(STACK_COL, ROWS - 2);
  return (
    <svg
      {...artSvgProps(
        "constructive-planning",
        "Four 6s stacked above a gray row; the board rises one row and all four clear together",
        props,
      )}
    >
      <g className="board">
        <rect
          x={ORIGIN_X}
          y={ORIGIN_Y}
          width={COLS * CELL}
          height={ROWS * CELL}
          rx={4}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={GRID_LINES} stroke="var(--color-rule)" strokeWidth={1} fill="none" />
      </g>
      <g className="tart-final" data-anim="shift">
        <Gray col={0} row={ROWS - 1} />
        <Gray col={1} row={ROWS - 1} />
        <Gray col={2} row={ROWS - 1} />
        <Six row={5} />
        <Six row={4} />
        <Six row={3} />
        <g data-anim="land">
          <Six row={2} />
        </g>
      </g>
      <g data-anim="newrow" opacity={0}>
        <Gray col={0} row={ROWS - 1} />
        <Gray col={1} row={ROWS - 1} />
        <Gray col={2} row={ROWS - 1} />
      </g>
      <circle
        data-anim="crack"
        cx={crackX}
        cy={crackY}
        r={4.5}
        fill="none"
        stroke="var(--color-disc-gray-core)"
        strokeWidth={1.5}
        strokeDasharray="2 2"
        opacity={0}
      />
      <rect
        data-anim="flash"
        x={ORIGIN_X + STACK_COL * CELL + 2}
        y={ORIGIN_Y + CELL + 2}
        width={CELL - 4}
        height={4 * CELL - 4}
        rx={4}
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth={1.5}
        opacity={0}
      />
    </svg>
  );
}
