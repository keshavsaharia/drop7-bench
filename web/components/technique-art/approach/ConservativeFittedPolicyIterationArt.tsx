/**
 * Card art for `value-policy-learning/conservative-fitted-policy-iteration`:
 * the constrained step. The fitted values propose a long move away from the
 * fallback policy; four ensemble members then disagree about how far the
 * improvement really reaches, and the step is cut back to the lowest of them,
 * which is the only part the data supports. The floor beside the fallback is
 * the margin an alternative must clear before it may be played at all.
 *
 * Server component. Motion lives in conservative-fitted-policy-iteration.css
 * (transform and opacity only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./conservative-fitted-policy-iteration.css";

const AXIS_Y = 100;
/** The fallback policy: a fixed search, and where every step starts. */
const START_X = 64;
/** Where the four members put the improvement; the lowest of them is the bound. */
const MEMBERS = [132, 176, 198, 246];
const BOUND_X = MEMBERS[0];
/** How far the unconstrained fitted step would go. */
const GREEDY_X = 276;
const BAR_H = 12;

export function ConservativeFittedPolicyIterationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-conservative-fitted-policy-iteration",
        "A policy step proposed far beyond what four ensemble members support, cut back to their lower bound",
        props,
      )}
    >
      <line x1={START_X} y1={AXIS_Y} x2={304} y2={AXIS_Y} stroke="var(--color-rule)" strokeWidth={1} />
      <rect x={START_X} y={86} width={32} height={28} fill="var(--color-accent-soft)" />
      <line x1={96} y1={84} x2={96} y2={116} stroke="var(--color-ink-4)" strokeWidth={1} strokeDasharray="3 3" />
      <path d={`M${START_X},116v6h32v-6`} fill="none" stroke="var(--color-ink-3)" strokeWidth={1.2} />
      <rect
        x={44}
        y={90}
        width={20}
        height={20}
        rx={3}
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth={1.2}
      />
      <g className="tart-final" data-anim="refused">
        <line
          x1={BOUND_X + 8}
          y1={AXIS_Y}
          x2={GREEDY_X - 8}
          y2={AXIS_Y}
          stroke="var(--color-ink-4)"
          strokeWidth={1.4}
          strokeDasharray="4 4"
        />
        <circle cx={GREEDY_X} cy={AXIS_Y} r={5.5} fill="none" stroke="var(--color-ink-4)" strokeWidth={1.4} />
        <text x={GREEDY_X} y={134} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
          refused
        </text>
      </g>
      <g className="tart-final" data-anim="bound">
        <line
          x1={BOUND_X}
          y1={64}
          x2={BOUND_X}
          y2={134}
          stroke="var(--color-highlight)"
          strokeWidth={1.2}
          strokeDasharray="3 3"
        />
        <text x={BOUND_X} y={58} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-highlight)">
          lower bound
        </text>
      </g>
      <rect
        data-anim="step"
        x={START_X}
        y={AXIS_Y - BAR_H / 2}
        width={BOUND_X - START_X}
        height={BAR_H}
        rx={BAR_H / 2}
        fill="var(--color-accent-strong)"
      />
      <circle data-anim="head" cx={BOUND_X} cy={AXIS_Y} r={6} fill="var(--color-accent)" />
      <g className="tart-final" data-anim="members">
        {MEMBERS.map((x) => (
          <g key={x}>
            <line x1={x} y1={82} x2={x} y2={92} stroke="var(--color-ink-4)" strokeWidth={1} />
            <circle cx={x} cy={78} r={3.5} fill="var(--color-ink-3)" />
          </g>
        ))}
        <text x={212} y={58} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
          four members
        </text>
      </g>
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={54} y={82} textAnchor="middle">
          fallback
        </text>
        <text x={80} y={134} textAnchor="middle">
          floor
        </text>
      </g>
    </svg>
  );
}
