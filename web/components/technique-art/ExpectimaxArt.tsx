/**
 * Expectimax card art. A max node at the root, three chance nodes (the disc
 * the game might deal), two leaves under each. On play the leaf values fade
 * in, each chance node shows its average, and the root keeps the best one:
 * the chosen edge thickens and the two losing edges dim.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./expectimax.css";

const NODES = [70, 160, 250];
const LEAVES: [number, number][] = [
  [40, 20],
  [10, 70],
  [60, 0],
];
const AVERAGES = [30, 40, 30];
const LEAF_Y = 146;

export function ExpectimaxArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "expectimax",
        "An expectimax tree: leaf values average up into chance nodes and the root keeps the best average",
        props,
      )}
    >
      <g className="root-edges" stroke="var(--color-ink-3)" strokeWidth={1.5} fill="none">
        <g data-anim="losers" opacity={0.3}>
          <line x1={160} y1={33} x2={70} y2={73} />
          <line x1={160} y1={33} x2={250} y2={73} />
        </g>
        <line x1={160} y1={33} x2={160} y2={73} />
      </g>
      <g className="leaf-edges" stroke="var(--color-ink-4)" strokeWidth={1} fill="none">
        {NODES.map((x) => (
          <path key={x} d={`M${x} 95L${x - 22} 138M${x} 95L${x + 22} 138`} />
        ))}
      </g>
      <g className="nodes" fill="var(--color-raised)" stroke="var(--color-ink-3)">
        <rect x={149} y={11} width={22} height={22} rx={4} />
        {NODES.map((x) => (
          <circle key={x} cx={x} cy={84} r={11} />
        ))}
      </g>
      <g className="leaves" fill="var(--color-raised)" stroke="var(--color-rule-strong)">
        {NODES.flatMap((x) =>
          [-22, 22].map((dx) => (
            <rect key={`${x}${dx}`} x={x + dx - 10} y={138} width={20} height={16} rx={3} />
          )),
        )}
      </g>
      <g className="labels" fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-3)">
        <text x={178} y={26}>
          max
        </text>
        <text x={268} y={88}>
          avg
        </text>
      </g>
      <g
        data-anim="leaf-values"
        fontSize={10}
        fontFamily={ART_MONO}
        fill="var(--color-ink-2)"
        textAnchor="middle"
        dominantBaseline="central"
      >
        {NODES.flatMap((x, i) =>
          LEAVES[i].map((value, j) => (
            <text key={`${x}${j}`} x={x + (j ? 22 : -22)} y={LEAF_Y}>
              {value}
            </text>
          )),
        )}
      </g>
      <g
        data-anim="node-values"
        fontSize={11}
        fontFamily={ART_MONO}
        fill="var(--color-ink)"
        textAnchor="middle"
        dominantBaseline="central"
      >
        {NODES.map((x, i) => (
          <text key={x} x={x} y={84}>
            {AVERAGES[i]}
          </text>
        ))}
      </g>
      <g className="tart-final" data-anim="choice">
        <line
          x1={160}
          y1={33}
          x2={160}
          y2={73}
          stroke="var(--color-accent)"
          strokeWidth={3.5}
          strokeLinecap="round"
        />
        <text
          x={160}
          y={22}
          textAnchor="middle"
          dominantBaseline="central"
          fontSize={10}
          fontWeight={700}
          fontFamily={ART_MONO}
          fill="var(--color-ink)"
        >
          40
        </text>
      </g>
    </svg>
  );
}
