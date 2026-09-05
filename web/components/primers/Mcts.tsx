/**
 * Figures for the Monte Carlo tree search primer at
 * content/learn/techniques/mcts.mdx.
 *
 * The toy is a maze with three paths and dice, so every count, average and
 * prior here is the toy's own. Nothing in these figures is a measured search;
 * the recorded results live in the prose, with their sources.
 *
 * Server components: SVG with CSS keyframes from ./mcts.css on elements
 * marked data-anim. Nodes and marks animate by opacity, the playout line by
 * stroke-dashoffset, bars and the arrow by transform; no text animates.
 */
import "./mcts.css";
import type { ReactNode } from "react";

const SANS = "var(--font-sans)";
const MONO = "var(--font-mono)";
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const INK_4 = "var(--color-ink-4)";
const RULE = "var(--color-rule)";
const SURFACE = "var(--color-surface)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const S1 = "var(--color-series-1)";
const S3 = "var(--color-series-3)";

function Fig({ caption, children }: { caption: string; children: ReactNode }) {
  return (
    <figure className="fig">
      <div className="fig-frame">{children}</div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/* =========================================================================
 * 1. Lopsided growth: one node per playout, most of them under the best child.
 * ========================================================================= */

interface TreeNode {
  x: number;
  y: number;
  parent: number | null;
}

/** Node order is the order the walks added them; index 0 is the root. */
const TREE: TreeNode[] = [
  { x: 280, y: 30, parent: null },
  { x: 150, y: 80, parent: 0 },
  { x: 280, y: 80, parent: 0 },
  { x: 410, y: 80, parent: 0 },
  { x: 100, y: 130, parent: 1 },
  { x: 150, y: 130, parent: 1 },
  { x: 280, y: 130, parent: 2 },
  { x: 200, y: 130, parent: 1 },
  { x: 130, y: 180, parent: 5 },
  { x: 410, y: 130, parent: 3 },
  { x: 170, y: 180, parent: 5 },
  { x: 115, y: 230, parent: 8 },
];
const PATH_TO_LEAF = [11, 8, 5, 1, 0];

export function MctsLopsidedGrowth() {
  const leaf = TREE[11];
  const badgeY = 268;
  const children = [
    { index: 1, label: "A", visits: 7, avg: "6.1" },
    { index: 2, label: "B", visits: 2, avg: "3.5" },
    { index: 3, label: "C", visits: 2, avg: "4.0" },
  ];
  return (
    <Fig caption="Eleven walks from the same start, one node added per walk. Path A kept paying, so most walks went back down it and the tree is four levels deep there and one node deep under B and C. The dashed line is the twelfth walk's playout, finishing the maze with a cheap rule past the stored tree; its payout is then added to every node on the path back up.">
      <svg
        className="primer-mcts"
        viewBox="0 0 560 300"
        role="img"
        aria-label="A search tree growing one node at a time, deep under the left child and shallow elsewhere, with a playout line to a payout and backup marks on the path"
      >
        <text x={20} y={22} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the tree after eleven walks
        </text>
        {TREE.map((node, i) => {
          if (node.parent === null) return null;
          const parent = TREE[node.parent];
          return (
            <g key={`e${i}`} data-anim="mcts-node" data-step={i}>
              <path d={`M${parent.x},${parent.y} L${node.x},${node.y}`} stroke={INK_4} strokeWidth={1.2} />
            </g>
          );
        })}
        <circle cx={TREE[0].x} cy={TREE[0].y} r={9} fill={INK_2} />
        <text x={TREE[0].x + 14} y={TREE[0].y + 4} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          start
        </text>
        {TREE.map((node, i) => {
          if (i === 0) return null;
          const onPath = PATH_TO_LEAF.includes(i);
          return (
            <g key={i} data-anim="mcts-node" data-step={i}>
              <circle cx={node.x} cy={node.y} r={i <= 3 ? 8 : 6} fill={onPath ? ACCENT : S1} stroke={SURFACE} strokeWidth={1.5} />
            </g>
          );
        })}
        {children.map((child) => {
          const node = TREE[child.index];
          const dx = child.index === 1 ? -14 : 14;
          const anchor = child.index === 1 ? "end" : "start";
          return (
            <g key={child.label}>
              <text x={node.x + dx} y={node.y - 2} textAnchor={anchor} fontSize={10} fontFamily={SANS} fontWeight={700} fill={INK}>
                path {child.label}
              </text>
              <text x={node.x + dx} y={node.y + 10} textAnchor={anchor} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                {child.visits} visits, avg {child.avg}
              </text>
            </g>
          );
        })}
        <path
          data-anim="mcts-playout"
          d={`M${leaf.x},${leaf.y + 8} V${badgeY - 12}`}
          stroke={S3}
          strokeWidth={1.6}
          strokeDasharray="4 3"
          fill="none"
        />
        <g data-anim="mcts-badge">
          <rect x={leaf.x - 30} y={badgeY - 10} width={60} height={20} rx={10} fill={S3} />
          <text x={leaf.x} y={badgeY + 4} textAnchor="middle" fontSize={9.5} fontFamily={MONO} fontWeight={700} fill={SURFACE}>
            payout 7
          </text>
        </g>
        <text x={leaf.x + 38} y={badgeY - 8} fontSize={9} fontFamily={SANS} fill={INK_3}>
          playout: finish the maze
        </text>
        <text x={leaf.x + 38} y={badgeY + 4} fontSize={9} fontFamily={SANS} fill={INK_3}>
          with a cheap rule
        </text>
        {PATH_TO_LEAF.map((index, k) => {
          const node = TREE[index];
          return (
            <g key={index} data-anim={`mcts-mark-${k + 1}`}>
              <text x={node.x - 12} y={node.y - 10} textAnchor="end" fontSize={9} fontFamily={MONO} fontWeight={700} fill={S3}>
                +7
              </text>
            </g>
          );
        })}
        <g transform="translate(440 150)">
          <text y={0} fontSize={9.5} fontFamily={SANS} fontWeight={600} fill={INK}>
            each walk
          </text>
          {["select the best child plus bonus", "expand one new node", "play out to a payout", "back up along the path"].map((line, i) => (
            <text key={line} y={16 + i * 14} fontSize={9} fontFamily={SANS} fill={INK_3}>
              {i + 1}. {line}
            </text>
          ))}
        </g>
        <text x={20} y={288} fontSize={9} fontFamily={SANS} fill={INK_3}>
          Toy counts; the tree shape is the mechanism, not a measured search.
        </text>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 2. The selection rule as two stacked bars: average plus a shrinking bonus.
 * ========================================================================= */

export function MctsUcbBars() {
  const base = 210;
  const unit = 20;
  const barW = 56;
  const bars = [
    { id: "a", label: "A", x: 90, avg: 5.0, bonusLate: 1.02, earlyVisits: 5, lateVisits: 5 },
    { id: "b", label: "B", x: 230, avg: 3.5, bonusLate: 2.28, earlyVisits: 1, lateVisits: 1 },
    { id: "c", label: "C", x: 370, avg: 4.0, bonusLate: 1.14, earlyVisits: 1, lateVisits: 4 },
  ];
  return (
    <Fig caption="Under each child, a solid bar for its average payout so far and a translucent bar for its exploration bonus, which is large when a child has few visits and shrinks as they accumulate. The arrow follows the taller stack. Early on, C's untried bonus wins it the walk; after three visits to C its bonus has shrunk and A's better average takes over.">
      <svg
        className="primer-mcts"
        viewBox="0 0 560 260"
        role="img"
        aria-label="Three stacked bars of average plus exploration bonus; the bonus on the right-hand child shrinks and the selection arrow moves to the left-hand child"
      >
        <text x={40} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          score = average + c × √(ln N ÷ n)
        </text>
        <text x={40} y={38} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          N: the parent&apos;s visits; n: this child&apos;s visits; c = 1.5 in this toy
        </text>
        <path d={`M40,${base} H${bars[2].x + barW + 40}`} stroke={INK_2} strokeWidth={1} />
        {bars.map((bar) => {
          const avgH = bar.avg * unit;
          const bonusH = bar.bonusLate * unit;
          return (
            <g key={bar.id}>
              <rect x={bar.x} y={base - avgH} width={barW} height={avgH} rx={3} fill={S1} />
              <rect
                data-anim={`mcts-bonus-${bar.id}`}
                x={bar.x}
                y={base - avgH - bonusH}
                width={barW}
                height={bonusH}
                rx={3}
                fill={ACCENT_SOFT}
                stroke={ACCENT}
                strokeWidth={1}
              />
              <text x={bar.x + barW / 2} y={base - avgH + 14} textAnchor="middle" fontSize={9} fontFamily={MONO} fill={SURFACE}>
                avg {bar.avg.toFixed(1)}
              </text>
              <text x={bar.x + barW / 2} y={base + 18} textAnchor="middle" fontSize={10} fontFamily={SANS} fontWeight={700} fill={INK}>
                child {bar.label}
              </text>
              {bar.earlyVisits === bar.lateVisits ? (
                <text x={bar.x + barW / 2} y={base + 32} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                  n = {bar.lateVisits}
                </text>
              ) : (
                <g>
                  <g data-anim="mcts-label-early" opacity={0}>
                    <text x={bar.x + barW / 2} y={base + 32} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                      n = {bar.earlyVisits}
                    </text>
                  </g>
                  <g data-anim="mcts-label-late">
                    <text x={bar.x + barW / 2} y={base + 32} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                      n = {bar.lateVisits}
                    </text>
                  </g>
                </g>
              )}
            </g>
          );
        })}
        {/* the selection arrow: base position under A, slides in from under C */}
        <g data-anim="mcts-arrow">
          <path d={`M${bars[0].x + barW / 2 - 7},${base + 48} L${bars[0].x + barW / 2},${base + 40} L${bars[0].x + barW / 2 + 7},${base + 48} Z`} fill={ACCENT} />
        </g>
        <text x={40} y={base + 46} fontSize={8.5} fontFamily={SANS} fill={ACCENT}>
          next walk
        </text>
        <g transform="translate(470 120)">
          <rect x={0} y={-8} width={12} height={12} rx={2} fill={S1} />
          <text x={18} y={2} fontSize={9} fontFamily={SANS} fill={INK_2}>
            average
          </text>
          <rect x={0} y={14} width={12} height={12} rx={2} fill={ACCENT_SOFT} stroke={ACCENT} />
          <text x={18} y={24} fontSize={9} fontFamily={SANS} fill={INK_2}>
            bonus
          </text>
          <text x={0} y={48} fontSize={8.5} fontFamily={SANS} fill={INK_3}>
            B&apos;s average is low,
          </text>
          <text x={0} y={60} fontSize={8.5} fontFamily={SANS} fill={INK_3}>
            but its bonus keeps it
          </text>
          <text x={0} y={72} fontSize={8.5} fontFamily={SANS} fill={INK_3}>
            in the running
          </text>
        </g>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 3. A prior: plausible moves are tried first; implausible ones must earn it.
 * ========================================================================= */

export function MctsPrior() {
  const root = { x: 280, y: 40 };
  const children = [
    { label: "A", x: 110, prior: 0.6, order: "tried 1st and 2nd" },
    { label: "B", x: 280, prior: 0.3, order: "tried 3rd" },
    { label: "C", x: 450, prior: 0.1, order: "waits its turn" },
  ];
  const cy = 128;
  return (
    <Fig caption="The same three children with a prior above each: a cheap guess, before any walk, at how plausible the move is. In PUCT the exploration bonus is multiplied by that prior, so the first walks go to the largest dot and a move with a small prior has to earn attention by paying well when it is finally tried.">
      <svg
        className="primer-mcts"
        viewBox="0 0 560 210"
        role="img"
        aria-label="A root with three children, each with a prior dot of a different size above it; the largest prior is tried first"
      >
        <text x={20} y={22} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          PUCT: bonus × prior
        </text>
        {children.map((child) => (
          <path key={child.label} d={`M${root.x},${root.y} L${child.x},${cy}`} stroke={INK_4} strokeWidth={1.2} />
        ))}
        <circle cx={root.x} cy={root.y} r={9} fill={INK_2} />
        <text x={root.x + 14} y={root.y + 4} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          start
        </text>
        {children.map((child, i) => {
          const r = 4 + child.prior * 16;
          const px = child.x + 30;
          const py = cy - 24;
          return (
            <g key={child.label}>
              <circle cx={child.x} cy={cy} r={12} fill={i === 0 ? ACCENT : S1} stroke={SURFACE} strokeWidth={1.5} />
              <text x={child.x} y={cy + 4} textAnchor="middle" fontSize={10} fontFamily={SANS} fontWeight={700} fill={SURFACE}>
                {child.label}
              </text>
              <circle cx={px} cy={py} r={r} fill={ACCENT_SOFT} stroke={ACCENT} strokeWidth={1.2} />
              <text x={px + r + 5} y={py + 3} fontSize={9} fontFamily={MONO} fill={ACCENT}>
                P = {child.prior.toFixed(1)}
              </text>
              <text x={child.x} y={cy + 30} textAnchor="middle" fontSize={9} fontFamily={SANS} fill={INK_2}>
                {child.order}
              </text>
            </g>
          );
        })}
        <path d={`M40,${cy + 46} H520`} stroke={RULE} strokeWidth={1} />
        <text x={40} y={cy + 64} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          The prior can come from a policy network or a hand-written guess; the walks then correct it.
        </text>
      </svg>
    </Fig>
  );
}
