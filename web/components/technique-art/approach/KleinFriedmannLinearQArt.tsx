/**
 * Card art for `value-policy-learning/klein-friedmann-linear-q`: six weights
 * and how they were found. On play the six dials twitch while the step-size
 * arrow shrinks away — the 1/t schedule spending itself — and then a search
 * cursor jumps the dials straight to new settings and the games get longer.
 *
 * Server component. Motion lives in klein-friedmann-linear-q.css (transform
 * and opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./klein-friedmann-linear-q.css";

const DIAL_R = 16;
const DIAL_Y = 74;
const DIAL_X = [30, 68, 106, 144, 182, 220];
const ARROW_X = 252;
/**
 * Where the search leaves each dial; the resting frame shows those settings.
 * The rotations are written without a centre because the stylesheet gives the
 * dials `transform-box: fill-box; transform-origin: center`, which the
 * transform attribute obeys too.
 */
const FINAL_ANGLE = [-58, 74, -104, 38, 126, -82];
const BAR_X = 296;
const BAR_TOP = 40;
const BAR_BOTTOM = 132;

/** Four corner brackets around the first dial; the cursor jumps along the row. */
const CURSOR = ((left: number, right: number, top: number, bottom: number) =>
  [
    `M${left},${top}h8m-8,0v8`,
    `M${left},${bottom}h8m-8,0v-8`,
    `M${right},${top}h-8m8,0v8`,
    `M${right},${bottom}h-8m8,0v-8`,
  ].join(""))(DIAL_X[0] - 22, DIAL_X[0] + 22, DIAL_Y - 22, DIAL_Y + 22);

export function KleinFriedmannLinearQArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-klein-friedmann-linear-q", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Six weight dials twitch while the learning step shrinks to nothing, then a search sets them directly and the games run longer"
      }
    >
      {DIAL_X.map((cx, index) => (
        <g
          key={cx}
          className={`dial dial-${index}`}
          data-anim={`dial-${index}`}
          transform={`rotate(${FINAL_ANGLE[index]})`}
        >
          <circle cx={cx} cy={DIAL_Y} r={DIAL_R} fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1.2" />
          <line
            x1={cx}
            y1={DIAL_Y}
            x2={cx}
            y2={DIAL_Y - DIAL_R + 4}
            stroke="var(--color-accent)"
            strokeWidth="2.2"
            strokeLinecap="round"
          />
        </g>
      ))}
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)" textAnchor="middle">
        {DIAL_X.map((cx, index) => (
          <text key={cx} x={cx} y={DIAL_Y + 30}>
            w{index + 1}
          </text>
        ))}
      </g>
      <g className="step" data-anim="step" transform="scale(1 0.04)">
        <path
          d={`M${ARROW_X},110V52m-5,8l5,-8l5,8`}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth="2"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>
      <text x={ARROW_X} y="124" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-series-2)">
        step
      </text>
      <g className="cursor" data-anim="cursor" opacity="0">
        <path
          d={CURSOR}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth="1.6"
          strokeLinecap="round"
        />
      </g>
      <rect
        x={BAR_X - 7}
        y={BAR_TOP}
        width="14"
        height={BAR_BOTTOM - BAR_TOP}
        rx="2"
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
        strokeWidth="0.8"
      />
      <line
        className="moves"
        data-anim="moves"
        x1={BAR_X}
        y1={BAR_BOTTOM}
        x2={BAR_X}
        y2={BAR_TOP}
        stroke="var(--color-accent-strong)"
        strokeWidth="14"
        strokeDasharray={BAR_BOTTOM - BAR_TOP}
        strokeDashoffset="26"
      />
      <text x={BAR_X} y="146" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        moves
      </text>
      <g className="caption-a" data-anim="caption-a" opacity="0">
        <text x="10" y="24" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          learning by 1/t: the step spends itself
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x="10" y="24" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          search sets the six weights directly
        </text>
      </g>
    </svg>
  );
}
