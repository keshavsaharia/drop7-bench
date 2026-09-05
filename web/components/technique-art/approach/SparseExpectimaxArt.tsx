/**
 * Card art for `heuristic-search/sparse-expectimax`: the chance layer is
 * sampled, not enumerated. One chance node opens a fan to every outcome the
 * game could deal; on play the fan drops back to a ghost and five stratified
 * edges take its place, and the work that buys pays for depth — each surviving
 * stratum runs on through two more plies to a leaf. The resting frame is the
 * five strata, the plies they reach, and the fan they stand in for.
 *
 * Server component. Motion lives in sparse-expectimax.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./sparse-expectimax.css";

const CHANCE = { x: 160, y: 26, r: 10 };
const FAN_Y = 88;
const FAN_X1 = 34;
const FAN_X2 = 296;
/** Every outcome the chance node could take, drawn and then not evaluated. */
const OUTCOMES = 49;
/** The strata the search actually evaluates, one per equal slice of the fan. */
const STRATA = 5;

const OUTCOME_X = Array.from({ length: OUTCOMES }, (_, i) => FAN_X1 + (i * (FAN_X2 - FAN_X1)) / (OUTCOMES - 1));
const STRATUM_X = Array.from(
  { length: STRATA },
  (_, i) => FAN_X1 + ((i + 0.5) * (FAN_X2 - FAN_X1)) / STRATA,
);

const TRUNK_TOP = 94;
const TRUNK_BOTTOM = 146;
const PLY_Y = [110, 128];

const FAN = OUTCOME_X.map((x) => `M${CHANCE.x},${CHANCE.y + CHANCE.r}L${x.toFixed(2)},${FAN_Y}`).join("");
const SAMPLED = STRATUM_X.map((x) => `M${CHANCE.x},${CHANCE.y + CHANCE.r}L${x.toFixed(2)},${FAN_Y}`).join("");
const TRUNKS = STRATUM_X.map((x) => `M${x.toFixed(2)},${TRUNK_TOP}V${TRUNK_BOTTOM}`).join("");
const PLIES = STRATUM_X.flatMap((x) => PLY_Y.map((y) => `M${(x - 4.5).toFixed(2)},${y}h9`)).join("");

export function SparseExpectimaxArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-sparse-expectimax",
        "A chance node's fan of every outcome fading behind five stratified samples, each carried two plies deeper",
        props,
      )}
    >
      <path
        className="fan"
        data-anim="fan"
        d={FAN}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth="0.7"
        opacity="0.26"
      />
      <circle
        cx={CHANCE.x}
        cy={CHANCE.y}
        r={CHANCE.r}
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth="1.3"
      />
      <g className="strata" data-anim="strata">
        <path d={SAMPLED} fill="none" stroke="var(--color-accent)" strokeWidth="1.8" />
        {STRATUM_X.map((x) => (
          <circle
            key={x}
            cx={x}
            cy={FAN_Y + 2}
            r="4"
            fill="var(--color-accent-strong)"
            stroke="var(--color-accent)"
            strokeWidth="1"
          />
        ))}
      </g>
      <g className="trunks" data-anim="trunks">
        <path d={TRUNKS} fill="none" stroke="var(--color-accent)" strokeWidth="2.2" strokeLinecap="round" />
        <path d={PLIES} fill="none" stroke="var(--color-accent)" strokeWidth="1.2" opacity="0.7" />
      </g>
      <g className="leaves" data-anim="leaves" fill="var(--color-raised)" stroke="var(--color-accent)" strokeWidth="1.1">
        {STRATUM_X.map((x) => (
          <rect key={x} x={x - 7} y={TRUNK_BOTTOM} width="14" height="12" rx="2" />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="20">
          every outcome
        </text>
        <text x="50" y="104" textAnchor="end">
          5 strata
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          a few outcomes, spent on depth
        </text>
      </g>
    </svg>
  );
}
