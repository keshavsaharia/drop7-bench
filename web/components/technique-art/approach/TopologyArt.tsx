/**
 * Card art for `oracle-curriculum/topology`: the board read as a shape. The
 * occupied cells are drawn as one solid silhouette with the numbered discs
 * sitting on top of it; a scan line passes down the board, the numerals go
 * out with it, and what is left is the arrangement — the deep single-column
 * pocket, the dead block of buried covers and low numbers, and the cracked
 * cover near the surface. Nothing the model reads is a value.
 *
 * Server component. Motion lives in topology.css (transform and opacity
 * only); the markup is the resting frame, which is the silhouette and its
 * three marks with no numbers on the board.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing, BOARD, boardHeight, boardWidth } from "../board";
import "./topology.css";

/**
 * A settled board, row-major from the top: column heights 4, 6, 2, 6, 3, 5, 1
 * with two solid covers buried in column 1, one in column 3 and a cracked
 * cover at the top of column 0. No disc's value equals its row or column run,
 * so nothing on it is waiting to fire.
 */
const CELLS = "0000000" + "0402000" + "0707040" + "9504020" + "5805760" + "1818210" + "3241565";

const LEFT = BOARD.x;
const TOP = BOARD.y;
const CELL = BOARD.cell;
/** Column 2 is two high between two columns of six: a deep single-column well. */
const POCKET = { x: LEFT + 2 * CELL, y: TOP, width: CELL, height: 5 * CELL };
/** Columns 0 and 1 below row 4: the buried covers and the low numbers under them. */
const DEAD = { x: LEFT, y: TOP + 4 * CELL, width: 2 * CELL, height: 3 * CELL };
/** Four 45-degree strokes across the dead block, drawn corner to corner. */
const HATCH = [
  `M${DEAD.x},${DEAD.y + 18}L${DEAD.x + 36},${DEAD.y + 54}`,
  `M${DEAD.x},${DEAD.y}L${DEAD.x + 36},${DEAD.y + 36}`,
  `M${DEAD.x + 18},${DEAD.y}L${DEAD.x + 36},${DEAD.y + 18}`,
  `M${DEAD.x},${DEAD.y + 36}L${DEAD.x + 18},${DEAD.y + 54}`,
].join("");

const LEGEND_X = 158;
const LEGEND_TEXT_X = 180;
const LEGEND_Y = [50, 82, 114];

const FILLED = [...CELLS]
  .map((character, index) => ({ value: Number(character), index }))
  .filter(({ value }) => value !== 0);

export function TopologyArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-topology",
        "A Drop7 board whose numbers fade away, leaving the shape of the occupied cells with its pocket, its dead block and its cover marked",
        props,
      )}
    >
      <ArtBoard />
      <g className="shape">
        {FILLED.map(({ index }) => (
          <rect
            key={index}
            x={LEFT + (index % BOARD.cols) * CELL}
            y={TOP + Math.floor(index / BOARD.cols) * CELL}
            width={CELL}
            height={CELL}
            fill="var(--color-ink-4)"
          />
        ))}
      </g>
      <g className="values" data-anim="values" opacity={0}>
        <ArtCells cells={CELLS} />
      </g>
      <line
        className="scan"
        data-anim="scan"
        x1={LEFT}
        y1={TOP + 0.5}
        x2={LEFT + boardWidth()}
        y2={TOP + 0.5}
        stroke="var(--color-highlight)"
        strokeWidth={1.6}
        opacity={0}
      />
      <g className="marks" data-anim="marks">
        <rect
          x={POCKET.x}
          y={POCKET.y}
          width={POCKET.width}
          height={POCKET.height}
          rx={2}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.4}
          strokeDasharray="4 3"
        />
        <rect
          x={DEAD.x}
          y={DEAD.y}
          width={DEAD.width}
          height={DEAD.height}
          fill="var(--color-ink-2)"
          fillOpacity={0.16}
          stroke="var(--color-ink-2)"
          strokeWidth={1}
        />
        <path d={HATCH} stroke="var(--color-ink-2)" strokeWidth={1} fill="none" />
        <ArtRing col={0} row={3} />
      </g>
      <g className="legend">
        <rect
          x={LEGEND_X}
          y={LEGEND_Y[0]}
          width={16}
          height={12}
          rx={2}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.4}
          strokeDasharray="4 3"
        />
        <rect
          x={LEGEND_X}
          y={LEGEND_Y[1]}
          width={16}
          height={12}
          fill="var(--color-ink-2)"
          fillOpacity={0.16}
          stroke="var(--color-ink-2)"
          strokeWidth={1}
        />
        <rect
          x={LEGEND_X}
          y={LEGEND_Y[2]}
          width={16}
          height={12}
          rx={2}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
        <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          <text x={LEGEND_TEXT_X} y={LEGEND_Y[0] + 9}>
            pocket
          </text>
          <text x={LEGEND_TEXT_X} y={LEGEND_Y[1] + 9}>
            dead
          </text>
          <text x={LEGEND_TEXT_X} y={LEGEND_Y[2] + 9}>
            cover
          </text>
        </g>
      </g>
      <g className="tart-final" data-anim="caption">
        <text
          x={LEFT + boardWidth() / 2}
          y={TOP + boardHeight() + 18}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          shape, not numbers
        </text>
      </g>
    </svg>
  );
}
