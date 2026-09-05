/**
 * Card art for `heuristic-search/open-loop`: a plan of four columns, written
 * down before anything is dealt, and three imagined streams of discs running
 * underneath it. The playhead steps from one move to the next and the discs
 * of each stream arrive as it passes, all different — and the plan above them
 * never changes. That is the whole idea: one fixed sequence of columns, judged
 * against every stream at once, with no branch anywhere in it.
 *
 * Each plan slot is the seven columns of the board with the committed one
 * filled, so the plan is read as columns rather than as numbers.
 *
 * Server component. Motion lives in open-loop.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtDisc, type BoardGeometry } from "../board";
import "./open-loop.css";

/** The four moves of the plan, at their x centres in the frame. */
const STEPS = [88, 150, 212, 274];
/** The column the plan commits to at each step, of the board's seven. */
const PLAN = [3, 0, 5, 1];
const SLOT = { width: 56, height: 14, y: 12, cell: 8 };

/** Three imagined streams: the same four moments, dealt differently in each. */
const STREAMS = [
  { y: 60, discs: [3, 6, 1, 4] },
  { y: 98, discs: [5, 2, 7, 2] },
  { y: 136, discs: [1, 4, 3, 6] },
];

/** A one-cell geometry centred on a point, so a kit disc can be placed freely. */
function at(cx: number, cy: number, cell = 30): BoardGeometry {
  return { x: cx - cell / 2, y: cy - cell / 2, cell, cols: 1, rows: 1 };
}

export function OpenLoopArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-open-loop",
        "A fixed plan of four columns above three different imagined streams of discs, unchanged as each stream is dealt",
        props,
      )}
    >
      <g className="guides">
        {STEPS.map((cx) => (
          <line
            key={cx}
            x1={cx}
            y1={30}
            x2={cx}
            y2={152}
            stroke="var(--color-rule)"
            strokeWidth={1}
            strokeDasharray="2 3"
          />
        ))}
        {STREAMS.map((stream) => (
          <line
            key={stream.y}
            x1={58}
            y1={stream.y}
            x2={304}
            y2={stream.y}
            stroke="var(--color-rule)"
            strokeWidth={1}
          />
        ))}
      </g>

      <g className="ribbon">
        {STEPS.map((cx) => (
          <g key={cx}>
            <rect
              x={cx - SLOT.width / 2}
              y={SLOT.y}
              width={SLOT.width}
              height={SLOT.height}
              rx={2}
              fill="var(--color-cell)"
              stroke="var(--color-rule-strong)"
            />
            <path
              d={[1, 2, 3, 4, 5, 6]
                .map((tick) => `M${cx - SLOT.width / 2 + tick * SLOT.cell} ${SLOT.y}v${SLOT.height}`)
                .join("")}
              stroke="var(--color-rule)"
              strokeWidth={1}
              fill="none"
            />
          </g>
        ))}
      </g>
      <g data-anim="plan">
        {STEPS.map((cx, step) => (
          <rect
            key={cx}
            x={cx - SLOT.width / 2 + PLAN[step] * SLOT.cell + 1}
            y={SLOT.y + 1}
            width={SLOT.cell - 2}
            height={SLOT.height - 2}
            rx={1}
            fill="var(--color-accent)"
          />
        ))}
      </g>

      <rect
        data-anim="playhead"
        x={STEPS[3] - 0.8}
        y={8}
        width={1.6}
        height={148}
        fill="var(--color-accent-strong)"
        opacity={0.5}
      />

      {STEPS.map((cx, step) => (
        <g key={cx} data-anim={`step-${step + 1}`}>
          {STREAMS.map((stream) => (
            <ArtDisc
              key={stream.y}
              value={stream.discs[step]}
              col={0}
              row={0}
              g={at(cx, stream.y)}
            />
          ))}
        </g>
      ))}

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={10} y={24}>
          plan
        </text>
        <text x={10} y={102}>
          streams
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text
          x={160}
          y={172}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          one plan, every stream
        </text>
      </g>
    </svg>
  );
}
