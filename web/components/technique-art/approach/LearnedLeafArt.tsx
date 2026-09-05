/**
 * Card art for `lifetime-objective/learned-leaf`: the leaf of the frozen
 * depth-4 search is half the hand-made formula and half a learned survival
 * estimate. On play each leaf's formula glyph slides half out of its box and
 * an hourglass — how long the game has left — slides half in, and the root
 * value moves because of it.
 *
 * The sliding glyphs live in a nested <svg>, whose viewport clips them, so
 * the art needs no clip-path id. Server component; motion lives in
 * learned-leaf.css (transform and opacity only).
 */
import type { ArtProps } from "../registry";
import "./learned-leaf.css";

const ROOT = { x: 160, y: 30 };
const LEAF_W = 64;
const LEAF_H = 34;
const LEAF_Y = 106;
const LEAVES = [20, 128, 236];

const EDGES = LEAVES.map((x) => `M${ROOT.x},${ROOT.y + 9}L${x + LEAF_W / 2},${LEAF_Y}`).join("");
/**
 * Both glyphs are drawn in the nested viewport's own coordinates, centred on
 * (31, 16), so the only transform on them is the animated slide.
 */
/** Three stacked rules: the hand-made formula's terms. */
const FORMULA = "M15,10h22M15,16h15M15,22h19";
/** An hourglass: two triangles meeting at a waist. */
const HOURGLASS = "M22,6h18M22,26h18M24,7h14l-7,9l7,9h-14l7,-9z";

export function LearnedLeafArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-learned-leaf", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Each leaf of the search is half the hand-made formula and half a learned estimate of how long the game lasts"
      }
    >
      <path d={EDGES} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.2" />
      <rect
        x={ROOT.x - 9}
        y={ROOT.y - 9}
        width="18"
        height="18"
        rx="3"
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth="1.2"
      />
      <g className="value-a" data-anim="value-a" opacity="0">
        <text x={ROOT.x + 16} y={ROOT.y + 3} fontFamily="var(--font-mono)" fontSize="11" fill="var(--color-ink-2)">
          v = 41
        </text>
      </g>
      <g className="value-b" data-anim="value-b">
        <text x={ROOT.x + 16} y={ROOT.y + 3} fontFamily="var(--font-mono)" fontSize="11" fill="var(--color-accent)">
          v = 58
        </text>
      </g>
      <g className="leaves" fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1">
        {LEAVES.map((x) => (
          <rect key={x} x={x} y={LEAF_Y} width={LEAF_W} height={LEAF_H} rx="4" />
        ))}
      </g>
      {LEAVES.map((x) => (
        <svg key={x} x={x + 1} y={LEAF_Y + 1} width={LEAF_W - 2} height={LEAF_H - 2}>
          <g className="formula" data-anim="formula" transform="translate(-15 0)">
            <path d={FORMULA} fill="none" stroke="var(--color-ink-2)" strokeWidth="2" strokeLinecap="round" />
          </g>
          <g className="glass" data-anim="glass" transform="translate(15 0)">
            <path d={HOURGLASS} fill="none" stroke="var(--color-accent)" strokeWidth="1.6" strokeLinejoin="round" />
          </g>
        </svg>
      ))}
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="20" y="98">
          leaves
        </text>
        <text x="8" y="30">
          root
        </text>
      </g>
      <g className="caption-a" data-anim="caption-a" opacity="0">
        <text x="160" y="162" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          hand-made leaf
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x="160" y="162" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          half formula, half learned survival
        </text>
      </g>
    </svg>
  );
}
