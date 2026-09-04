/**
 * Card art for `heuristic-search/risk-sensitive`: the same scenarios scored by
 * a different statistic. Two candidate columns are run through one shared set
 * of sampled outcomes, worse further down. Their plain averages land on the
 * same line, so an ordinary search cannot separate them; the average of the
 * worst quarter can, and the column whose tail stays shallow is played.
 *
 * Server component. Motion lives in risk-sensitive.css (transform and opacity
 * only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./risk-sensitive.css";

/**
 * Sampled outcomes for the two columns, as heights in the frame: lower on the
 * card is a worse outcome. The two sets are built to share one average, so the
 * mean markers are drawn on the same line rather than asserted to be equal.
 */
const WIDE = [36, 46, 62, 76, 92, 106, 136, 150];
const TIGHT = [60, 68, 76, 84, 92, 100, 108, 116];
const WIDE_X = 100;
const TIGHT_X = 216;

function mean(ys: readonly number[]): number {
  return ys.reduce((sum, y) => sum + y, 0) / ys.length;
}

/** The worst quarter of a set of outcomes, worst first. */
function worstQuarter(ys: readonly number[]): number[] {
  return [...ys].sort((a, b) => b - a).slice(0, Math.round(ys.length / 4));
}

const MEAN_Y = mean(WIDE);

/** One column's scenarios, offset left and right so none overlaps. */
function Scenarios({ ys, cx }: { ys: readonly number[]; cx: number }) {
  return (
    <g data-anim="fan">
      {ys.map((y, index) => (
        <circle key={y} cx={cx + (index % 2 ? 6 : -6)} cy={y} r={4.5} fill="var(--color-ink-3)" />
      ))}
    </g>
  );
}

/** The bracket around a column's worst quarter and the average inside it. */
function Tail({ ys, cx, colour }: { ys: readonly number[]; cx: number; colour: string }) {
  const worst = worstQuarter(ys);
  const top = Math.min(...worst) - 6;
  const bottom = Math.max(...worst) + 6;
  const bracket = cx + 19;
  return (
    <g className="tart-final" data-anim="tail">
      <path
        d={`M${bracket - 4},${top}h4v${bottom - top}h-4`}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth={1.4}
      />
      <line
        x1={cx - 18}
        y1={mean(worst)}
        x2={bracket}
        y2={mean(worst)}
        stroke={colour}
        strokeWidth={3}
        strokeLinecap="round"
      />
    </g>
  );
}

/** The plain average of every scenario, identical for both columns. */
function Average({ cx }: { cx: number }) {
  return (
    <g className="tart-final" data-anim="mid">
      <line
        x1={cx - 15}
        y1={MEAN_Y}
        x2={cx + 15}
        y2={MEAN_Y}
        stroke="var(--color-ink-1)"
        strokeWidth={3.5}
        strokeLinecap="round"
      />
    </g>
  );
}

export function RiskSensitiveArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-risk-sensitive",
        "Two columns whose sampled outcomes share one average; the column with the shallower worst quarter is played",
        props,
      )}
    >
      <line
        x1={60}
        y1={MEAN_Y}
        x2={258}
        y2={MEAN_Y}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        strokeDasharray="4 3"
      />
      <g data-anim="dim" opacity={0.45}>
        <Scenarios ys={WIDE} cx={WIDE_X} />
        <Average cx={WIDE_X} />
        <Tail ys={WIDE} cx={WIDE_X} colour="var(--color-series-2)" />
      </g>
      <g>
        <Scenarios ys={TIGHT} cx={TIGHT_X} />
        <Average cx={TIGHT_X} />
        <Tail ys={TIGHT} cx={TIGHT_X} colour="var(--color-accent)" />
      </g>
      <g className="tart-final" data-anim="pick">
        <rect
          x={193}
          y={48}
          width={50}
          height={80}
          rx={6}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.6}
        />
        <text x={TIGHT_X} y={146} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
          played
        </text>
      </g>
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={WIDE_X} y={22} textAnchor="middle">
          col 2
        </text>
        <text x={TIGHT_X} y={22} textAnchor="middle">
          col 5
        </text>
        <text x={8} y={MEAN_Y - 3}>
          mean
        </text>
        <text x={8} y={146}>
          worst quarter
        </text>
      </g>
    </svg>
  );
}
