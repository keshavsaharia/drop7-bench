/**
 * Card art for `tree-search/nnue-guided`: the search tree with a learned
 * evaluator steering where the budget goes, not what the answer is. Every
 * legal column at the root stays in the tree; one ply in, the network numbers
 * the interior branches, the branch it ranks last fades out of the search, and
 * the two it keeps are the only ones carried to depth 3 and then to depth 4.
 * The tree to depth 2 is there from the first frame; what plays is the
 * ordering and the reach it buys.
 *
 * Server component. Motion lives in nnue-guided.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./nnue-guided.css";

/** The root node, and the x of each ply of nodes to its right. */
const ROOT = { x: 24, y: 90 };
const PLY_X = [78, 146, 206, 266];

/** The seven legal columns at the root. The model may not touch any of them. */
const ROOT_Y = [24, 46, 68, 90, 112, 134, 156];

/** The interior branches the model is allowed to order, best rank first. */
const INTERIOR = [
  { y: 56, rank: 1 },
  { y: 90, rank: 2 },
  { y: 124, rank: 3 },
];

/** Depth-3 children, and the depth-4 pair only the top-ranked branch reaches. */
const DEPTH_3 = [
  { y: 40, from: 56 },
  { y: 68, from: 56 },
  { y: 84, from: 90 },
  { y: 108, from: 90 },
];
const DEPTH_4 = [
  { y: 32, from: 40 },
  { y: 60, from: 68 },
];

function edge(x1: number, y1: number, x2: number, y2: number): string {
  return `M${x1} ${y1}L${x2} ${y2}`;
}

const ROOT_EDGES = ROOT_Y.map((y) => edge(ROOT.x + 8, ROOT.y, PLY_X[0] - 6, y)).join("");
const KEPT_EDGES = INTERIOR.filter((branch) => branch.rank < 3)
  .map((branch) => edge(PLY_X[0] + 6, ROOT.y, PLY_X[1] - 6, branch.y))
  .join("");
const PRUNED_EDGE = edge(PLY_X[0] + 6, ROOT.y, PLY_X[1] - 6, INTERIOR[2].y);
const DEPTH_3_EDGES = DEPTH_3.map((node) => edge(PLY_X[1] + 6, node.from, PLY_X[2] - 5, node.y)).join("");
const DEPTH_4_EDGES = DEPTH_4.map((node) => edge(PLY_X[2] + 5, node.from, PLY_X[3] - 5, node.y)).join("");

/** One node of the tree; `guided` marks the plies the model's ordering bought. */
function Node({ x, y, guided = false }: { x: number; y: number; guided?: boolean }) {
  return (
    <rect
      x={x - 5}
      y={y - 5}
      width={10}
      height={10}
      rx={2}
      fill={guided ? "var(--color-accent-soft)" : "var(--color-raised)"}
      stroke={guided ? "var(--color-accent)" : "var(--color-rule-strong)"}
      strokeWidth={1.2}
    />
  );
}

export function NnueGuidedArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-nnue-guided",
        "A search tree that keeps every legal root column while the network numbers the interior branches, drops the one it ranks last and carries the best one two plies deeper",
        props,
      )}
    >
      <rect
        x={ROOT.x - 7}
        y={ROOT.y - 7}
        width={14}
        height={14}
        rx={3}
        fill="var(--color-raised)"
        stroke="var(--color-ink-2)"
        strokeWidth={1.3}
      />
      <text x={ROOT.x} y={ROOT.y - 15} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        root
      </text>
      <path d={ROOT_EDGES + KEPT_EDGES} fill="none" stroke="var(--color-ink-3)" strokeWidth={1} />
      {ROOT_Y.map((y) => (
        <Node key={y} x={PLY_X[0]} y={y} />
      ))}
      {INTERIOR.filter((branch) => branch.rank < 3).map((branch) => (
        <Node key={branch.rank} x={PLY_X[1]} y={branch.y} />
      ))}
      <g data-anim="prune" opacity={0.3}>
        <path d={PRUNED_EDGE} fill="none" stroke="var(--color-ink-3)" strokeWidth={1} />
        <Node x={PLY_X[1]} y={INTERIOR[2].y} />
      </g>
      <g data-anim="ranks" fontFamily={ART_MONO} fontSize={10} fontWeight={700} textAnchor="middle">
        {INTERIOR.map((branch) => (
          <text
            key={branch.rank}
            x={PLY_X[1]}
            y={branch.y - 9}
            fill={branch.rank < 3 ? "var(--color-accent)" : "var(--color-ink-3)"}
          >
            {branch.rank}
          </text>
        ))}
      </g>
      <g data-anim="deep-3">
        <path d={DEPTH_3_EDGES} fill="none" stroke="var(--color-accent)" strokeWidth={1} />
        {DEPTH_3.map((node) => (
          <Node key={node.y} x={PLY_X[2]} y={node.y} guided />
        ))}
      </g>
      <g data-anim="deep-4">
        <path d={DEPTH_4_EDGES} fill="none" stroke="var(--color-accent)" strokeWidth={1} />
        {DEPTH_4.map((node) => (
          <Node key={node.y} x={PLY_X[3]} y={node.y} guided />
        ))}
        <text x={PLY_X[3]} y={86} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          depth 4
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={174} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the net orders; the search decides
        </text>
      </g>
    </svg>
  );
}
