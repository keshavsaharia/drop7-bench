/**
 * Card art for risk and survival: a score staircase rising one 17k step at a
 * time and a board-height line ratcheting up toward the top rule. Both draw
 * left to right; when the height line touches the rule the staircase stops
 * and a cross marks the end of the game.
 *
 * Server component. Motion lives in risk-survival.css (stroke-dashoffset,
 * transform and opacity only); the markup is the finished frame.
 */
import "./risk-survival.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

/** Eight equal steps from the origin to (222, 86). */
const STAIRCASE = "M30,150 " + "h24 v-8 ".repeat(8).trim();
/** A ratchet from (30, 130) that reaches the top rule at (222, 44). */
const HEIGHT = "M30,130 h36 v-14 h30 v-12 h34 v-16 h30 v-12 h28 v-14 h34 v-18";

export function RiskSurvivalArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--risk-survival", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Risk and survival: the score climbs in steps until the board height reaches the top row"}
    >
      <g className="frame">
        <path d="M30,30 V150 H304" fill="none" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <line x1="30" y1="44" x2="304" y2="44" stroke="var(--color-series-7)" strokeWidth="1" strokeDasharray="4 3" opacity="0.8" />
      </g>
      <g className="series">
        <path
          className="score"
          data-anim="draw"
          d={STAIRCASE}
          pathLength={1}
          strokeDasharray="1"
          fill="none"
          stroke="var(--color-series-1)"
          strokeWidth="2"
        />
        <path
          className="height"
          data-anim="draw"
          d={HEIGHT}
          pathLength={1}
          strokeDasharray="1"
          fill="none"
          stroke="var(--color-series-7)"
          strokeWidth="2"
        />
        <circle className="end" data-anim="end" cx="222" cy="86" r="3" fill="var(--color-series-1)" />
        <g transform="translate(222 44)">
          <path
            className="hit"
            data-anim="hit"
            d="M-5,-5 l10,10 M5,-5 l-10,10"
            stroke="var(--color-series-7)"
            strokeWidth="2"
            strokeLinecap="round"
          />
        </g>
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="34" y="40">top row</text>
        <text x="136" y="70">height</text>
        <text x="228" y="90">score</text>
        <text x="34" y="164">+17k a step</text>
      </g>
      <text className="over" data-anim="over" x="228" y="36" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
        game over
      </text>
      <g className="tart-final">
        <text x="304" y="172" textAnchor="end" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          the board, not the score, ends the game
        </text>
      </g>
    </svg>
  );
}
