/**
 * Card art for `d4-long-outcome/rollout-veto`: the search sees four moves,
 * the rollout replays twenty-five and crosses five rises. On play the long
 * bar draws itself across those rises and its margin badge travels to the
 * gate — and stops short of the threshold, so the gate stays shut and the
 * depth-4 move is kept. The veto only fires when the margin clears the mark.
 *
 * Server component. Motion lives in rollout-veto.css (transform, opacity and
 * stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./rollout-veto.css";

const BAR_X = 44;
const SHORT_END = 104;
const LONG_END = 240;
const SHORT_Y = 68;
const LONG_Y = 110;
const RISES = [96, 130, 164, 198, 232];
const GATE_X = 256;
const THRESHOLD_X = 248;

const RISE_LINES = RISES.map((x) => `M${x},44v96`).join("");

export function RolloutVetoArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-rollout-veto", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "A four-move search bar and a twenty-five-move rollout bar crossing five rises, and a gate that stays shut"
      }
    >
      <path d={RISE_LINES} fill="none" stroke="var(--color-rule-strong)" strokeWidth="1" strokeDasharray="3 4" />
      <line
        x1={BAR_X}
        y1={SHORT_Y}
        x2={SHORT_END}
        y2={SHORT_Y}
        stroke="var(--color-ink-3)"
        strokeWidth="8"
        strokeLinecap="round"
      />
      <line
        className="long"
        data-anim="long"
        x1={BAR_X}
        y1={LONG_Y}
        x2={LONG_END}
        y2={LONG_Y}
        stroke="var(--color-accent-strong)"
        strokeWidth="8"
        strokeLinecap="round"
        strokeDasharray={LONG_END - BAR_X}
      />
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x={BAR_X} y={SHORT_Y - 10}>
          4 moves: the search
        </text>
        <text x={BAR_X} y={LONG_Y - 10}>
          25 moves: the rollout
        </text>
        <text x="96" y="154" textAnchor="middle">
          rises
        </text>
      </g>
      <g className="gate" stroke="var(--color-ink-2)" strokeWidth="1.4" fill="var(--color-raised)">
        <rect className="gate-top" data-anim="gate-top" x={GATE_X} y="82" width="24" height="28" rx="2" />
        <rect className="gate-bottom" data-anim="gate-bottom" x={GATE_X} y="110" width="24" height="28" rx="2" />
      </g>
      <line
        x1={THRESHOLD_X}
        y1="80"
        x2={THRESHOLD_X}
        y2="140"
        stroke="var(--color-highlight)"
        strokeWidth="1.2"
        strokeDasharray="3 3"
      />
      <g className="badge" data-anim="badge" transform="translate(60 0)">
        <rect x="150" y="122" width="34" height="14" rx="3" fill="var(--color-accent)" />
        <text
          x="167"
          y="132"
          textAnchor="middle"
          fontFamily="var(--font-mono)"
          fontSize="9"
          fill="var(--color-accent-fg)"
        >
          +Δ
        </text>
      </g>
      <text x={THRESHOLD_X + 3} y="74" textAnchor="end" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-highlight)">
        threshold
      </text>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          margin short of the mark: the search keeps its move
        </text>
      </g>
    </svg>
  );
}
