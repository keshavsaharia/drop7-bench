/**
 * Card art for `terminal-policy-iteration/public-rollout-policy-iteration`:
 * one step of policy improvement, drawn as the loop it belongs to. A trusted
 * base policy plays out every column over the same fifteen imagined futures —
 * the comb of hairlines every track crosses — the column with the best average
 * becomes the improved policy, and the improved policy is what the next step
 * would start from. On play the tracks run out across the shared futures, one
 * of them wins, and the return arm closes the loop back onto the base.
 *
 * Server component. Motion lives in public-rollout-policy-iteration.css
 * (stroke-dashoffset and opacity only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./public-rollout-policy-iteration.css";

const TRACK_X0 = 74;
const TRACK_X1 = 224;
/** One track per legal column. */
const TRACK_YS = [46, 60, 74, 88, 102, 116, 130];
const BEST = 3;
const STEPS = ["a", "b", "c"] as const;
/** The fifteen futures every column is replayed through, as one comb. */
const COMB = Array.from({ length: 15 }, (_, i) => `M${78 + i * 10},40v96`).join("");

export function PublicRolloutPolicyIterationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-public-rollout-policy-iteration",
        "A base policy plays every column out across the same fifteen futures, the best average becomes the improved policy, and an arm returns it to the base",
        props,
      )}
    >
      <g className="base">
        <rect
          x="8"
          y="76"
          width="46"
          height="26"
          rx="4"
          fill="var(--color-raised)"
          stroke="var(--color-ink-3)"
          strokeWidth="1.1"
        />
        <text
          x="31"
          y="89"
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily={ART_MONO}
          fontSize="9"
          fill="var(--color-ink-2)"
        >
          base
        </text>
        <path
          d="M56,89h10M64,86l5,3l-5,3"
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
        />
      </g>

      <line x1="72" y1="42" x2="72" y2="134" stroke="var(--color-ink-3)" strokeWidth="1.2" />
      <path
        className="comb"
        data-anim="comb"
        d={COMB}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth="0.9"
        strokeDasharray="1 5"
      />

      <g className="tracks">
        {TRACK_YS.map((y, index) => (
          <line
            key={y}
            data-anim={`track-${STEPS[index % STEPS.length]}`}
            x1={TRACK_X0}
            y1={y}
            x2={TRACK_X1}
            y2={y}
            pathLength={1}
            strokeDasharray="1"
            stroke="var(--color-ink-3)"
            strokeWidth="1.4"
          />
        ))}
      </g>

      <g className="best" data-anim="best">
        <line
          x1={TRACK_X0}
          y1={TRACK_YS[BEST]}
          x2={TRACK_X1}
          y2={TRACK_YS[BEST]}
          stroke="var(--color-accent)"
          strokeWidth="2"
        />
        <circle cx={TRACK_X1} cy={TRACK_YS[BEST]} r="3.4" fill="var(--color-accent)" />
      </g>

      <g className="improved" data-anim="improved">
        <path
          d="M228,88h6M232,85l5,3l-5,3"
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1.2"
        />
        <rect
          x="240"
          y="76"
          width="54"
          height="26"
          rx="4"
          fill="var(--color-raised)"
          stroke="var(--color-accent)"
          strokeWidth="1.1"
        />
        <text
          x="267"
          y="89"
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily={ART_MONO}
          fontSize="9"
          fill="var(--color-ink-1)"
        >
          improved
        </text>
      </g>

      <text x={TRACK_X0} y="150" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        15 shared futures, 50 moves each
      </text>

      <g className="tart-final" data-anim="loop">
        <path
          d="M267,74V32Q267,24 259,24H39Q31,24 31,32V72M27,66l4,6l4,-6"
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth="1.2"
        />
        <rect
          x="6"
          y="74"
          width="50"
          height="30"
          rx="5"
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth="1.2"
        />
        <text
          x="149"
          y="38"
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize="9"
          fill="var(--color-highlight)"
        >
          the new base
        </text>
      </g>
    </svg>
  );
}
