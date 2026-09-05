/**
 * Card art for `fair-expectimax/selective-depth`: an uneven tree. Every legal
 * column is searched at the root, so seven cones hang from it and none is cut
 * away; the ply rules behind them mark how far each one is taken. On play all
 * seven grow to the fourth ply together, two of them are ringed, and only
 * those two carry on past the depth-4 rule to a fifth move — the extra depth
 * is spent where a cheap ordering says it is worth it, and nowhere else.
 *
 * Server component. Motion lives in selective-depth.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./selective-depth.css";

/** The seven legal columns, spread across the frame. */
const COLUMN_X = Array.from({ length: 7 }, (_, i) => 62 + i * 39.5);
/** The two columns a cheap ordering puts in front: the only ones deepened. */
const DEEPENED = new Set([2, 5]);

const ROOT = { x: 180.5, y: 16, size: 15 };
/** Row of column nodes: the first of the search's own moves. */
const APEX_Y = 44;
/** Where the cones start, just under their node. */
const CONE_Y = 47;
const SHALLOW_BASE = 118;
const DEEP_BASE = 142;
const SHALLOW_HALF = 11;
const DEEP_HALF = (SHALLOW_HALF * (DEEP_BASE - CONE_Y)) / (SHALLOW_BASE - CONE_Y);

const RULE_X1 = 50;
const RULE_X2 = 314;
/** The two plies between the node row and the fourth move. */
const INNER_PLIES = [70, 94];

function cone(x: number, base: number, half: number): string {
  return `M${x},${CONE_Y}L${(x - half).toFixed(2)},${base}L${(x + half).toFixed(2)},${base}z`;
}

const EDGES = COLUMN_X.map((x) => `M${ROOT.x},${ROOT.y + ROOT.size}L${x},${APEX_Y - 3}`).join("");
const CHOSEN_EDGES = COLUMN_X.filter((_, i) => DEEPENED.has(i))
  .map((x) => `M${ROOT.x},${ROOT.y + ROOT.size}L${x},${APEX_Y - 3}`)
  .join("");
const INNER_RULES = INNER_PLIES.map((y) => `M${RULE_X1},${y}H${RULE_X2}`).join("");

export function SelectiveDepthArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-selective-depth",
        "Seven columns searched four moves deep, of which two are carried one move further",
        props,
      )}
    >
      <path d={INNER_RULES} fill="none" stroke="var(--color-ink-4)" strokeWidth="0.8" strokeDasharray="2 5" />
      <path
        d={`M${RULE_X1},${SHALLOW_BASE}H${RULE_X2}`}
        fill="none"
        stroke="var(--color-rule-strong)"
        strokeWidth="1"
        strokeDasharray="3 4"
      />
      <path d={EDGES} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.1" />
      <rect
        x={ROOT.x - ROOT.size / 2}
        y={ROOT.y}
        width={ROOT.size}
        height={ROOT.size}
        rx="3"
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth="1.2"
      />
      <g className="cones">
        {COLUMN_X.map((x, index) =>
          DEEPENED.has(index) ? (
            <path
              key={x}
              className="cone"
              data-anim="deepen"
              d={cone(x, DEEP_BASE, DEEP_HALF)}
              fill="var(--color-accent-soft)"
              stroke="var(--color-accent)"
              strokeWidth="1.2"
            />
          ) : (
            <path
              key={x}
              className="cone"
              data-anim="grow"
              d={cone(x, SHALLOW_BASE, SHALLOW_HALF)}
              fill="var(--color-ink-4)"
              fillOpacity="0.32"
              stroke="var(--color-ink-3)"
              strokeWidth="1"
            />
          ),
        )}
      </g>
      <g className="nodes" fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1.1">
        {COLUMN_X.map((x) => (
          <circle key={x} cx={x} cy={APEX_Y} r="3.4" />
        ))}
      </g>
      <g className="chosen" data-anim="chosen">
        <path d={CHOSEN_EDGES} fill="none" stroke="var(--color-accent)" strokeWidth="2" strokeLinecap="round" />
        {COLUMN_X.filter((_, index) => DEEPENED.has(index)).map((x) => (
          <circle key={x} cx={x} cy={APEX_Y} r="6" fill="none" stroke="var(--color-accent)" strokeWidth="1.4" />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)" textAnchor="end">
        <text x="46" y={APEX_Y + 3.5}>
          columns
        </text>
        <text x="46" y={SHALLOW_BASE + 3.5}>
          depth 4
        </text>
      </g>
      <g className="tart-final" data-anim="fifth">
        <path
          d={`M${RULE_X1},${DEEP_BASE}H${RULE_X2}`}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1"
          strokeDasharray="3 4"
        />
        <text x="46" y={DEEP_BASE + 3.5} fontFamily={ART_MONO} fontSize="9" fill="var(--color-accent)" textAnchor="end">
          depth 5
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="168" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          a fifth move on two columns, not seven
        </text>
      </g>
    </svg>
  );
}
