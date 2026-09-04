/**
 * Card art for `heuristic-search/critical-risk`: a real board that is one
 * disc from the end, and the one cell that ends it. Column three already
 * stands six high with two covered discs inside it, so its top cell is the
 * last free square in the column: fill it and the next rise pushes a disc
 * off the board. On play the height line and the `critical` tag come up, that
 * cell is ringed and crossed, the ranking switches from the plain mean to the
 * mean blended with the worst 40%, and the next disc moves off the tall
 * column onto the shortest one.
 *
 * Server component. Motion lives in critical-risk.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing, BOARD, boardWidth, cellCenter, columnX } from "../board";
import "./critical-risk.css";

/**
 * Row-major from the top. Column three is six high and holds a solid and a
 * cracked gray; nothing on the board is currently in a run equal to its own
 * value, so the position is settled.
 */
const CELLS = "0000000" + "0040000" + "0083000" + "0598207" + "5221853" + "6633525" + "2258361";

/** The top cell of the tallest column: the square that ends the game. */
const KILL_COL = 2;
const KILL_ROW = 0;
/** The shortest column, and the row the next disc would rest on there. */
const SAFE_COL = 5;
const SAFE_ROW = 3;
const NEXT_VALUE = 3;

const [KILL_X, KILL_Y] = cellCenter(KILL_COL, KILL_ROW);
/** The height the trigger watches: the top of a six-high column. */
const HEIGHT_LINE = BOARD.y + BOARD.cell;
const DROP_X = columnX(SAFE_COL);

export function CriticalRiskArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-critical-risk",
        "A board one disc from ending, with the killing cell crossed out and the ranking switched to worst case",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={CELLS} />
      </ArtBoard>

      <g data-anim="tag">
        <line
          x1={BOARD.x}
          y1={HEIGHT_LINE}
          x2={BOARD.x + boardWidth()}
          y2={HEIGHT_LINE}
          stroke="var(--color-danger)"
          strokeWidth={1}
          strokeDasharray="4 3"
        />
        <rect x={156} y={32} width={7} height={7} rx={1.5} fill="var(--color-danger)" />
        <text x={169} y={39} fontFamily={ART_MONO} fontSize={10} fill="var(--color-danger)">
          critical
        </text>
      </g>

      <g data-anim="kill">
        <rect
          x={KILL_X - 8}
          y={KILL_Y - 8}
          width={16}
          height={16}
          rx={2}
          fill="none"
          stroke="var(--color-danger)"
          strokeWidth={1.6}
        />
        <path
          d={`M${KILL_X - 4},${KILL_Y - 4}l8,8M${KILL_X + 4},${KILL_Y - 4}l-8,8`}
          fill="none"
          stroke="var(--color-danger)"
          strokeWidth={1.8}
          strokeLinecap="round"
        />
      </g>

      <g data-anim="dim" opacity={0.32}>
        <rect x={156} y={54} width={148} height={22} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <text x={166} y={68} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          mean
        </text>
      </g>

      <g data-anim="switch">
        <path d="M230,79v7M226,84l4,4l4,-4" fill="none" stroke="var(--color-ink-3)" strokeWidth={1.2} strokeLinecap="round" />
      </g>

      <g data-anim="lit">
        <rect x={156} y={92} width={148} height={22} rx={4} fill="var(--color-accent-soft)" stroke="var(--color-accent)" />
        <text x={166} y={106} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-1)">
          mean + worst 40%
        </text>
      </g>

      <g data-anim="path">
        <line
          x1={DROP_X}
          y1={BOARD.y}
          x2={DROP_X}
          y2={BOARD.y + SAFE_ROW * BOARD.cell}
          stroke="var(--color-accent)"
          strokeWidth={1.5}
          strokeDasharray="3 3"
        />
        <ArtRing col={SAFE_COL} row={SAFE_ROW} />
      </g>

      <g data-anim="move">
        <ArtDisc value={NEXT_VALUE} col={SAFE_COL} row={-1} />
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={172} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          caution only at critical states
        </text>
      </g>
    </svg>
  );
}
