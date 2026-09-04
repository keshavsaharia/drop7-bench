/**
 * Card art for the Rust engine's parity replay: one tape of discs feeds two
 * identical boards, the reference and the port. Each move lands in both, a
 * check appears when the two records agree, and the matched-move counter
 * advances (drawn as swapped labels, never as changing text).
 *
 * Server component. Motion lives in engine-rust.css (transform and opacity
 * only); the markup is the resting frame after three matched moves.
 */
import "./engine-rust.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const CELL = 12;
const BOARD_Y = 74;
const LEFT_X = 58;
const RIGHT_X = 202;
const COLS = 5;
const ROWS = 4;

const TAPE = [3, 7, 2, 5, 4] as const;
const TAPE_X = (index: number) => 80 + index * 40;

/** Three moves: disc value, column, and the row it settles in. */
const MOVES: ReadonlyArray<readonly [number, number, number]> = [
  [3, 1, 3],
  [7, 3, 3],
  [2, 1, 2],
];

const COUNTS = ["0 matched", "1 matched", "2 matched", "3 matched"] as const;

function gridPath(x: number, y: number): string {
  const parts = [`M${x},${y} h${COLS * CELL} v${ROWS * CELL} h${-COLS * CELL} z`];
  for (let c = 1; c < COLS; c++) parts.push(`M${x + c * CELL},${y} v${ROWS * CELL}`);
  for (let r = 1; r < ROWS; r++) parts.push(`M${x},${y + r * CELL} h${COLS * CELL}`);
  return parts.join(" ");
}

function centre(boardX: number, col: number, row: number): [number, number] {
  return [boardX + col * CELL + CELL / 2, BOARD_Y + row * CELL + CELL / 2];
}

export function EngineRustArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--engine-rust", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Rust engine: one disc tape replayed on two boards, checked move by move"}
    >
      <g className="tape">
        <line x1="60" y1="30" x2="260" y2="30" stroke="var(--color-rule-strong)" strokeWidth="1.5" />
        {TAPE.map((value, index) => (
          <g key={index}>
            <circle cx={TAPE_X(index)} cy="30" r="9" fill={`var(--color-disc-${value})`} />
            <text
              x={TAPE_X(index)}
              y="30.5"
              textAnchor="middle"
              dominantBaseline="central"
              fontFamily="var(--font-sans)"
              fontSize="10"
              fontWeight={700}
              fill={`var(--color-disc-${value}-fg)`}
            >
              {value}
            </text>
          </g>
        ))}
        <text x="266" y="33" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">tape</text>
        <g transform={`translate(${TAPE_X(2)} 52)`}>
          <path className="head" data-anim="head" d="M0,0 l-5,7 h10 z" fill="var(--color-accent)" />
        </g>
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x={LEFT_X} y="68">C++ reference</text>
        <text x={RIGHT_X} y="68">Rust bitboard</text>
      </g>
      <g className="boards" fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1">
        <path d={gridPath(LEFT_X, BOARD_Y)} />
        <path d={gridPath(RIGHT_X, BOARD_Y)} />
      </g>
      <g className="moves">
        {MOVES.map(([value, col, row], index) => {
          const [lx, ly] = centre(LEFT_X, col, row);
          const [rx, ry] = centre(RIGHT_X, col, row);
          return (
            <g key={index} className={`land land-${index + 1}`} data-anim="land">
              <circle cx={lx} cy={ly} r="5" fill={`var(--color-disc-${value})`} />
              <circle cx={rx} cy={ry} r="5" fill={`var(--color-disc-${value})`} />
            </g>
          );
        })}
      </g>
      <g className="checks" fill="none" stroke="var(--color-status-completed)" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        {MOVES.map((_, index) => (
          <path
            key={index}
            className={`check check-${index + 1}`}
            data-anim="check"
            d="M-4,0 l3,3 l5,-6"
            transform={`translate(${146 + index * 14} 98)`}
          />
        ))}
      </g>
      <g className="counter" fontFamily="var(--font-mono)" fontSize="10" fill="var(--color-ink-2)" textAnchor="middle">
        {COUNTS.map((label, index) => (
          <text key={label} className={`count count-${index}`} data-anim="count" x="160" y="140" opacity={index === 3 ? 1 : 0}>
            {label}
          </text>
        ))}
      </g>
      <g className="tart-final">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          same seed, same moves, same bytes
        </text>
      </g>
    </svg>
  );
}
