/**
 * Card art for `constructive-reservoir/vertical-reservoir`: build the stack,
 * get nothing for it, and let the rise collect. Four 5s go up in one column
 * and pay nothing; the board rises; the row that arrives puts a fifth 5 under
 * them, the run of five clears, and the gray disc beside it cracks. The cycle
 * then rebuilds the stack, which is the resting frame.
 *
 * The board is a two-column crop, drawn inside a nested <svg> so the row
 * waiting below the floor is clipped until the rise brings it in.
 *
 * Server component. Motion lives in vertical-reservoir.css (transform and
 * opacity only); the markup is the resting frame.
 */
import { CellGlyph } from "@/components/discs";
import type { ArtProps } from "../registry";
import "./vertical-reservoir.css";

const CELL = 20;
const COLS = 2;
const ROWS = 7;
const ORIGIN = { x: 128, y: 20 };
/** Local coordinates inside the nested viewport. */
const GRID = [
  ...Array.from({ length: COLS + 1 }, (_, i) => `M${i * CELL},0v${ROWS * CELL}`),
  ...Array.from({ length: ROWS + 1 }, (_, i) => `M0,${i * CELL}h${COLS * CELL}`),
].join("");
const STACK_ROWS = [3, 4, 5, 6];

export function VerticalReservoirArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-vertical-reservoir", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Four fives stacked in a column pay nothing until the rise adds a fifth, and then the run clears and the gray disc cracks"
      }
    >
      <svg x={ORIGIN.x} y={ORIGIN.y} width={COLS * CELL} height={ROWS * CELL}>
        <path d={GRID} fill="none" stroke="var(--color-rule)" strokeWidth="0.8" />
        <g className="content" data-anim="rise">
          <g className="stack" data-anim="clear">
            <g className="fourth" data-anim="drop">
              <CellGlyph cell={5} x={0} y={STACK_ROWS[0] * CELL} s={CELL} />
            </g>
            {STACK_ROWS.slice(1).map((row) => (
              <CellGlyph key={row} cell={5} x={0} y={row * CELL} s={CELL} />
            ))}
            <CellGlyph cell={5} x={0} y={ROWS * CELL} s={CELL} />
          </g>
          <g className="gray-solid" data-anim="gray-solid">
            <CellGlyph cell={8} x={CELL} y={6 * CELL} s={CELL} />
          </g>
          <g className="gray-cracked" data-anim="gray-cracked" opacity="0">
            <CellGlyph cell={9} x={CELL} y={6 * CELL} s={CELL} />
          </g>
        </g>
        <rect
          className="flash"
          data-anim="flash"
          opacity="0"
          x="1"
          y={2 * CELL + 1}
          width={CELL - 2}
          height={5 * CELL - 2}
          rx="2"
          fill="var(--color-accent)"
        />
      </svg>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="30">
          one column,
        </text>
        <text x="8" y="42">
          four identical
        </text>
        <text x="8" y="54">
          discs
        </text>
      </g>
      <g className="step-a" data-anim="step-a" opacity="0">
        <text x="184" y="60" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          four in a run:
        </text>
        <text x="184" y="72" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          nothing scores
        </text>
      </g>
      <g className="step-b" data-anim="step-b" opacity="0">
        <text x="184" y="60" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-highlight)">
          the rise adds
        </text>
        <text x="184" y="72" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-highlight)">
          the fifth
        </text>
      </g>
      <g className="step-c" data-anim="step-c" opacity="0">
        <text x="184" y="60" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-accent)">
          five 5s clear,
        </text>
        <text x="184" y="72" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-accent)">
          the gray cracks
        </text>
      </g>
      <g className="tart-final" data-anim="rest">
        <text x="184" y="60" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          build four now,
        </text>
        <text x="184" y="72" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          collect on the rise
        </text>
      </g>
    </svg>
  );
}
