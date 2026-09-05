/**
 * Card art for Monte Carlo tree search: a lopsided tree grows one node per
 * beat (most of them under the left child), a dashed playout runs from the
 * deepest node to a payout badge, and a token carries the result back up the
 * path, lighting each node it passes.
 *
 * Server component. Motion lives in mcts.css (opacity, transform and fill
 * only); the markup is the resting frame: full tree, highlighted path, badge.
 */
import "./mcts.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

interface Node {
  id: string;
  x: number;
  y: number;
  parent?: string;
  /** Beat at which the node appears (1..7); the root is always present. */
  beat?: number;
  /** Nodes on the backed-up path get an accent fill. */
  onPath?: boolean;
}

const NODES: readonly Node[] = [
  { id: "root", x: 160, y: 24, onPath: true },
  { id: "l", x: 108, y: 62, parent: "root", beat: 1, onPath: true },
  { id: "r", x: 212, y: 62, parent: "root", beat: 2 },
  { id: "ll", x: 78, y: 100, parent: "l", beat: 3, onPath: true },
  { id: "lr", x: 138, y: 100, parent: "l", beat: 4 },
  { id: "lll", x: 56, y: 138, parent: "ll", beat: 5, onPath: true },
  { id: "llr", x: 100, y: 138, parent: "ll", beat: 6 },
  { id: "rl", x: 212, y: 100, parent: "r", beat: 7 },
];

const RADIUS = 7;

export function MctsArt({ mode = "hover", title, className }: ArtProps) {
  const byId = new Map(NODES.map((node) => [node.id, node]));
  return (
    <svg
      className={["tart", "tart--mcts", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Monte Carlo tree search: a lopsided tree, one playout, and the result backed up the path"}
    >
      <g className="tree">
        {NODES.map((node) => {
          const parent = node.parent ? byId.get(node.parent) : undefined;
          const groupProps = node.beat
            ? { className: `node node-${node.beat}`, "data-anim": "node" }
            : { className: "node node-root" };
          return (
            <g key={node.id} {...groupProps}>
              {parent && (
                <line
                  x1={parent.x}
                  y1={parent.y}
                  x2={node.x}
                  y2={node.y}
                  stroke="var(--color-ink-3)"
                  strokeWidth="1.2"
                />
              )}
              <circle
                className={node.onPath ? `mark mark-${node.id}` : undefined}
                data-anim={node.onPath ? "mark" : undefined}
                cx={node.x}
                cy={node.y}
                r={RADIUS}
                fill={node.onPath ? "var(--color-accent-strong)" : "var(--color-surface)"}
                stroke="var(--color-ink-3)"
                strokeWidth="1.2"
              />
            </g>
          );
        })}
      </g>
      <g className="simulation">
        <line
          className="playout"
          data-anim="playout"
          x1="56"
          y1="145"
          x2="84"
          y2="162"
          stroke="var(--color-ink-3)"
          strokeWidth="1.2"
          strokeDasharray="3 3"
        />
        <g className="badge" data-anim="badge">
          <rect x="86" y="156" width="30" height="14" rx="3" fill="var(--color-accent-strong)" />
          <text
            x="101"
            y="167"
            textAnchor="middle"
            fontFamily="var(--font-mono)"
            fontSize="10"
            fontWeight={700}
            fill="var(--color-accent-fg)"
          >
            +1
          </text>
        </g>
        <circle className="token" data-anim="token" cx="56" cy="138" r="3.5" fill="var(--color-accent)" opacity="0" />
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="124" y="167">playout</text>
        <text x="232" y="66">backup</text>
      </g>
      <g className="tart-final">
        <text x="304" y="172" textAnchor="end" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          most visits went left
        </text>
      </g>
    </svg>
  );
}
