/**
 * Card art for `heuristic-search/edge-priority`: the two outer columns are not
 * ordinary columns. The board is drawn with column 1 and column 7 banded off
 * as their own territory, the covered disc at the top of each one ringed as
 * the thing the term charges for, and the drop preference sliding outward to
 * the edge. Beside the board, two strips say why: a covered disc in the middle
 * can be reached from both sides, one against the wall from only one.
 *
 * The position is a real one: no disc on it is standing in a run equal to its
 * own value, so nothing on this board would clear on its own.
 *
 * Server component. Motion lives in edge-priority.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import {
  ArtBoard,
  ArtCells,
  ArtGray,
  ArtRing,
  BOARD,
  boardHeight,
  columnX,
  type BoardGeometry,
} from "../board";
import "./edge-priority.css";

/** A settled mid-game board: three covered discs at the same altitude. */
const CELLS = "0000000" + "0000000" + "0000000" + "8008008" + "5066506" + "6552652" + "2645265";

/** Row the three covered discs sit on. */
const COVER_ROW = 3;
/** The columns the term treats differently, and the one the drop moves away from. */
const EDGE_COLUMNS = [0, 6];
const FROM_COLUMN = 3;
const TO_COLUMN = 6;

/** Three cells apiece: one covered disc and the sides a clearing disc can arrive from. */
const MIDDLE_STRIP: BoardGeometry = { x: 200, y: 46, cell: 20, cols: 3, rows: 1 };
const EDGE_STRIP: BoardGeometry = { ...MIDDLE_STRIP, y: 104 };

/** An arrow along a strip's centre line, pointing at the covered disc. */
function reachArrow(g: BoardGeometry, fromX: number, toX: number): string {
  const y = g.y + g.cell / 2;
  const head = toX > fromX ? -4 : 4;
  return `M${fromX} ${y}H${toX}M${toX + head} ${y - 3.2}L${toX} ${y}L${toX + head} ${y + 3.2}`;
}

export function EdgePriorityArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-edge-priority",
        "A board whose two outer columns are banded off, their covered discs ringed, and the drop preference sliding outward",
        props,
      )}
    >
      <ArtBoard />
      <g data-anim="bands">
        {EDGE_COLUMNS.map((col) => (
          <rect
            key={col}
            x={BOARD.x + col * BOARD.cell}
            y={BOARD.y}
            width={BOARD.cell}
            height={boardHeight()}
            fill="var(--color-accent-soft)"
          />
        ))}
      </g>
      <ArtCells cells={CELLS} />
      <g data-anim="rings">
        {EDGE_COLUMNS.map((col) => (
          <ArtRing key={col} col={col} row={COVER_ROW} />
        ))}
      </g>
      <path
        data-anim="marker"
        d={`M${columnX(TO_COLUMN) - 6} 167L${columnX(TO_COLUMN)} 158L${columnX(TO_COLUMN) + 6} 167Z`}
        fill="var(--color-accent)"
      />

      <g className="reach" fontFamily={ART_MONO} fontSize={10} fill="var(--color-ink-2)">
        <text x={152} y={60}>
          middle
        </text>
        <ArtBoard g={MIDDLE_STRIP} />
        <ArtGray col={1} row={0} g={MIDDLE_STRIP} />
        <text x={152} y={118}>
          edge
        </text>
        <ArtBoard g={EDGE_STRIP} />
        <ArtGray col={2} row={0} g={EDGE_STRIP} />
        <line
          x1={EDGE_STRIP.x + 3 * EDGE_STRIP.cell}
          y1={EDGE_STRIP.y - 1}
          x2={EDGE_STRIP.x + 3 * EDGE_STRIP.cell}
          y2={EDGE_STRIP.y + EDGE_STRIP.cell + 1}
          stroke="var(--color-ink-3)"
          strokeWidth={3}
        />
        <g data-anim="arrows" fill="none" stroke="var(--color-accent)" strokeWidth={1.4}>
          <path d={reachArrow(MIDDLE_STRIP, 204, 218)} />
          <path d={reachArrow(MIDDLE_STRIP, 256, 242)} />
          <path d={reachArrow(EDGE_STRIP, 204, 238)} />
        </g>
      </g>

      <g className="tart-final" data-anim="caption">
        <text
          x={columnX(FROM_COLUMN)}
          y={176}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          the outer columns cost more
        </text>
      </g>
    </svg>
  );
}
