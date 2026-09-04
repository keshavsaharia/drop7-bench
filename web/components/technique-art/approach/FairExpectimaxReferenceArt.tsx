/**
 * Card art for `fair-expectimax/reference`: the shape of the reference search
 * itself. A root, seven moves, seven chance outcomes under each, and the
 * fourth level drawn as a comb the frame cannot hold — nothing is pruned. On
 * play the fan fills a level at a time, then one root edge lights and the
 * rest dim: the whole tree is paid for, one column is played.
 *
 * Server component. Motion lives in reference.css (opacity and transform
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./reference.css";

const ROOT = { x: 160, y: 22 };
const L1_Y = 56;
const L2_Y = 100;
const L3_Y = 138;
const L1 = [25, 70, 115, 160, 205, 250, 295];
const L2 = Array.from({ length: 49 }, (_, i) => 12 + i * 6.17);
const L3 = Array.from({ length: 98 }, (_, i) => 11 + i * 3.06);

/** One circle as a path subpath, so a whole level costs a single element. */
function dot(x: number, y: number, r: number): string {
  return `M${(x - r).toFixed(2)},${y}a${r},${r} 0 1,0 ${2 * r},0a${r},${r} 0 1,0 ${-2 * r},0`;
}

const EDGES_1 = L1.map((x) => `M${ROOT.x},${ROOT.y + 8}L${x},${L1_Y - 6}`).join("");
const EDGES_2 = L1.flatMap((x, i) =>
  L2.slice(i * 7, i * 7 + 7).map((x2) => `M${x},${L1_Y + 6}L${x2.toFixed(2)},${L2_Y - 4}`),
).join("");
const WEDGES_3 = L2.map(
  (x) => `M${x.toFixed(2)},${L2_Y + 4}L${(x - 2.6).toFixed(2)},${L3_Y - 4}L${(x + 2.6).toFixed(2)},${L3_Y - 4}z`,
).join("");
const NODES_1 = L1.map((x) => dot(x, L1_Y, 5.5)).join("");
const NODES_2 = L2.map((x) => dot(x, L2_Y, 2.4)).join("");
const COMB_3 = L3.map((x) => `M${x.toFixed(2)},${L3_Y - 3}v11`).join("");
const CHOSEN = 3;

export function FairExpectimaxReferenceArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-reference", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ??
        "The reference search: an unpruned four-level fan of every move and every disc, of which one root edge is played"
      }
    >
      <g className="fan" data-anim="dim">
        <g className="lv1" data-anim="lv1">
          <path d={EDGES_1} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.2" />
          <path d={NODES_1} fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1.2" />
        </g>
        <g className="lv2" data-anim="lv2">
          <path d={EDGES_2} fill="none" stroke="var(--color-ink-4)" strokeWidth="0.7" />
          <path d={NODES_2} fill="var(--color-ink-3)" />
        </g>
        <g className="lv3" data-anim="lv3">
          <path d={WEDGES_3} fill="var(--color-ink-4)" opacity="0.45" />
          <path d={COMB_3} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.1" />
        </g>
      </g>
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
      <g className="choice" data-anim="choice">
        <line
          x1={ROOT.x}
          y1={ROOT.y + 8}
          x2={L1[CHOSEN]}
          y2={L1_Y - 6}
          stroke="var(--color-accent)"
          strokeWidth="3"
          strokeLinecap="round"
        />
        <circle cx={L1[CHOSEN]} cy={L1_Y} r="6" fill="var(--color-accent-strong)" stroke="var(--color-accent)" />
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="20">
          depth 4
        </text>
        <text x="8" y="32">
          7 · 49 · 343
        </text>
        <text x="312" y="20" textAnchor="end">
          nothing pruned
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text
          x="160"
          y="172"
          textAnchor="middle"
          fontFamily="var(--font-mono)"
          fontSize="9"
          fill="var(--color-ink-2)"
        >
          every branch valued, one column played
        </text>
      </g>
    </svg>
  );
}
