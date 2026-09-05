/**
 * Card art for `fair-expectimax/root-risk`: one column's seven sampled
 * outcomes hanging from the root, and the weight each one carries in the
 * column's value. The bars above show how far each future falls; the bars
 * below show its weight. On play the fan arrives, the worst 25% of the
 * outcomes is bracketed, and the weights redistribute — the two worst grow
 * and the other five shrink — so the resting frame is a value decided mostly
 * by its bad tail.
 *
 * Server component. Motion lives in root-risk.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./root-risk.css";

/** Centre x of each of the seven sampled outcomes, worst on the left. */
const COLS = [58, 92, 126, 160, 194, 228, 262];
const BAR_W = 16;

const ROOT = { x: 160, y: 19 };
const OUTCOME_TOP = 46;
/** How far each outcome falls: the two on the left are the bad tail. */
const OUTCOME_FALL = [46, 38, 20, 16, 12, 9, 6];

const WEIGHT_TOP = 106;
/**
 * The weight each outcome carries under `0.75 x mean + 0.25 x worst-25%`:
 * the worst outcome takes a quarter of the weight, the second worst rather
 * less, and the surviving five share what is left.
 */
const WEIGHT = [44, 38, 19, 19, 19, 19, 19];

const EDGES = COLS.map((x) => `M${ROOT.x},${ROOT.y + 7}L${x},${OUTCOME_TOP - 2}`).join("");

function weightAnim(index: number): string {
  if (index === 0) return "w-worst";
  if (index === 1) return "w-second";
  return "w-rest";
}

export function RootRiskArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-root-risk",
        "Seven sampled outcomes fanning from the root, with the worst two given extra weight in the column's value",
        props,
      )}
    >
      <g data-anim="fan">
        <path d={EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth={1} />
        <rect
          x={ROOT.x - 7}
          y={ROOT.y - 7}
          width={14}
          height={14}
          rx={3}
          fill="var(--color-raised)"
          stroke="var(--color-ink-2)"
          strokeWidth={1.2}
        />
      </g>

      <g data-anim="bars">
        {COLS.map((x, i) => (
          <rect
            key={x}
            x={x - BAR_W / 2}
            y={OUTCOME_TOP}
            width={BAR_W}
            height={OUTCOME_FALL[i]}
            rx={2}
            fill={i < 2 ? "var(--color-danger)" : "var(--color-ink-3)"}
          />
        ))}
      </g>

      <line x1={44} y1={WEIGHT_TOP} x2={276} y2={WEIGHT_TOP} stroke="var(--color-rule-strong)" strokeWidth={1} />
      {COLS.map((x, i) => (
        <rect
          key={x}
          data-anim={weightAnim(i)}
          x={x - BAR_W / 2}
          y={WEIGHT_TOP}
          width={BAR_W}
          height={WEIGHT[i]}
          rx={2}
          fill={i < 2 ? "var(--color-danger)" : "var(--color-ink-4)"}
        />
      ))}

      <g data-anim="mark">
        <path d="M46,152v5h62v-5" fill="none" stroke="var(--color-danger)" strokeWidth={1.4} />
        <text x={114} y={161} fontFamily={ART_MONO} fontSize={9} fill="var(--color-danger)">
          worst 25%
        </text>
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={146} y={23} textAnchor="end">
          root
        </text>
        <text x={8} y={40}>
          outcomes
        </text>
        <text x={8} y={112}>
          weight
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={174} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the worst outcomes weigh more
        </text>
      </g>
    </svg>
  );
}
