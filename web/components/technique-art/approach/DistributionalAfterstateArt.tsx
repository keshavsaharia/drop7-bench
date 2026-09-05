/**
 * Card art for `afterstate-learning/distributional-afterstate`: the seven
 * afterstates one position offers, ranked by a spread rather than a number.
 * On play each spread grows from a single tick into quantile bars, and then
 * the columns reorder themselves by what those spreads say.
 *
 * The disc in every well is the next disc of the `row-clear` position in
 * web/content/learn/rules-scenarios.json — the same disc, dropped into each
 * of the seven columns in turn.
 *
 * Server component. Motion lives in distributional-afterstate.css (transform
 * only); the markup is the resting frame.
 */
import { CellGlyph } from "@/components/discs";
import type { ArtProps } from "../registry";
import "./distributional-afterstate.css";

const NEXT_DISC = 3;
const SLOT = 44;
const X0 = 12;
const WELL = { w: 26, h: 30, y: 22 };
const SPREAD_MID = 108;
/** Half-height of each candidate's quantile spread, in user units. */
const SPREADS = [26, 14, 32, 9, 20, 30, 12];
/**
 * Where each candidate sits once the row is ordered by spread (2, 5, 0, 4, 1,
 * 6, 3), as a travel in slots. The resting frame is the ordered row, and
 * distributional-afterstate.css repeats these as --dx.
 */
const TRAVEL = [2, 3, -2, 3, -1, -4, -1].map((slots) => slots * SLOT);

function slotX(index: number): number {
  return X0 + index * SLOT;
}

export function DistributionalAfterstateArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-distributional-afterstate", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Seven resolved afterstates, each carrying a spread of outcomes instead of one number, then reordered by that spread"
      }
    >
      <line x1="8" y1={SPREAD_MID} x2="312" y2={SPREAD_MID} stroke="var(--color-rule)" strokeWidth="0.8" />
      {SPREADS.map((half, index) => {
        const x = slotX(index);
        return (
          <g
            key={index}
            className={`slot slot-${index}`}
            data-anim="slot"
            transform={`translate(${TRAVEL[index]} 0)`}
          >
            <rect
              x={x}
              y={WELL.y}
              width={WELL.w}
              height={WELL.h}
              rx="3"
              fill="var(--color-cell)"
              stroke="var(--color-rule-strong)"
              strokeWidth="0.8"
            />
            <CellGlyph cell={NEXT_DISC} x={x + 3} y={WELL.y + WELL.h - 23} s={20} />
            <g className="spread" data-anim="spread">
              <line
                x1={x + WELL.w / 2}
                y1={SPREAD_MID - half}
                x2={x + WELL.w / 2}
                y2={SPREAD_MID + half}
                stroke="var(--color-accent-strong)"
                strokeWidth="7"
                strokeLinecap="round"
              />
              <line
                x1={x + WELL.w / 2 - 6}
                y1={SPREAD_MID}
                x2={x + WELL.w / 2 + 6}
                y2={SPREAD_MID}
                stroke="var(--color-accent)"
                strokeWidth="2"
              />
            </g>
          </g>
        );
      })}
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="16">
          seven afterstates
        </text>
        <text x="8" y={SPREAD_MID - 40}>
          quantiles
        </text>
      </g>
      <g className="caption-a" data-anim="caption-a" opacity="0">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          one number each
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          ranked by the whole distribution
        </text>
      </g>
    </svg>
  );
}
