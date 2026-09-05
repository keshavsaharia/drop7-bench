/**
 * Card art for `tree-search/observable-mcts`: two routes, one node. Each
 * route reaches the same visible position while still carrying a different
 * sampled future, so a search that stored the future would keep two nodes
 * apart. Keyed by the visible position alone, the two collapse into one: the
 * duplicate nodes slide together and fuse, the imagined futures fade to the
 * side because nothing in the key holds them, and the merged node keeps both
 * incoming edges.
 *
 * Server component. Motion lives in observable-mcts.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtDisc, type BoardGeometry } from "../board";
import "./observable-mcts.css";

interface Point {
  x: number;
  y: number;
}

/** The position the search is thinking from. */
const ROOT: Point = { x: 160, y: 20 };
/** The two chance outcomes it crossed, each drawn once and remembered. */
const LEFT: Point = { x: 64, y: 56 };
const RIGHT: Point = { x: 256, y: 56 };
/** Where a future-keyed search would put two separate nodes. */
const SPLIT_LEFT: Point = { x: 112, y: 140 };
const SPLIT_RIGHT: Point = { x: 208, y: 140 };
/** The one node this search keeps. */
const MERGED: Point = { x: 160, y: 134 };

/** Two discs apiece: the rest of the future each route happened to sample. */
const FUTURE_LEFT: BoardGeometry = { x: 34, y: 84, cell: 20, cols: 2, rows: 1 };
const FUTURE_RIGHT: BoardGeometry = { ...FUTURE_LEFT, x: 234 };
const LEFT_DISCS = [4, 1];
const RIGHT_DISCS = [6, 3];

function edge(from: Point, to: Point): string {
  return `M${from.x},${from.y}L${to.x},${to.y}`;
}

function FutureStrip({ g, discs }: { g: BoardGeometry; discs: readonly number[] }) {
  return (
    <g>
      <rect
        x={g.x}
        y={g.y}
        width={g.cols * g.cell}
        height={g.rows * g.cell}
        rx={4}
        fill="var(--color-raised)"
        stroke="var(--color-rule-strong)"
      />
      {discs.map((value, index) => (
        <ArtDisc key={value} value={value} col={index} row={0} g={g} />
      ))}
    </g>
  );
}

export function ObservableMctsArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-observable-mcts",
        "Two routes carrying different sampled futures arrive at the same visible position and become one node",
        props,
      )}
    >
      <path
        d={`${edge(ROOT, LEFT)}${edge(ROOT, RIGHT)}`}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth="1.2"
      />
      <path
        data-anim="split"
        d={`${edge(LEFT, SPLIT_LEFT)}${edge(RIGHT, SPLIT_RIGHT)}`}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth="1.2"
        strokeDasharray="4 4"
        opacity="0"
      />
      <path
        data-anim="merge"
        d={`${edge(LEFT, MERGED)}${edge(RIGHT, MERGED)}`}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth="1.8"
      />
      <g data-anim="futures" opacity="0.35">
        <FutureStrip g={FUTURE_LEFT} discs={LEFT_DISCS} />
        <FutureStrip g={FUTURE_RIGHT} discs={RIGHT_DISCS} />
      </g>
      <circle
        data-anim="ghost-left"
        cx={SPLIT_LEFT.x}
        cy={SPLIT_LEFT.y}
        r="7"
        fill="var(--color-raised)"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
        opacity="0"
      />
      <circle
        data-anim="ghost-right"
        cx={SPLIT_RIGHT.x}
        cy={SPLIT_RIGHT.y}
        r="7"
        fill="var(--color-raised)"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
        opacity="0"
      />
      <circle
        data-anim="merged"
        cx={MERGED.x}
        cy={MERGED.y}
        r="11"
        fill="var(--color-accent-strong)"
        stroke="var(--color-accent)"
        strokeWidth="1.5"
      />
      {[LEFT, RIGHT].map((node) => (
        <circle
          key={node.x}
          cx={node.x}
          cy={node.y}
          r="7"
          fill="var(--color-raised)"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
        />
      ))}
      <rect
        x={ROOT.x - 8}
        y={ROOT.y - 8}
        width="16"
        height="16"
        rx="3"
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth="1.2"
      />
      <text x="160" y="78" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        sampled futures
      </text>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="166" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          one node per visible state
        </text>
      </g>
    </svg>
  );
}
