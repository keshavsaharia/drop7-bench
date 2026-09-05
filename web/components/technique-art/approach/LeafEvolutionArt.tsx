/**
 * Card art for `lifetime-objective/leaf-evolution`: the four-move search drawn
 * whole and left alone, with the evolution happening only in the row of leaf
 * evaluators along its frontier. On play every leaf's number is refitted — the
 * bars inside the boxes resize, and nothing above them moves — until a
 * different branch of the same tree becomes the best one.
 *
 * Server component. Motion lives in leaf-evolution.css (transform and opacity
 * only); the SVG's own attributes are the resting frame, which is the refitted
 * leaf and the branch it now chooses.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./leaf-evolution.css";

const ROOT: readonly [number, number] = [160, 24];
const TIER_1 = [104, 216];
const TIER_2 = [76, 132, 188, 244];
const TIER_3 = [65, 87, 121, 143, 177, 199, 233, 255];
const Y_1 = 58;
const Y_2 = 92;
const Y_3 = 118;

/** The leaf boxes along the frontier: the only part of the search being tuned. */
const BOX_Y = 138;
const BOX_W = 20;
const BOX_H = 14;
/** Refitted leaf values, as bar widths inside the 16-unit interior of a box. */
const LEAF_W = [9, 13, 6, 15, 11, 17, 7, 12];

const EDGES = [
  `M${ROOT[0]},${ROOT[1]}L${TIER_1[0]},${Y_1}`,
  `M${ROOT[0]},${ROOT[1]}L${TIER_1[1]},${Y_1}`,
  ...TIER_1.flatMap((x, i) => [
    `M${x},${Y_1}L${TIER_2[i * 2]},${Y_2}`,
    `M${x},${Y_1}L${TIER_2[i * 2 + 1]},${Y_2}`,
  ]),
  ...TIER_2.flatMap((x, i) => [
    `M${x},${Y_2}L${TIER_3[i * 2]},${Y_3}`,
    `M${x},${Y_2}L${TIER_3[i * 2 + 1]},${Y_3}`,
  ]),
  ...TIER_3.map((x) => `M${x},${Y_3}V${BOX_Y}`),
].join("");

/** The branch the frozen leaf preferred, and the one the refitted leaf takes. */
const OLD_PATH = `M${ROOT[0]},${ROOT[1]}L${TIER_1[0]},${Y_1}L${TIER_2[0]},${Y_2}L${TIER_3[0]},${Y_3}V${BOX_Y}`;
const NEW_PATH = `M${ROOT[0]},${ROOT[1]}L${TIER_1[1]},${Y_1}L${TIER_2[2]},${Y_2}L${TIER_3[5]},${Y_3}V${BOX_Y}`;

export function LeafEvolutionArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-leaf-evolution",
        "A four-move search tree holds still while the row of leaf evaluators at its frontier is refitted, and a different branch becomes the best",
        props,
      )}
    >
      <g className="depth" stroke="var(--color-ink-4)" strokeWidth={1.1} fill="none">
        <path d={`M28,${ROOT[1]}H22V${BOX_Y}H28`} />
        <path d={`M22,${Y_1}H26M22,${Y_2}H26M22,${Y_3}H26`} />
      </g>

      <path d={EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth={1} />

      <path data-anim="old" d={OLD_PATH} fill="none" stroke="var(--color-ink-2)" strokeWidth={2.4} opacity={0} />

      <g className="nodes" fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth={1}>
        <circle cx={ROOT[0]} cy={ROOT[1]} r={4} />
        {TIER_1.map((x) => (
          <circle key={x} cx={x} cy={Y_1} r={3.6} />
        ))}
        {TIER_2.map((x) => (
          <circle key={x} cx={x} cy={Y_2} r={3.2} />
        ))}
        {TIER_3.map((x) => (
          <circle key={x} cx={x} cy={Y_3} r={2.6} />
        ))}
      </g>

      <g className="leaves">
        {TIER_3.map((cx) => (
          <rect
            key={cx}
            x={cx - BOX_W / 2}
            y={BOX_Y}
            width={BOX_W}
            height={BOX_H}
            rx={2.5}
            fill="var(--color-raised)"
            stroke="var(--color-rule-strong)"
            strokeWidth={1}
          />
        ))}
        {TIER_3.map((cx, i) => (
          <rect
            key={cx}
            data-anim={`leaf${i}`}
            x={cx - 8}
            y={BOX_Y + 3}
            width={LEAF_W[i]}
            height={BOX_H - 6}
            rx={1.5}
            fill="var(--color-series-1)"
          />
        ))}
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={10} y={16}>
          depth 4
        </text>
        <text x={160} y={170} textAnchor="middle">
          leaf weights
        </text>
      </g>

      <g className="tart-final" data-anim="pick">
        <path d={NEW_PATH} fill="none" stroke="var(--color-accent)" strokeWidth={3} strokeLinejoin="round" />
        <rect
          x={TIER_3[5] - BOX_W / 2 - 2}
          y={BOX_Y - 2}
          width={BOX_W + 4}
          height={BOX_H + 4}
          rx={3.5}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.4}
        />
      </g>
    </svg>
  );
}
