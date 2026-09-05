/**
 * Card art for `fair-expectimax/rollout-improvement`: the improvement step
 * itself, drawn as the paired lattice it really is. Seven tapes down the left,
 * one per value the next disc can take, and every candidate column replayed on
 * all seven by the base policy — the same imagined futures for every column,
 * so the comparison is paired. On play the lattice fills a tape at a time, the
 * column means grow, and the column the rollout picks lights beside the one the
 * base policy would have played on its own.
 *
 * Server component. Motion lives in rollout-improvement.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtDisc, type BoardGeometry } from "../board";
import "./rollout-improvement.css";

/** One column of cells for the seven tape heads, reusing the board's disc. */
const TAPES: BoardGeometry = { x: 14, y: 28, cell: 16, cols: 1, rows: 7 };

/** Row centres: one per tape, the first future disc stratified over all seven values. */
const ROW_Y = [36, 52, 68, 84, 100, 116, 132];

/** Candidate columns at the root, each replayed on every tape. */
const COL_X = [72, 122, 172, 222, 272];
const CELL_W = 30;

/** The move the base policy plays by itself, and the move its rollout picks. */
const BASE = 3;
const IMPROVED = 1;

const BAR_BASE = 160;
const BAR_HEIGHT = [12, 24, 8, 17, 7];

function marker(cx: number): string {
  return `M${cx - 5},13L${cx + 5},13L${cx},22z`;
}

const COLUMN_TICKS = COL_X.map((cx) => `M${cx},25v3`).join("");

export function RolloutImprovementArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-rollout-improvement",
        "Seven stratified tapes against five candidate columns, every cell played out by the base policy, and the improved move beside the base move",
        props,
      )}
    >
      <rect
        className="band"
        data-anim="band"
        x={COL_X[IMPROVED] - CELL_W / 2 - 4}
        y="29"
        width={CELL_W + 8}
        height="132"
        rx="4"
        fill="var(--color-accent-soft)"
      />
      <path d={COLUMN_TICKS} fill="none" stroke="var(--color-ink-4)" strokeWidth="1.2" />
      <path d={marker(COL_X[BASE])} fill="none" stroke="var(--color-ink-2)" strokeWidth="1.2" />

      <g className="heads">
        {ROW_Y.map((y, tape) => (
          <ArtDisc key={y} value={tape + 1} col={0} row={tape} g={TAPES} />
        ))}
      </g>
      {ROW_Y.map((y, tape) => (
        <g key={y} className="tape" data-anim={`tape-${tape}`}>
          {COL_X.map((cx) => (
            <rect key={cx} x={cx - CELL_W / 2} y={y - 5.5} width={CELL_W} height="11" rx="3" fill="var(--color-ink-4)" />
          ))}
        </g>
      ))}

      <line
        x1={COL_X[0] - CELL_W / 2}
        y1={BAR_BASE + 1}
        x2={COL_X[COL_X.length - 1] + CELL_W / 2}
        y2={BAR_BASE + 1}
        stroke="var(--color-rule-strong)"
        strokeWidth="1"
      />
      <g className="means" data-anim="means">
        {COL_X.map((cx, index) => (
          <rect
            key={cx}
            x={cx - 5}
            y={BAR_BASE - BAR_HEIGHT[index]}
            width="10"
            height={BAR_HEIGHT[index]}
            rx="2"
            fill={index === IMPROVED ? "var(--color-accent)" : "var(--color-ink-3)"}
          />
        ))}
      </g>

      <g className="improved" data-anim="improved">
        <path d={marker(COL_X[IMPROVED])} fill="var(--color-accent)" />
        <text x={COL_X[IMPROVED] + 10} y="21" fontFamily={ART_MONO} fontSize="9" fill="var(--color-accent)">
          new
        </text>
      </g>

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="10" y="21">
          7 tapes
        </text>
        <text x={COL_X[BASE] + 10} y="21">
          base
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="174" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          a new move, from playing the base policy on
        </text>
      </g>
    </svg>
  );
}
