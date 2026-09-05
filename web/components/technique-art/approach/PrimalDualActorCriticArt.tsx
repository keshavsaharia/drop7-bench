/**
 * Card art for `ntuple-rl/primal-dual-actor-critic`: two quantities traded
 * against each other across the five-drop cycle the board rises on. Above the
 * constraint line, the drift the policy is meant to keep at or below zero;
 * below it, the price charged for breaking it. On play each cycle's drift
 * stands up on the wrong side of the line and the price answers with another
 * step down. At rest the staircase is four steps deep and the drift has never
 * come back to the line.
 *
 * Server component. Motion lives in primal-dual-actor-critic.css (transform
 * and opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./primal-dual-actor-critic.css";

/** The constraint line, and the four aligned five-drop cycles along it. */
const ZERO_Y = 98;
const EDGES = [52, 112, 172, 232, 292];
const CYCLES = [0, 1, 2, 3];
/** Each cycle's drift, all of it above the line. */
const DRIFT = [30, 34, 30, 36];
const DRIFT_W = 26;
/** The price, one step deeper per cycle. */
const PRICE = [9, 18, 27, 36];

function cycleMid(index: number): number {
  return (EDGES[index] + EDGES[index + 1]) / 2;
}

export function PrimalDualActorCriticArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-primal-dual-actor-critic",
        "Over four five-drop cycles the drift stands above the constraint line and the price charged for it steps deeper below",
        props,
      )}
    >
      <path
        d={`M${EDGES[0]},28v6M${EDGES[0]},28H${EDGES[1]}M${EDGES[1]},28v6`}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth={1}
      />
      <path
        d={EDGES.map((x) => `M${x},44v108`).join("")}
        fill="none"
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        strokeDasharray="2 5"
      />
      <path
        d={EDGES.map((x) => `M${x},159v-9m-3.5,3.5l3.5,-3.5l3.5,3.5`).join("")}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth={1.2}
        strokeLinejoin="round"
      />

      {CYCLES.map((index) => (
        <rect
          key={`drift-${index}`}
          className="bar"
          data-anim={`drift-${index + 1}`}
          x={cycleMid(index) - DRIFT_W / 2}
          y={ZERO_Y - DRIFT[index]}
          width={DRIFT_W}
          height={DRIFT[index]}
          rx={2}
          fill="var(--color-series-2)"
        />
      ))}

      {CYCLES.map((index) => (
        <rect
          key={`price-${index}`}
          className="step"
          data-anim={`price-${index + 1}`}
          x={EDGES[index]}
          y={ZERO_Y}
          width={EDGES[index + 1] - EDGES[index]}
          height={PRICE[index]}
          fill="var(--color-accent-strong)"
          stroke="var(--color-accent)"
          strokeWidth={1}
        />
      ))}

      <line
        x1={44}
        y1={ZERO_Y}
        x2={296}
        y2={ZERO_Y}
        stroke="var(--color-highlight)"
        strokeWidth={1.2}
        strokeDasharray="3 3"
      />

      <g data-anim="link">
        <path
          d="M102,70c8,10 8,14 0,22"
          fill="none"
          stroke="var(--color-ink-2)"
          strokeWidth={1.3}
        />
        <path d="M102,98l-3.5,-7h7z" fill="var(--color-ink-2)" />
      </g>

      <g fontFamily={ART_MONO} fontSize={9}>
        <text x={cycleMid(0)} y={22} textAnchor="middle" fill="var(--color-ink-3)">
          5 drops
        </text>
        <text x={10} y={70} fill="var(--color-ink-3)">
          drift
        </text>
        <text x={10} y={124} fill="var(--color-ink-3)">
          price
        </text>
        <text x={40} y={ZERO_Y} textAnchor="end" dominantBaseline="central" fill="var(--color-highlight)">
          0
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={176} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the price rose, the drift stayed above zero
        </text>
      </g>
    </svg>
  );
}
