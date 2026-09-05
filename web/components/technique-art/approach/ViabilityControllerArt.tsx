/**
 * Card art for `constructive-reservoir/viability-controller`: the boundary and
 * the correction. The gauge on the right marks the region the controller keeps
 * the board inside; as discs land the load climbs across the line into "too
 * full", the mode flips from charging to release, the played 4 takes the
 * column's pair of 4s with it, and the load settles back inside. No number is
 * scored on the way: the mode is chosen first, then the move.
 *
 * Server component. Motion lives in viability-controller.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtScore, type BoardGeometry } from "../board";
import "./viability-controller.css";

const G: BoardGeometry = { x: 14, y: 32, cell: 16, cols: 7, rows: 7 };

/** Everything on the board that neither arrives, clears nor falls. */
const BASE =
  "0000000" +
  "0000000" +
  "0000000" +
  "0075000" +
  "0561000" +
  "6152016" +
  "5426665";

const RELEASE_COL = 4;
const GAUGE_X = 160;
const GAUGE_W = 28;
const GAUGE_TOP = 32;
const GAUGE_BOTTOM = 146;
const EDGE_Y = 86;
const LABEL_X = 196;

export function ViabilityControllerArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-viability-controller",
        "A gauge with a viable region; the board's load crosses the line, a release clears a pair of discs, and the load settles back inside",
        props,
      )}
    >
      <ArtBoard g={G} />
      <ArtCells cells={BASE} g={G} />
      <ArtDisc value={6} col={6} row={4} g={G} data-anim="arrive" />
      <ArtDisc value={4} col={RELEASE_COL} row={5} g={G} data-anim="spent" opacity={0} />
      <ArtDisc value={4} col={RELEASE_COL} row={3} g={G} data-anim="played" opacity={0} />
      <ArtDisc value={5} col={RELEASE_COL} row={5} g={G} data-anim="settle" />
      <ArtScore depth={1} col={RELEASE_COL} row={4} g={G} data-anim="wave" />

      <rect
        x={GAUGE_X}
        y={GAUGE_TOP}
        width={GAUGE_W}
        height={GAUGE_BOTTOM - GAUGE_TOP}
        rx={4}
        fill="var(--color-raised)"
        stroke="var(--color-rule-strong)"
      />
      <rect
        x={GAUGE_X}
        y={EDGE_Y}
        width={GAUGE_W}
        height={GAUGE_BOTTOM - EDGE_Y}
        rx={4}
        fill="var(--color-accent-soft)"
        stroke="var(--color-accent-strong)"
      />
      <line
        x1={GAUGE_X - 4}
        y1={EDGE_Y}
        x2={GAUGE_X + GAUGE_W + 4}
        y2={EDGE_Y}
        stroke="var(--color-highlight)"
        strokeWidth={1.4}
        strokeDasharray="3 3"
      />
      <line
        data-anim="alarm"
        x1={GAUGE_X - 4}
        y1={EDGE_Y}
        x2={GAUGE_X + GAUGE_W + 4}
        y2={EDGE_Y}
        stroke="var(--color-highlight)"
        strokeWidth={3}
        opacity={0}
      />
      <rect
        data-anim="load"
        x={GAUGE_X - 4}
        y={112}
        width={GAUGE_W + 8}
        height={8}
        rx={4}
        fill="var(--color-accent)"
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={LABEL_X} y={58}>
          too full
        </text>
        <text x={LABEL_X} y={126}>
          viable
        </text>
      </g>
      <g className="tart-final" data-anim="rest" fontFamily={ART_MONO} fontSize={9}>
        <text x={LABEL_X} y={100} fill="var(--color-highlight)">
          release
        </text>
        <text x={14} y={166} fill="var(--color-ink-2)">
          drift out, then steer back
        </text>
      </g>
    </svg>
  );
}
