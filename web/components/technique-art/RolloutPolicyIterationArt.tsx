/**
 * Card art for rollouts and policy iteration: three candidate moves from one
 * root, five thin rollouts racing right from each (some end early with a
 * cross), three bars filling to their mean returns, and a check only for a
 * bar that clears the dashed acceptance line.
 *
 * Server component. Motion lives in rollout-policy-iteration.css (stroke-
 * dashoffset, transform and opacity only); the markup is the resting frame.
 */
import "./rollout-policy-iteration.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const LINE_START = 56;
const LINE_END = 196;
const BAR_X = 214;
const BAR_WIDTH = 84;
const THRESHOLD = 0.7;

interface Candidate {
  y: number;
  color: string;
  /** Where each rollout stops; anything short of LINE_END ended the game early. */
  ends: readonly number[];
  /** Mean return as a fraction of the bar width. */
  mean: number;
}

const CANDIDATES: readonly Candidate[] = [
  { y: 44, color: "var(--color-series-1)", ends: [196, 118, 196, 196, 196], mean: 0.44 },
  { y: 90, color: "var(--color-series-2)", ends: [196, 196, 196, 128, 196], mean: 0.83 },
  { y: 136, color: "var(--color-series-3)", ends: [104, 196, 196, 196, 150], mean: 0.56 },
];

const OFFSETS = [-8, -4, 0, 4, 8] as const;

export function RolloutPolicyIterationArt({ mode = "hover", title, className }: ArtProps) {
  const thresholdX = BAR_X + BAR_WIDTH * THRESHOLD;
  return (
    <svg
      className={["tart", "tart--rollout-policy-iteration", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ??
        "Rollouts and policy iteration: three candidates, many rollouts each, mean returns, and a check only past the acceptance line"
      }
    >
      <g className="candidates">
        <path
          d="M16,90 L46,44 M16,90 L46,90 M16,90 L46,136"
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
        />
        <circle cx="16" cy="90" r="4" fill="var(--color-ink-2)" />
        {CANDIDATES.map((candidate) => (
          <circle key={candidate.y} cx="46" cy={candidate.y} r="4.5" fill={candidate.color} />
        ))}
      </g>
      <g className="rollouts">
        {CANDIDATES.map((candidate) =>
          candidate.ends.map((end, index) => {
            const y = candidate.y + OFFSETS[index];
            const early = end < LINE_END;
            return (
              <line
                key={`${candidate.y}-${index}`}
                className={early ? "run run-early" : "run run-full"}
                data-anim="run"
                x1={LINE_START}
                y1={y}
                x2={end}
                y2={y}
                pathLength={1}
                strokeDasharray="1"
                stroke={candidate.color}
                strokeWidth="1"
                opacity="0.75"
              />
            );
          }),
        )}
      </g>
      <g className="stops">
        {CANDIDATES.map((candidate) =>
          candidate.ends.map((end, index) =>
            end < LINE_END ? (
              <path
                key={`${candidate.y}-${index}`}
                className="stop"
                data-anim="stop"
                d="M-3,-3 l6,6 M3,-3 l-6,6"
                transform={`translate(${end + 4} ${candidate.y + OFFSETS[index]})`}
                stroke="var(--color-series-7)"
                strokeWidth="1.3"
                fill="none"
              />
            ) : null,
          ),
        )}
      </g>
      <g className="means">
        {CANDIDATES.map((candidate) => (
          <rect
            key={`track-${candidate.y}`}
            x={BAR_X}
            y={candidate.y - 5}
            width={BAR_WIDTH}
            height="10"
            rx="2"
            fill="var(--color-cell)"
            stroke="var(--color-rule-strong)"
            strokeWidth="1"
          />
        ))}
        {CANDIDATES.map((candidate) => (
          <rect
            key={`fill-${candidate.y}`}
            className="bar-fill"
            data-anim="bar"
            x={BAR_X}
            y={candidate.y - 5}
            width={BAR_WIDTH * candidate.mean}
            height="10"
            rx="2"
            fill={candidate.color}
          />
        ))}
        <line
          x1={thresholdX}
          y1="30"
          x2={thresholdX}
          y2="150"
          stroke="var(--color-ink-2)"
          strokeWidth="1"
          strokeDasharray="3 3"
        />
        <path
          className="check"
          data-anim="check"
          d="M300,90 l3,3 l6,-7"
          fill="none"
          stroke="var(--color-status-completed)"
          strokeWidth="2"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="56" y="26">rollouts</text>
        <text x="214" y="26">mean return</text>
      </g>
      <g className="tart-final">
        <text x="16" y="172" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          only a mean past the line earns a check
        </text>
      </g>
    </svg>
  );
}
