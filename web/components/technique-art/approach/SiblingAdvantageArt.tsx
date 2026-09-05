/**
 * Card art for `value-policy-learning/sibling-advantage`: one root board, the
 * same disc hovering over all seven of its legal columns, and one tape of
 * future discs that every sibling is continued on. The seven bars then grow
 * out of a shared zero line — above it and below it — because what is learned
 * is each sibling's gap from its siblings, not a value of its own. The tallest
 * gap is ringed at the end.
 *
 * Nothing here is a measurement: the bars have no scale and no numbers, and
 * the zero line is the root, not a result.
 *
 * Server component. Motion lives in sibling-advantage.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, type BoardGeometry } from "../board";
import "./sibling-advantage.css";

/** The root position, small enough to leave the right half for the siblings. */
const G: BoardGeometry = { x: 8, y: 36, cell: 16, cols: 7, rows: 7 };

/** A settled root: nothing on it clears where it stands. */
const CELLS =
  "0000000" + "0000000" + "0006000" + "0602030" + "0267120" + "4351625" + "3642516";

/** The one disc to place, drawn over each of the seven columns it could go in. */
const NEXT_DISC = 4;
const COLUMNS = [0, 1, 2, 3, 4, 5, 6];

/** The shared continuation tape: the same discs behind every sibling. */
const TAPE: BoardGeometry = { x: 183, y: 11, cell: 22, cols: 4, rows: 1 };
const TAPE_DISCS = [3, 6, 1, 4];

/**
 * Each sibling's gap from its siblings, in user units: above the line is
 * better than the rest of the family, below it is worse. The extremes are
 * equal and opposite, so the group's own box is centred on the zero line and
 * one keyframe can grow all seven out of it.
 */
const GAPS = [18, -22, 44, 6, -44, 30, -10];
const ZERO = 92;
const COLUMN = { x: 132, pitch: 25, width: 15 };
/** The sibling with the widest gap, ringed once the family is ranked. */
const BEST = 2;

export function SiblingAdvantageArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-sibling-advantage",
        "One root board with the same disc over all seven legal columns, one shared tape of future discs, and seven bars measuring each sibling against the others",
        props,
      )}
    >
      <ArtBoard g={G} />
      <ArtCells cells={CELLS} g={G} />
      <g data-anim="siblings" opacity={0.6}>
        {COLUMNS.map((col) => (
          <ArtDisc key={col} value={NEXT_DISC} col={col} row={-1} g={G} />
        ))}
      </g>

      <g data-anim="tape">
        <ArtBoard g={TAPE} />
        {TAPE_DISCS.map((value, index) => (
          <ArtDisc key={value} value={value} col={index} row={0} g={TAPE} />
        ))}
      </g>
      <path
        d="M227 34V40M132 40h165"
        fill="none"
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        strokeDasharray="3 2.5"
      />

      <line x1={126} y1={ZERO} x2={303} y2={ZERO} stroke="var(--color-rule-strong)" strokeWidth={1} />
      <g data-anim="bars">
        {GAPS.map((gap, index) => (
          <rect
            key={gap}
            x={COLUMN.x + index * COLUMN.pitch}
            y={gap > 0 ? ZERO - gap : ZERO}
            width={COLUMN.width}
            height={Math.abs(gap)}
            rx={2}
            fill={gap > 0 ? "var(--color-accent)" : "var(--color-ink-3)"}
          />
        ))}
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={177} y={26} textAnchor="end">
          one tape
        </text>
        <text x={64} y={160} textAnchor="middle">
          one root
        </text>
        <text x={214} y={160} textAnchor="middle">
          seven siblings
        </text>
      </g>

      <g className="tart-final" data-anim="rank">
        <rect
          x={COLUMN.x + BEST * COLUMN.pitch - 3}
          y={ZERO - GAPS[BEST] - 3}
          width={COLUMN.width + 6}
          height={GAPS[BEST] + 6}
          rx={3}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
        <text
          x={160}
          y={174}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          the gap, not the value
        </text>
      </g>
    </svg>
  );
}
