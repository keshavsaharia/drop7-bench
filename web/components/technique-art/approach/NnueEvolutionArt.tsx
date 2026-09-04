/**
 * Card art for `lifetime-objective/nnue-evolution`: a fitness axis with the
 * frozen fair leaf marked high and the distilled warm start marked low, and
 * the twelve candidates of the population between them. On play the cloud
 * drifts up over the generations and tightens, and stops short of the fair
 * leaf — which is the result this approach recorded.
 *
 * Server component. Motion lives in nnue-evolution.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./nnue-evolution.css";

const AXIS_X = 92;
const FAIR_Y = 42;
const WARM_Y = 138;
const CLOUD_X = 196;

/** Twelve candidates in three clusters; the clusters converge as they rise. */
const CLUSTERS: ReadonlyArray<ReadonlyArray<readonly [number, number]>> = [
  [
    [-42, -6],
    [-33, 8],
    [-24, -2],
    [-38, 3],
  ],
  [
    [-6, 6],
    [2, -7],
    [8, 3],
    [-2, 0],
  ],
  [
    [24, 4],
    [32, -6],
    [40, 2],
    [30, 9],
  ],
];

function dots(cluster: ReadonlyArray<readonly [number, number]>): string {
  return cluster
    .map(([dx, dy]) => {
      const x = CLOUD_X + dx;
      const y = WARM_Y + dy;
      return `M${x - 4},${y}a4,4 0 1,0 8,0a4,4 0 1,0 -8,0`;
    })
    .join("");
}

export function NnueEvolutionArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-nnue-evolution", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "A population of twelve evolved leaves climbs from its warm start toward the fair leaf and stops short of it"
      }
    >
      <path
        d={`M${AXIS_X},152V26m0,0l-4,7m4,-7l4,7`}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <line x1={AXIS_X - 8} y1={FAIR_Y} x2={AXIS_X + 150} y2={FAIR_Y} stroke="var(--color-highlight)" strokeWidth="1.6" />
      <line x1={AXIS_X - 8} y1={WARM_Y} x2={AXIS_X + 150} y2={WARM_Y} stroke="var(--color-series-5)" strokeWidth="1.6" />
      <g fontFamily="var(--font-mono)" fontSize="9">
        <text x="8" y="30" fill="var(--color-ink-3)">
          fitness
        </text>
        <text x="8" y={FAIR_Y + 3} fill="var(--color-highlight)">
          fair leaf
        </text>
        <text x="8" y={WARM_Y + 3} fill="var(--color-series-5)">
          warm start
        </text>
      </g>
      <g className="cloud" data-anim="rise" fill="var(--color-accent)" transform="translate(0 -68)">
        <path className="cluster-l" data-anim="cluster-l" d={dots(CLUSTERS[0])} transform="translate(30 4)" />
        <path className="cluster-c" data-anim="cluster-c" d={dots(CLUSTERS[1])} transform="translate(2 -1)" />
        <path className="cluster-r" data-anim="cluster-r" d={dots(CLUSTERS[2])} transform="translate(-28 -3)" />
      </g>
      <g className="tart-final" data-anim="gap">
        <path
          d={`M${CLOUD_X + 58},${FAIR_Y}v28m-4,0h8m-4,0v0`}
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth="1"
          strokeDasharray="2 3"
        />
        <text x={CLOUD_X + 64} y={FAIR_Y + 20} fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          short
        </text>
      </g>
      <g className="gens" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="170">
          12 candidates, generation by generation
        </text>
      </g>
    </svg>
  );
}
