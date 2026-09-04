/**
 * Heuristic evaluation card art. A seven-column board drawn as heights and a
 * three-row tally beside it. On play a scan line sweeps down the board, the
 * tallies fill in as it passes, the penalty resolves, and a marker points at
 * the column the evaluator would drop into.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./heuristic-evaluation.css";

const HEIGHTS = [3, 5, 2, 6, 4, 1, 3];
const UNIT = 18;
const FLOOR = 156;
const TALLY: { label: string; value: string; y: number }[] = [
  { label: "height", value: "6", y: 52 },
  { label: "bumps", value: "16", y: 80 },
  { label: "wells", value: "2", y: 108 },
];

export function HeuristicEvaluationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "heuristic-evaluation",
        "A board drawn as column heights, a scan line reading it, and a tally that adds up to a penalty",
        props,
      )}
    >
      <g className="board">
        <rect
          x={16}
          y={22}
          width={154}
          height={136}
          rx={4}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        {HEIGHTS.map((h, i) => (
          <rect
            key={i}
            x={20 + i * 22}
            y={FLOOR - h * UNIT}
            width={18}
            height={h * UNIT}
            rx={2}
            fill="var(--color-ink-4)"
          />
        ))}
        <line
          data-anim="scan"
          x1={18}
          y1={27}
          x2={168}
          y2={27}
          stroke="var(--color-accent)"
          strokeWidth={1.5}
          opacity={0}
        />
      </g>
      <g className="tally" fontFamily={ART_MONO}>
        {TALLY.map((row, i) => (
          <g key={row.label}>
            <text x={190} y={row.y} fontSize={11} fill="var(--color-ink-2)">
              {row.label}
            </text>
            <text
              data-anim={`val-${i + 1}`}
              x={304}
              y={row.y}
              fontSize={12}
              textAnchor="end"
              fill="var(--color-ink)"
            >
              {row.value}
            </text>
          </g>
        ))}
        <line x1={190} y1={122} x2={304} y2={122} stroke="var(--color-rule-strong)" />
        <text x={190} y={142} fontSize={11} fontWeight={700} fill="var(--color-ink-2)">
          penalty
        </text>
        <text
          data-anim="sum"
          x={304}
          y={142}
          fontSize={13}
          fontWeight={700}
          textAnchor="end"
          fill="var(--color-accent)"
        >
          24
        </text>
      </g>
      <g className="tart-final" data-anim="marker">
        <path d="M133 168l6-7 6 7z" fill="var(--color-accent)" />
      </g>
    </svg>
  );
}
