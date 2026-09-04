/**
 * Card art for determinized planning: one root position fans into three
 * sampled futures drawn as three thin lanes; a plan line runs down each; the
 * three plans merge into one chosen column marker; a small ghost "?" over the
 * merge is the knowledge the fused plan assumes it will have. The resting
 * frame (the SVG's own attributes) is the finished drawing; the first
 * keyframe holds the empty lanes.
 *
 * Server component. Motion is CSS in determinization.css on the data-anim
 * elements; the shared play/pause contract lives in art.css.
 */
import "./determinization.css";
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";

const ROOT: [number, number] = [160, 22];
const MERGE: [number, number] = [160, 156];
const LANES = [
  { x: 72, disc: 3 },
  { x: 160, disc: 7 },
  { x: 248, disc: 2 },
];

function lanePath(x: number): string {
  return `M${ROOT[0]} ${ROOT[1] + 8} L${x} 62 V128 L${MERGE[0]} ${MERGE[1] - 4}`;
}

export function DeterminizationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "determinization",
        "One position fans into three imagined futures, a plan is drawn in each, and the plans merge into one chosen column",
        props,
      )}
    >
      <rect x={ROOT[0] - 8} y={ROOT[1] - 8} width={16} height={16} rx={2} fill="var(--color-accent-strong)" />
      <g className="lanes">
        {LANES.map((lane) => (
          <path key={lane.x} d={lanePath(lane.x)} stroke="var(--color-rule-strong)" strokeWidth={1.5} fill="none" />
        ))}
      </g>
      <g className="tart-final">
        {LANES.map((lane, k) => (
          <path
            key={lane.x}
            data-anim={`plan-${k + 1}`}
            d={lanePath(lane.x)}
            pathLength={100}
            strokeDasharray={100}
            strokeDashoffset={0}
            stroke="var(--color-accent)"
            strokeWidth={2}
            strokeLinejoin="round"
            fill="none"
          />
        ))}
        {LANES.map((lane, k) => (
          <g key={lane.x} data-anim={`future-${k + 1}`}>
            <circle cx={lane.x} cy={80} r={7} fill={`var(--color-disc-${lane.disc})`} />
            <text
              x={lane.x}
              y={80}
              textAnchor="middle"
              dominantBaseline="central"
              fontSize={8}
              fontWeight={700}
              fontFamily={ART_MONO}
              fill={`var(--color-disc-${lane.disc}-fg)`}
            >
              {lane.disc}
            </text>
          </g>
        ))}
        <text
          x={MERGE[0]}
          y={MERGE[1] - 18}
          textAnchor="middle"
          dominantBaseline="central"
          fontSize={12}
          fontWeight={700}
          fontFamily={ART_MONO}
          fill="var(--color-ink-3)"
        >
          ?
        </text>
        <circle
          data-anim="ghost"
          cx={MERGE[0]}
          cy={MERGE[1] - 18}
          r={9}
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth={1.2}
          strokeDasharray="2.5 2"
        />
        <rect
          data-anim="choose"
          x={MERGE[0] - 14}
          y={MERGE[1] - 2}
          width={28}
          height={10}
          rx={3}
          fill="var(--color-accent-strong)"
        />
      </g>
    </svg>
  );
}
