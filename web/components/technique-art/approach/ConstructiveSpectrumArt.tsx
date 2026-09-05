/**
 * Card art for `constructive-reservoir/constructive-spectrum`: the dial between
 * cashing in now and building toward a described board. The needle runs from
 * "score now" to "target shape" and the board underneath it changes from the
 * flat wall with its covered discs buried to the target picture — columns at a
 * variety of heights, a covered disc still reachable on top, and two stored 6s
 * in one column that fire on the same trigger.
 *
 * Server component. Motion lives in constructive-spectrum.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, type BoardGeometry } from "../board";
import "./constructive-spectrum.css";

/** A 7x7 board centred under the dial. */
const G: BoardGeometry = { x: 100, y: 34, cell: 17, cols: 7, rows: 7 };

/** Cashing in: one flat wall, every column the same height, the grays buried. */
const FLAT =
  "0000000" +
  "0000000" +
  "0000000" +
  "0000000" +
  "4564564" +
  "6456456" +
  "8888888";

/**
 * The target picture: heights 1,3,2,3,1,1,4; the covered disc sits on top of
 * the tall column where it can still be opened; the two 6s under it share one
 * trigger, because the same arriving disc in the same column fires both.
 */
const TARGET =
  "0000000" +
  "0000000" +
  "0000000" +
  "0000008" +
  "0204006" +
  "0145002" +
  "4512656";

const TRACK_LEFT = 44;
const TRACK_RIGHT = 276;
const TRACK_Y = 22;

export function ConstructiveSpectrumArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-constructive-spectrum",
        "A dial running from scoring now to a target board shape, with the board changing from a flat wall to spread-out columns",
        props,
      )}
    >
      <g stroke="var(--color-rule-strong)" strokeWidth={1.4} strokeLinecap="round">
        <line x1={TRACK_LEFT} y1={TRACK_Y} x2={TRACK_RIGHT} y2={TRACK_Y} />
        <line x1={TRACK_LEFT} y1={TRACK_Y - 5} x2={TRACK_LEFT} y2={TRACK_Y + 5} />
        <line x1={TRACK_RIGHT} y1={TRACK_Y - 5} x2={TRACK_RIGHT} y2={TRACK_Y + 5} />
      </g>
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={TRACK_LEFT} y={13}>
          score now
        </text>
        <text x={TRACK_RIGHT} y={13} textAnchor="end">
          target shape
        </text>
      </g>
      <circle
        data-anim="needle"
        cx={TRACK_RIGHT}
        cy={TRACK_Y}
        r={5}
        fill="var(--color-accent)"
        stroke="var(--color-bg)"
        strokeWidth={1.5}
      />
      <ArtBoard g={G} />
      <g data-anim="flat" opacity={0}>
        <ArtCells cells={FLAT} g={G} />
      </g>
      <g data-anim="target">
        <ArtCells cells={TARGET} g={G} />
      </g>
      <g data-anim="trigger">
        <path
          d="M224 110.5h4V144.5h-4"
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.4}
        />
        <text x={233} y={131} fontFamily={ART_MONO} fontSize={9} fill="var(--color-highlight)">
          one trigger
        </text>
      </g>
      <g className="tart-final" data-anim="rest">
        <text
          x={160}
          y={170}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          build the shape now, cash it later
        </text>
      </g>
    </svg>
  );
}
