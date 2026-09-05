/**
 * Card art for `fair-expectimax/chance-strata`: the seven values the game can
 * deal, each with the weight the search gives it. On play five weights fill
 * and two stay at zero — the reference's five samples — then all seven fill
 * to the same height, which is the change this experiment tested, and the
 * cycle returns to the five the reference actually draws, which is also the
 * resting frame.
 *
 * Server component. Motion lives in chance-strata.css (stroke-dashoffset and
 * opacity only); the markup is the resting frame.
 */
import { CellGlyph } from "@/components/discs";
import type { ArtProps } from "../registry";
import "./chance-strata.css";

const VALUES = [1, 2, 3, 4, 5, 6, 7];
const GLYPH = 24;
const X0 = 16;
const STEP = 43;
const GLYPH_Y = 40;
const BAR_BOTTOM = 138;
const BAR_TOP = 82;
/** The two values the five-sample reference happens to miss in this frame. */
const MISSED = new Set([5, 7]);

function centre(index: number): number {
  return X0 + index * STEP + GLYPH / 2;
}

export function ChanceStrataArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-chance-strata", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Seven possible next discs: five carry a sampled weight and two carry none, until all seven are weighted alike"
      }
    >
      <g className="discs">
        {VALUES.map((value, index) => (
          <CellGlyph key={value} cell={value} x={X0 + index * STEP} y={GLYPH_Y} s={GLYPH} />
        ))}
      </g>
      <g className="tracks" fill="var(--color-cell)" stroke="var(--color-rule)" strokeWidth="0.8">
        {VALUES.map((value, index) => (
          <rect key={value} x={centre(index) - 6} y={BAR_TOP} width="12" height={BAR_BOTTOM - BAR_TOP} rx="2" />
        ))}
      </g>
      <g
        className="sampled"
        data-anim="sampled"
        stroke="var(--color-accent-strong)"
        strokeWidth="12"
        strokeDasharray={BAR_BOTTOM - BAR_TOP}
      >
        {VALUES.filter((value) => !MISSED.has(value)).map((value) => (
          <line
            key={value}
            x1={centre(VALUES.indexOf(value))}
            y1={BAR_BOTTOM}
            x2={centre(VALUES.indexOf(value))}
            y2={BAR_TOP}
          />
        ))}
      </g>
      <g
        className="missed"
        data-anim="missed"
        stroke="var(--color-accent-strong)"
        strokeWidth="12"
        strokeDasharray={BAR_BOTTOM - BAR_TOP}
        strokeDashoffset={BAR_BOTTOM - BAR_TOP}
      >
        {VALUES.filter((value) => MISSED.has(value)).map((value) => (
          <line
            key={value}
            x1={centre(VALUES.indexOf(value))}
            y1={BAR_BOTTOM}
            x2={centre(VALUES.indexOf(value))}
            y2={BAR_TOP}
          />
        ))}
      </g>
      <g
        className="zeros"
        data-anim="zeros"
        fontFamily="var(--font-mono)"
        fontSize="11"
        fill="var(--color-ink-2)"
        textAnchor="middle"
      >
        {VALUES.filter((value) => MISSED.has(value)).map((value) => (
          <text key={value} x={centre(VALUES.indexOf(value))} y={BAR_BOTTOM - 8}>
            0
          </text>
        ))}
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="30">
          weight on each next disc
        </text>
      </g>
      <g className="caption-b" data-anim="caption-b" opacity="0">
        <text x="160" y="160" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-accent)">
          all seven, each weighted 1/7
        </text>
      </g>
      <g className="tart-final" data-anim="caption-a">
        <text x="160" y="160" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          five samples, two values unweighted
        </text>
      </g>
    </svg>
  );
}
