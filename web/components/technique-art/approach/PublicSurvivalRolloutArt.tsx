/**
 * Card art for `terminal-policy-iteration/public-survival-rollout`: the column
 * comparison whose axis is lifetime, not score. Every legal column is forced,
 * the same imagined futures play on from each, and what is measured is how far
 * up the axis the game gets before it ends — each dashed line is one row rise.
 * On play the seven columns climb to their mean lifetimes, the individual
 * futures show as ticks scattered around each mean, and the ring lands on the
 * column that lasted longest.
 *
 * Server component. Motion lives in public-survival-rollout.css (transform and
 * opacity only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./public-survival-rollout.css";

const BASE_Y = 150;
const AXIS_X0 = 32;
const AXIS_X1 = 266;
const BAR_X0 = 40;
const BAR_STEP = 34;
const BAR_W = 10;
/** One dashed line per row rise, up the moves-survived axis. */
const RISES = [134, 118, 102, 86, 70, 54, 38];
/** Mean lifetime of each of the seven columns, as the top of its bar. */
const TOPS = [96, 62, 118, 44, 104, 130, 78];
/** Ticks around a mean: the individual futures that made it. */
const SAMPLES = [-11, -4, 7];
const BEST = 3;
const STEPS = ["a", "b", "c"] as const;

function barX(index: number): number {
  return BAR_X0 + index * BAR_STEP;
}

export function PublicSurvivalRolloutArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-public-survival-rollout",
        "Seven columns climbing an axis of moves survived, one dashed line per row rise, with a ring on the column that lasted longest",
        props,
      )}
    >
      <g className="axis">
        {RISES.map((y) => (
          <line
            key={y}
            x1={AXIS_X0}
            y1={y}
            x2={AXIS_X1}
            y2={y}
            stroke="var(--color-rule-strong)"
            strokeWidth="1"
            strokeDasharray="3 4"
          />
        ))}
        <line
          x1={AXIS_X0}
          y1={BASE_Y}
          x2={AXIS_X1}
          y2={BASE_Y}
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
        />
      </g>

      <g className="lifetimes">
        {TOPS.map((top, index) => (
          <rect
            key={top}
            data-anim={`grow-${STEPS[index % STEPS.length]}`}
            x={barX(index)}
            y={top}
            width={BAR_W}
            height={BASE_Y - top}
            rx="2"
            fill={index === BEST ? "var(--color-accent)" : "var(--color-accent-strong)"}
          />
        ))}
      </g>

      <g className="marks" data-anim="marks">
        {TOPS.map((top, index) => (
          <g key={top}>
            {SAMPLES.map((offset) => (
              <line
                key={offset}
                x1={barX(index) + 1}
                y1={top + offset}
                x2={barX(index) + BAR_W - 1}
                y2={top + offset}
                stroke="var(--color-ink-2)"
                strokeWidth="1"
              />
            ))}
            <line
              x1={barX(index) - 4}
              y1={top}
              x2={barX(index) + BAR_W + 4}
              y2={top}
              stroke="var(--color-ink-1)"
              strokeWidth="1.6"
            />
          </g>
        ))}
      </g>

      <rect
        className="pick"
        data-anim="pick"
        x={barX(BEST) - 6}
        y={TOPS[BEST] - 17}
        width={BAR_W + 12}
        height="27"
        rx="3"
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth="1.4"
      />

      <g className="rise-gap" stroke="var(--color-ink-3)" strokeWidth="1" fill="none">
        <path d="M272,118h6M275,118v16M272,134h6" />
      </g>

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="10" y="20">
          moves survived
        </text>
        <text x="282" y="130">
          rise
        </text>
      </g>

      <g className="tart-final" data-anim="final">
        <path
          d={`M${barX(BEST) + BAR_W / 2 - 4},164l4,-6l4,6z`}
          fill="var(--color-highlight)"
        />
        <text x="10" y="172" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          31 shared futures, a hundred moves each
        </text>
      </g>
    </svg>
  );
}
