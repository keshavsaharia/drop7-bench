/**
 * Card art for `lifetime-objective/afterstate-net`: the board a disc leaves
 * behind, and how much longer the game runs from it. The disc drops into the
 * board on the left; the whole board is then ringed, because the network is
 * handed a position and never the column that made it; and on the right the
 * survival horizon grows, one bar per row rise the position is still alive
 * for, shortening as the rises go on.
 *
 * The bars carry no numbers. They are the shape of a prediction, not a
 * measurement of one.
 *
 * Server component. Motion lives in afterstate-net.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, boardHeight, boardWidth, type BoardGeometry } from "../board";
import "./afterstate-net.css";

/** A board small enough to leave the right half of the frame for the horizon. */
const G: BoardGeometry = { x: 8, y: 28, cell: 16, cols: 7, rows: 7 };

/** A settled mid-game position: nothing on it clears where it stands. */
const CELLS =
  "0000000" + "0000000" + "0004600" + "0603700" + "5302140" + "5132625" + "4651364";

/** The move: a 5 into the third column, which settles two above the floor. */
const DROP = { value: 5, col: 2, row: 4 };

/** The survival horizon: one bar per rise ahead, and no scale on any of them. */
const BARS = [84, 72, 60, 46, 32, 18];
const BAR = { x: 140, pitch: 29, width: 18, base: 142 };

export function AfterstateNetArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-afterstate-net",
        "A disc lands on a board, the whole board is ringed, and bars beside it grow into how many more row rises that position survives",
        props,
      )}
    >
      <ArtBoard g={G} />
      <ArtCells cells={CELLS} g={G} />
      <ArtDisc value={DROP.value} col={DROP.col} row={DROP.row} g={G} data-anim="drop" />

      <rect
        data-anim="ring"
        x={G.x - 4}
        y={G.y - 4}
        width={boardWidth(G) + 8}
        height={boardHeight(G) + 8}
        rx={6}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={1.5}
      />

      <path
        d="M127 92h8m-3-3 3 3-3 3"
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth={1.2}
        strokeLinecap="round"
        strokeLinejoin="round"
      />

      <line
        x1={134}
        y1={BAR.base}
        x2={306}
        y2={BAR.base}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
      />
      {BARS.map((height, index) => (
        <rect
          key={height}
          data-anim={`bar-${index + 1}`}
          x={BAR.x + index * BAR.pitch}
          y={BAR.base - height}
          width={BAR.width}
          height={height}
          rx={2}
          fill="var(--color-accent)"
        />
      ))}

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)" textAnchor="middle">
        <text x={64} y={158}>
          afterstate
        </text>
        <text x={222} y={158}>
          rises ahead
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text
          x={160}
          y={174}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          how long, not how much
        </text>
      </g>
    </svg>
  );
}
