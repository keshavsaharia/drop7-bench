/**
 * Card art for `lifetime-objective/rollout-veto-17k`: an answer that changes
 * after it has been given. The four-move search has already put the next disc
 * over its column; only then does the long look-ahead finish — seven imagined
 * futures played twenty-five moves out — and when all four of its conditions
 * pass, the disc slides to a different column and the ring moves with it. The
 * board is tall, which is the condition that routes the long look-ahead at all.
 *
 * Server component. Motion lives in rollout-veto-17k.css (transform, opacity
 * and stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing, BOARD, columnX } from "../board";
import "./rollout-veto-17k.css";

const CELLS = "0000000" + "0000000" + "0010000" + "0080000" + "0460050" + "3529016" + "7243561";

/** The visible next disc. */
const NEXT = 2;
/** The column the four-move search picks, and the one the long look-ahead takes. */
const INCUMBENT = { col: 6, row: 4 };
const CHALLENGER = { col: 0, row: 4 };
const TRAVEL = columnX(CHALLENGER.col) - columnX(INCUMBENT.col);

/** The tall column that routes the long look-ahead: five discs, rows 2 to 6. */
const TALL = { col: 2, top: 2 };

/** Seven imagined futures, each played twenty-five moves out. */
const FUTURES = [44, 53, 62, 71, 80, 89, 98];
const FUTURE_X = 158;
const HORIZON = 302;

/** The four conditions a challenger has to pass before it may overrule. */
const TICKS = [162, 192, 222, 252];

export function RolloutVeto17kArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-rollout-veto-17k",
        "A tall board whose chosen column changes after seven long look-ahead futures pass four conditions",
        props,
      )}
    >
      <ArtBoard />
      <rect
        x={BOARD.x + TALL.col * BOARD.cell}
        y={BOARD.y + TALL.top * BOARD.cell}
        width={BOARD.cell}
        height={(BOARD.rows - TALL.top) * BOARD.cell}
        rx="2"
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth="1.2"
        strokeDasharray="3 3"
      />
      <ArtCells cells={CELLS} />

      <g className="kept" data-anim="kept" opacity="0">
        <ArtDisc value={NEXT} col={INCUMBENT.col} row={INCUMBENT.row} opacity={0.45} />
        <ArtRing col={INCUMBENT.col} row={INCUMBENT.row} />
      </g>
      <g className="taken" data-anim="taken">
        <ArtDisc value={NEXT} col={CHALLENGER.col} row={CHALLENGER.row} opacity={0.45} />
        <ArtRing col={CHALLENGER.col} row={CHALLENGER.row} />
      </g>
      <g className="pointer" data-anim="pointer" transform={`translate(${TRAVEL} 0)`}>
        <ArtDisc value={NEXT} col={INCUMBENT.col} row={-1} />
      </g>

      <g className="futures">
        {FUTURES.map((y) => (
          <line
            key={y}
            className="future"
            data-anim="future"
            x1={FUTURE_X}
            y1={y}
            x2={HORIZON}
            y2={y}
            pathLength={1}
            strokeDasharray="1"
            stroke="var(--color-ink-3)"
            strokeWidth="1"
          />
        ))}
        <line x1={FUTURE_X - 4} y1="40" x2={FUTURE_X - 4} y2="102" stroke="var(--color-ink-4)" strokeWidth="1.4" />
        <line x1={HORIZON + 3} y1="40" x2={HORIZON + 3} y2="102" stroke="var(--color-ink-4)" strokeWidth="1.4" />
      </g>

      <g className="conditions" fill="none" stroke="var(--color-status-completed)" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
        {TICKS.map((x, index) => (
          <path key={x} data-anim={`tick-${index}`} d="M-4,0l3,4l6,-8" transform={`translate(${x} 132)`} />
        ))}
      </g>

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="152" y="16">
          tall board: routed
        </text>
        <text x="152" y="28">
          25 moves · 7 futures
        </text>
        <text x="152" y="116">
          all four conditions
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          the long view overrules the search
        </text>
      </g>
    </svg>
  );
}
