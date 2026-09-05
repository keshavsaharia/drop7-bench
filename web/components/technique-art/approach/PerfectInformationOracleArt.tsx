/**
 * Card art for `oracle-curriculum/perfect-information-oracle`: the planner is
 * handed the tape. Every disc on the board carries its number and no cover,
 * the seven discs still to come are unrolled above it, and on play a path is
 * drawn through the tape and down into the columns it will use, after which
 * the board clears in waves. The strip is drawn in the oracle colour, because
 * nothing that reads it can be deployed.
 *
 * The board is the tree position from web/content/learn/concept-scenarios.json
 * (its next disc, 1, opens the strip). Server component; motion lives in
 * perfect-information-oracle.css (stroke-dashoffset and opacity only).
 */
import { CellGlyph } from "@/components/discs";
import type { ArtProps } from "../registry";
import "./perfect-information-oracle.css";

const CELL = 15;
const BOARD = { x: 108, y: 52 };
const STRIP = { x: 96, y: 12, cell: 20 };
/** The tape: the scenario's next disc, then the six the oracle is also shown. */
const TAPE = [1, 4, 6, 2, 5, 3, 7];
/** concept-scenarios.json `tree`: column 5 holds 7 over 2 over 4, column 4 a 3. */
const DISCS: ReadonlyArray<readonly [number, number, number]> = [
  [5, 4, 7],
  [5, 5, 2],
  [5, 6, 4],
  [4, 6, 3],
];

const GRID = [
  ...Array.from({ length: 8 }, (_, i) => `M${BOARD.x + i * CELL},${BOARD.y}v${7 * CELL}`),
  ...Array.from({ length: 8 }, (_, i) => `M${BOARD.x},${BOARD.y + i * CELL}h${7 * CELL}`),
].join("");

function cellX(col: number): number {
  return BOARD.x + col * CELL;
}
function cellY(row: number): number {
  return BOARD.y + row * CELL;
}

/** Through the tape, then down into column 5 and across to column 4. */
const PLAN = `M${STRIP.x + 10},${STRIP.y + 10}H${STRIP.x + 130}L${cellX(5) + CELL / 2},${BOARD.y}V${cellY(4) + CELL / 2}H${cellX(4) + CELL / 2}V${cellY(6) + CELL / 2}`;

export function PerfectInformationOracleArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-perfect-information-oracle", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "A planner shown every future disc and every hidden value plans a path through them and clears the board"
      }
    >
      <rect
        x={STRIP.x - 4}
        y={STRIP.y - 3}
        width={7 * 22 + 8}
        height={26}
        rx="4"
        fill="none"
        stroke="var(--color-reads-oracle)"
        strokeWidth="1.2"
      />
      <g className="tape">
        {TAPE.map((value, index) => (
          <CellGlyph key={index} cell={value} x={STRIP.x + index * 22} y={STRIP.y} s={STRIP.cell} />
        ))}
      </g>
      <path d={GRID} fill="none" stroke="var(--color-rule)" strokeWidth="0.8" />
      <g className="wave-1" data-anim="wave-1">
        {DISCS.slice(0, 3).map(([col, row, value]) => (
          <CellGlyph key={`${col}-${row}`} cell={value} x={cellX(col)} y={cellY(row)} s={CELL} />
        ))}
      </g>
      <g className="wave-2" data-anim="wave-2">
        {DISCS.slice(3).map(([col, row, value]) => (
          <CellGlyph key={`${col}-${row}`} cell={value} x={cellX(col)} y={cellY(row)} s={CELL} />
        ))}
      </g>
      <path
        className="plan"
        data-anim="plan"
        d={PLAN}
        fill="none"
        stroke="var(--color-reads-oracle)"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray="340"
      />
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-reads-oracle)">
        <text x="8" y="24">
          every disc
        </text>
        <text x="8" y="36">
          still to come
        </text>
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="76">
          no covers:
        </text>
        <text x="8" y="88">
          every value
        </text>
        <text x="8" y="100">
          is known
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="174" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-reads-oracle)">
          a ceiling, never a policy
        </text>
      </g>
    </svg>
  );
}
