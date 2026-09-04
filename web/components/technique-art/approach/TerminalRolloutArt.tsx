/**
 * Card art for `terminal-policy-iteration/terminal-rollout`: the version of
 * the family's idea with no horizon in it. Two challengers are screened out of
 * the seven columns, and every imagined future is played past the line where
 * the other programs stop, on to the position that ends the game. On play the
 * futures race out through the horizon and die at their own lengths, the board
 * fills to the top until the next disc has nowhere to land, and the clock goes
 * round once for the whole of it.
 *
 * Server component. Motion lives in terminal-rollout.css (stroke-dashoffset,
 * transform and opacity only); the markup is the resting frame.
 */
import { ArtCells, ArtDisc, type BoardGeometry } from "../board";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./terminal-rollout.css";

/** The board sits right, with room above it for the disc that cannot land. */
const G: BoardGeometry = { x: 192, y: 44, cell: 15, cols: 7, rows: 7 };

const ROOT_X = 12;
const ROOT_Y = 92;
const FORK_X = 40;
const THREAD_X = 46;
const HORIZON_X = 92;
const CLOCK = { x: 18, y: 160, r: 10 };

/** Each future: its row, where its game ended, and its stagger group. */
const THREADS = [
  { y: 58, end: 132, step: "a" },
  { y: 66, end: 172, step: "b" },
  { y: 74, end: 108, step: "c" },
  { y: 110, end: 154, step: "b" },
  { y: 118, end: 116, step: "c" },
  { y: 126, end: 178, step: "a" },
] as const;

/**
 * The jammed board this run ends on, row-major from the top: column four is
 * filled to the ceiling, so the next disc has nowhere to go. No numbered disc
 * sits in a run of its own length, so nothing here would clear.
 */
const ROWS = [
  "0002000",
  "0041005",
  "0821704",
  "2222638",
  "2148333",
  "2422214",
  "1131811",
];
const NO_ROW = "0000000";

/** The same board masked down to one band of rows, so it can fill in stages. */
function band(from: number, to: number): string {
  return ROWS.map((row, index) => (index >= from && index <= to ? row : NO_ROW)).join("");
}

export function TerminalRolloutArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-terminal-rollout",
        "Two challengers, each future played past the horizon line until the board jams and the game ends",
        props,
      )}
    >
      <g className="fork" stroke="var(--color-ink-3)" strokeWidth="1.2" fill="none">
        <path d={`M${ROOT_X},${ROOT_Y}L${FORK_X},66M${ROOT_X},${ROOT_Y}L${FORK_X},118`} />
      </g>
      <circle cx={ROOT_X} cy={ROOT_Y} r="3.6" fill="var(--color-ink-2)" />
      <circle cx={FORK_X} cy="66" r="4" fill="var(--color-accent)" />
      <circle cx={FORK_X} cy="118" r="4" fill="var(--color-accent)" />

      <line
        x1={HORIZON_X}
        y1="48"
        x2={HORIZON_X}
        y2="140"
        stroke="var(--color-ink-4)"
        strokeWidth="1"
        strokeDasharray="3 4"
      />

      <g className="threads">
        {THREADS.map((thread) => (
          <line
            key={thread.y}
            data-anim={`thread-${thread.step}`}
            x1={THREAD_X}
            y1={thread.y}
            x2={thread.end}
            y2={thread.y}
            pathLength={1}
            strokeDasharray="1"
            stroke="var(--color-ink-3)"
            strokeWidth="1.2"
          />
        ))}
      </g>
      <g className="ends" fill="none" stroke="var(--color-series-8)" strokeWidth="1.3">
        {THREADS.map((thread) => (
          <path
            key={thread.y}
            data-anim={`cross-${thread.step}`}
            d="M-3,-3l6,6M3,-3l-6,6"
            transform={`translate(${thread.end + 5} ${thread.y})`}
          />
        ))}
      </g>

      <ArtDisc value={6} col={3} row={-1} g={G} />
      <g className="board">
        <rect
          x={G.x}
          y={G.y}
          width={G.cols * G.cell}
          height={G.rows * G.cell}
          rx="4"
          fill="var(--color-cell)"
          stroke="var(--color-rule-strong)"
        />
        <g data-anim="fill-a">
          <ArtCells cells={band(5, 6)} g={G} />
        </g>
        <g data-anim="fill-b">
          <ArtCells cells={band(3, 4)} g={G} />
        </g>
        <g data-anim="fill-c">
          <ArtCells cells={band(0, 2)} g={G} />
        </g>
      </g>

      <g className="clock" fill="none" stroke="var(--color-ink-4)" strokeWidth="1.2">
        <circle cx={CLOCK.x} cy={CLOCK.y} r={CLOCK.r} fill="var(--color-raised)" />
        <line
          className="hand"
          data-anim="hand"
          x1={CLOCK.x}
          y1={CLOCK.y}
          x2={CLOCK.x}
          y2={CLOCK.y - CLOCK.r + 2}
          stroke="var(--color-ink-2)"
          strokeWidth="1.4"
        />
      </g>

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="20">
          2 challengers
        </text>
        <text x={HORIZON_X} y="156" textAnchor="middle">
          horizon
        </text>
      </g>

      <g className="tart-final" data-anim="final">
        <path
          d="M-5,-5l10,10M5,-5l-10,10"
          transform={`translate(${G.x + 3.5 * G.cell} ${G.y - G.cell / 2})`}
          fill="none"
          stroke="var(--color-series-8)"
          strokeWidth="1.6"
        />
        <text x="40" y="172" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          every future runs to game over
        </text>
      </g>
    </svg>
  );
}
