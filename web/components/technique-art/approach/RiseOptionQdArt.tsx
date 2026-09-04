/**
 * Card art for `constructive-reservoir/rise-option-qd`: the archive, not the
 * champion. One option is a target height for each of the seven columns, held
 * for a whole rise cycle; mutating it moves it to a different behaviour, and
 * the behaviour decides which of the sixty-four archive cells it is filed in.
 * On play the option mutates, travels into its cell, and the archive fills in
 * around it — one surviving plan per kind, with an improved plan replacing the
 * one already in its cell.
 *
 * Server component. Motion lives in rise-option-qd.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./rise-option-qd.css";

const BAR_BASE = 126;
const BAR_X = 18;
const BAR_PITCH = 10;
const BAR_WIDTH = 8;
/** The option before it is mutated, and after: seven target column heights. */
const PROFILE_BEFORE = [18, 36, 27, 45, 18, 36, 27];
const PROFILE_AFTER = [36, 18, 45, 27, 54, 18, 36];

/** Four bands of the third descriptor, each a 4x4 grid of the other two. */
const CELL = 13;
const PANELS: readonly (readonly [number, number])[] = [
  [120, 40],
  [186, 40],
  [120, 106],
  [186, 106],
];
const PANEL_SIDE = 4 * CELL;
const PANEL_GRID = [1, 2, 3]
  .flatMap((i) => [`M${i * CELL},0v${PANEL_SIDE}`, `M0,${i * CELL}h${PANEL_SIDE}`])
  .join("");

type Slot = readonly [number, number, number];
/** The option the marker carries in, then three later rounds of insertions. */
const INSERTED: Slot = [1, 1, 2];
const ROUND_B: readonly Slot[] = [
  [0, 1, 0],
  [0, 3, 1],
  [1, 0, 0],
  [2, 1, 1],
];
const ROUND_C: readonly Slot[] = [
  [0, 0, 1],
  [0, 2, 2],
  [1, 2, 1],
  [1, 3, 3],
  [2, 0, 2],
  [2, 3, 0],
  [3, 1, 1],
  [3, 2, 0],
];
const ROUND_D: readonly Slot[] = [
  [0, 1, 3],
  [1, 2, 3],
  [2, 2, 2],
  [3, 0, 3],
  [3, 3, 2],
  [3, 0, 1],
];
/** The cell whose occupant is beaten by a better plan of the same kind. */
const REPLACED: Slot = [0, 1, 0];

function slotXy([panel, i, j]: Slot): [number, number] {
  const [px, py] = PANELS[panel];
  return [px + i * CELL, py + j * CELL];
}

function Filled({ slot, fill }: { slot: Slot; fill: string }) {
  const [x, y] = slotXy(slot);
  return <rect x={x + 1} y={y + 1} width={CELL - 2} height={CELL - 2} rx={2} fill={fill} />;
}

function Round({ slots, anim }: { slots: readonly Slot[]; anim: string }) {
  return (
    <g data-anim={anim}>
      {slots.map((slot) => (
        <Filled key={slot.join("-")} slot={slot} fill="var(--color-accent-strong)" />
      ))}
    </g>
  );
}

function Profile({ heights, fill }: { heights: readonly number[]; fill: string }) {
  return (
    <>
      {heights.map((height, index) => (
        <rect
          key={index}
          x={BAR_X + index * BAR_PITCH}
          y={BAR_BASE - height}
          width={BAR_WIDTH}
          height={height}
          rx={1.5}
          fill={fill}
        />
      ))}
    </>
  );
}

const [TARGET_X, TARGET_Y] = slotXy(INSERTED);
const MARKER_X = 52;
const MARKER_Y = 66;

export function RiseOptionQdArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-rise-option-qd",
        "A seven-column target height profile filed into one cell of a sixty-four cell archive, which then fills in",
        props,
      )}
    >
      <g className="tart-final" data-anim="rest">
        <text x={120} y={28} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          a shelf, not a champion
        </text>
      </g>
      <line
        x1={BAR_X}
        y1={BAR_BASE}
        x2={BAR_X + 6 * BAR_PITCH + BAR_WIDTH}
        y2={BAR_BASE}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
      />
      <g data-anim="before" opacity={0}>
        <Profile heights={PROFILE_BEFORE} fill="var(--color-ink-4)" />
      </g>
      <g data-anim="after">
        <Profile heights={PROFILE_AFTER} fill="var(--color-accent-strong)" />
      </g>
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={16} y={142}>
          an option:
        </text>
        <text x={16} y={154}>
          7 target heights
        </text>
        <text x={120} y={172}>
          spread / release / edge
        </text>
      </g>
      {PANELS.map(([px, py]) => (
        <g key={`${px}-${py}`}>
          <rect
            x={px}
            y={py}
            width={PANEL_SIDE}
            height={PANEL_SIDE}
            rx={3}
            fill="var(--color-cell)"
            stroke="var(--color-rule-strong)"
          />
          <path
            d={PANEL_GRID}
            transform={`translate(${px} ${py})`}
            fill="none"
            stroke="var(--color-rule)"
            strokeWidth={1}
          />
        </g>
      ))}
      <g data-anim="insert">
        <Filled slot={INSERTED} fill="var(--color-accent-strong)" />
      </g>
      <Round slots={ROUND_B} anim="round-b" />
      <Round slots={ROUND_C} anim="round-c" />
      <Round slots={ROUND_D} anim="round-d" />
      <g data-anim="replace">
        <Filled slot={REPLACED} fill="var(--color-accent)" />
      </g>
      <circle
        data-anim="marker"
        cx={MARKER_X}
        cy={MARKER_Y}
        r={4}
        fill="var(--color-accent)"
        opacity={0}
      />
      <path
        data-anim="trail"
        d={`M${MARKER_X} ${MARKER_Y}L${TARGET_X + CELL / 2} ${TARGET_Y + CELL / 2}`}
        fill="none"
        stroke="var(--color-accent-strong)"
        strokeWidth={1}
        strokeDasharray="3 4"
        opacity={0}
      />
    </svg>
  );
}
