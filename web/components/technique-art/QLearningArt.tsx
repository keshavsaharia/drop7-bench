/**
 * Q-learning card art. Four cells in a corridor, a value bar under each, an
 * agent dot and a reward in the last cell. On play the dot walks the corridor
 * three times: the first pass lifts the last bar, and each later pass backs
 * the value up one cell with a short leftward arrow. The bars end as a
 * staircase.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./q-learning.css";

const CELL_X = [36, 100, 164, 228];
const BAR_HEIGHTS = [8, 18, 28, 40];
const FLOOR = 150;

export function QLearningArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "q-learning",
        "An agent walks a four-cell corridor to a reward and the value bars under the cells fill in from the end backwards",
        props,
      )}
    >
      <g className="cells" fill="var(--color-raised)" stroke="var(--color-rule-strong)">
        {CELL_X.map((x) => (
          <rect key={x} x={x} y={30} width={56} height={52} rx={6} />
        ))}
      </g>
      <g className="reward">
        <circle cx={256} cy={56} r={9} fill="var(--color-highlight)" />
        <text
          x={256}
          y={56}
          textAnchor="middle"
          dominantBaseline="central"
          fontSize={12}
          fontWeight={700}
          fontFamily={ART_MONO}
          fill="var(--color-bg)"
        >
          +
        </text>
      </g>
      <circle data-anim="agent" className="agent" cx={64} cy={56} r={7} fill="var(--color-accent)" />
      <g className="values">
        <text x={22} y={150} fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-3)">
          Q
        </text>
        <line x1={36} y1={FLOOR + 0.5} x2={284} y2={FLOOR + 0.5} stroke="var(--color-rule-strong)" />
        {BAR_HEIGHTS.map((h, i) => (
          <rect
            key={i}
            data-anim={i === 0 ? undefined : `bar-${i + 1}`}
            className="bar"
            x={CELL_X[i] + 12}
            y={FLOOR - h}
            width={32}
            height={h}
            rx={2}
            fill="var(--color-accent-strong)"
          />
        ))}
      </g>
      <g
        className="tart-final"
        data-anim="arrows"
        stroke="var(--color-accent)"
        strokeWidth={1.5}
        strokeLinecap="round"
        strokeLinejoin="round"
        fill="none"
      >
        <path d="M236 104H212M218 100L212 104L218 108" />
        <path data-anim="arrow-2" d="M172 104H148M154 100L148 104L154 108" />
      </g>
    </svg>
  );
}
