/**
 * Card art for `tree-search/mcts`: one iteration of the TypeScript lab, drawn
 * as its four phases. A path is selected down to the frontier, one node is
 * added there, a short greedy playout runs from it to an evaluated leaf, and
 * the value is carried back up so every node on the path takes the accent
 * fill. The four phase names light in turn beside the tree and stay lit, so
 * the resting frame is one complete iteration.
 *
 * Server component. Motion lives in mcts.css (stroke-dashoffset, transform,
 * opacity and fill only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./mcts.css";

interface Point {
  x: number;
  y: number;
}

/** The stored tree: a root, three columns tried, and the branch the walk prefers. */
const ROOT: Point = { x: 110, y: 20 };
const SELECTED_1: Point = { x: 56, y: 54 };
const SELECTED_2: Point = { x: 32, y: 88 };
/** The position the walk left the tree at, added by the expand phase. */
const ADDED: Point = { x: 24, y: 122 };

const OTHER_NODES: readonly Point[] = [
  { x: 112, y: 54 },
  { x: 168, y: 54 },
  { x: 80, y: 88 },
  { x: 112, y: 88 },
  { x: 150, y: 88 },
  { x: 190, y: 88 },
  { x: 80, y: 122 },
];

const EDGES = [
  [ROOT, SELECTED_1],
  [ROOT, { x: 112, y: 54 }],
  [ROOT, { x: 168, y: 54 }],
  [SELECTED_1, SELECTED_2],
  [SELECTED_1, { x: 80, y: 88 }],
  [{ x: 112, y: 54 }, { x: 112, y: 88 }],
  [{ x: 168, y: 54 }, { x: 150, y: 88 }],
  [{ x: 168, y: 54 }, { x: 190, y: 88 }],
  [{ x: 80, y: 88 }, { x: 80, y: 122 }]
]
  .map(([from, to]) => `M${from.x},${from.y}L${to.x},${to.y}`)
  .join("");

/** One circle as a path subpath, so the untouched tree costs a single element. */
function dot(p: Point, r: number): string {
  return `M${p.x - r},${p.y}a${r},${r} 0 1,0 ${2 * r},0a${r},${r} 0 1,0 ${-2 * r},0`;
}

const OTHER_DOTS = OTHER_NODES.map((p) => dot(p, 6)).join("");

/** The greedy playout: two moves and the board the evaluator scores. */
const PLAYOUT = `M${ADDED.x},${ADDED.y}L44,138L64,146L84,156`;

const PHASES = [
  { name: "select", y: 48 },
  { name: "expand", y: 76 },
  { name: "simulate", y: 104 },
  { name: "back up", y: 132 },
];

const NODE_RADIUS = 6;

export function MctsApproachArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-mcts",
        "One search iteration: a path selected to the frontier, a node added, a short playout, and its value carried back to the root",
        props,
      )}
    >
      <path d={EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth="1.1" />
      <path
        data-anim="select"
        d={`M${ROOT.x},${ROOT.y}L${SELECTED_1.x},${SELECTED_1.y}L${SELECTED_2.x},${SELECTED_2.y}`}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth="2.2"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray="106"
        strokeDashoffset="0"
      />
      <g data-anim="expand">
        <line
          x1={SELECTED_2.x}
          y1={SELECTED_2.y}
          x2={ADDED.x}
          y2={ADDED.y}
          stroke="var(--color-accent)"
          strokeWidth="2.2"
          strokeLinecap="round"
        />
        <circle
          data-anim="fill-added"
          cx={ADDED.x}
          cy={ADDED.y}
          r={NODE_RADIUS}
          fill="var(--color-accent-strong)"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
        />
      </g>
      <path d={OTHER_DOTS} fill="var(--color-raised)" stroke="var(--color-ink-4)" strokeWidth="1" />
      <circle
        data-anim="fill-inner"
        cx={SELECTED_2.x}
        cy={SELECTED_2.y}
        r={NODE_RADIUS}
        fill="var(--color-accent-strong)"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
      />
      <circle
        data-anim="fill-outer"
        cx={SELECTED_1.x}
        cy={SELECTED_1.y}
        r={NODE_RADIUS}
        fill="var(--color-accent-strong)"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
      />
      <rect
        data-anim="fill-root"
        x={ROOT.x - 7}
        y={ROOT.y - 7}
        width="14"
        height="14"
        rx="3"
        fill="var(--color-accent-strong)"
        stroke="var(--color-ink-2)"
        strokeWidth="1.2"
      />
      <path
        data-anim="playout"
        d={PLAYOUT}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
        strokeDasharray="3 3"
      />
      <rect
        data-anim="token"
        x="79.5"
        y="151.5"
        width="9"
        height="9"
        rx="2"
        fill="var(--color-highlight)"
      />
      <g fontFamily={ART_MONO} fontSize="10" fill="var(--color-accent)">
        {PHASES.map((phase) => (
          <text key={phase.name} data-anim={`phase-${phase.name.replace(" ", "-")}`} x="222" y={phase.y}>
            {phase.name}
          </text>
        ))}
      </g>
    </svg>
  );
}
