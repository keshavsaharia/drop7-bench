/**
 * Card art for `terminal-policy-iteration/terminal-policy-iteration`: three
 * candidate moves, each replayed down hundreds of shared futures to a far
 * horizon, and the confidence intervals that come back. The rule wants an
 * interval clear of zero on both the score axis and the moves axis before it
 * will change the reference's move. On play the fans race out, the whiskers
 * form across zero, and the lock stays on.
 *
 * Server component. Motion lives in terminal-policy-iteration.css (transform
 * and opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./terminal-policy-iteration.css";

const STUB_X = 10;
const STUB_W = 24;
const STUB_YS = [44, 90, 136];
const FAN_END = 142;
const HORIZON = 148;
const ZERO_X = 244;
const PANELS = [
  { label: "score", ys: [42, 56, 70] },
  { label: "moves", ys: [112, 126, 140] },
];
/** Each interval as [left, right] offsets from the zero line: all straddle it. */
const WHISKERS: ReadonlyArray<readonly [number, number]> = [
  [-26, 18],
  [-14, 30],
  [-30, 10],
];

function fan(y: number): string {
  return Array.from({ length: 13 }, (_, i) => {
    const spread = (i - 6) * 3.4;
    return `M${STUB_X + STUB_W},${y}L${FAN_END},${y + spread}`;
  }).join("");
}

export function TerminalPolicyIterationArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-terminal-policy-iteration", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Three candidates replayed to a far horizon return intervals that all straddle zero, so the reference move is locked in"
      }
    >
      {STUB_YS.map((y, index) => (
        <g key={y}>
          <rect
            x={STUB_X}
            y={y - 8}
            width={STUB_W}
            height="16"
            rx="3"
            fill="var(--color-raised)"
            stroke="var(--color-ink-3)"
            strokeWidth="1.1"
          />
          <path
            className={`fan fan-${index}`}
            data-anim={`fan-${index}`}
            d={fan(y)}
            fill="none"
            stroke="var(--color-ink-4)"
            strokeWidth="0.7"
          />
        </g>
      ))}
      <line x1={HORIZON} y1="30" x2={HORIZON} y2="152" stroke="var(--color-rule-strong)" strokeWidth="1" strokeDasharray="3 4" />
      {PANELS.map((panel) => (
        <g key={panel.label}>
          <line
            x1={ZERO_X}
            y1={panel.ys[0] - 12}
            x2={ZERO_X}
            y2={panel.ys[2] + 12}
            stroke="var(--color-highlight)"
            strokeWidth="1"
            strokeDasharray="3 3"
          />
          <text
            x="170"
            y={panel.ys[0] - 14}
            fontFamily="var(--font-mono)"
            fontSize="9"
            fill="var(--color-ink-3)"
          >
            {panel.label}
          </text>
          <g className="whiskers" data-anim="whiskers">
            {panel.ys.map((y, index) => (
              <g key={y}>
                <line
                  x1={ZERO_X + WHISKERS[index][0]}
                  y1={y}
                  x2={ZERO_X + WHISKERS[index][1]}
                  y2={y}
                  stroke="var(--color-ink-2)"
                  strokeWidth="1.4"
                />
                <circle
                  cx={ZERO_X + (WHISKERS[index][0] + WHISKERS[index][1]) / 2}
                  cy={y}
                  r="2.4"
                  fill="var(--color-accent)"
                />
              </g>
            ))}
          </g>
        </g>
      ))}
      <text x={ZERO_X} y="94" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-highlight)">
        0
      </text>
      <text x="10" y="24" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        3 candidates
      </text>
      <text x={HORIZON - 4} y="166" textAnchor="end" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        200 moves of shared future
      </text>
      <g className="tart-final" data-anim="lock">
        <rect x="288" y="86" width="18" height="14" rx="2" fill="var(--color-ink-2)" />
        <path d="M291,86v-4a6,6 0 0,1 12,0v4" fill="none" stroke="var(--color-ink-2)" strokeWidth="1.6" />
        <text x="297" y="112" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          kept
        </text>
      </g>
    </svg>
  );
}
