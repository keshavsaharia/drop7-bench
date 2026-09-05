/**
 * Card art for afterstates: three small boards in a row. A disc drops into
 * the first (the move); the middle board, the position after the drop, is
 * ringed and gets a value badge; a "?" disc appears above the third (the
 * chance node). The ring persists: it is the afterstate that gets ranked.
 *
 * Server component. Motion lives in afterstate.css (transform and opacity
 * only); the markup is the resting frame.
 */
import "./afterstate.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const CELL = 13;
const ROWS = 4;
const COLS = 4;
const BOARD_Y = 76;
const BOARDS = [30, 134, 238] as const;

function gridPath(x: number, y: number): string {
  const parts = [`M${x},${y} h${COLS * CELL} v${ROWS * CELL} h${-COLS * CELL} z`];
  for (let c = 1; c < COLS; c++) parts.push(`M${x + c * CELL},${y} v${ROWS * CELL}`);
  for (let r = 1; r < ROWS; r++) parts.push(`M${x},${y + r * CELL} h${COLS * CELL}`);
  return parts.join(" ");
}

function centre(boardX: number, col: number, row: number): [number, number] {
  return [boardX + col * CELL + CELL / 2, BOARD_Y + row * CELL + CELL / 2];
}

function Disc({ cx, cy, value }: { cx: number; cy: number; value: number }) {
  return (
    <>
      <circle cx={cx} cy={cy} r="5.6" fill={`var(--color-disc-${value})`} />
      <text
        x={cx}
        y={cy + 0.5}
        textAnchor="middle"
        dominantBaseline="central"
        fontFamily="var(--font-sans)"
        fontSize="8"
        fontWeight={700}
        fill={`var(--color-disc-${value}-fg)`}
      >
        {value}
      </text>
    </>
  );
}

export function AfterstateArt({ mode = "hover", title, className }: ArtProps) {
  const [dropX, dropY] = centre(BOARDS[0], 2, 3);
  return (
    <svg
      className={["tart", "tart--afterstate", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Afterstates: the move, the position it leaves, and the chance node that follows"}
    >
      <g className="boards" fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1">
        {BOARDS.map((x) => (
          <path key={x} d={gridPath(x, BOARD_Y)} />
        ))}
      </g>
      <g className="arrows" fill="none" stroke="var(--color-ink-3)" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M88,102 h36 m-5,-4 l5,4 l-5,4" />
        <path d="M192,102 h36 m-5,-4 l5,4 l-5,4" />
      </g>
      <g className="move">
        <Disc cx={centre(BOARDS[0], 1, 3)[0]} cy={centre(BOARDS[0], 1, 3)[1]} value={3} />
        <g transform={`translate(${dropX} ${dropY})`}>
          <g className="drop" data-anim="drop">
            <Disc cx={0} cy={0} value={5} />
          </g>
        </g>
      </g>
      <g className="afterstate">
        <Disc cx={centre(BOARDS[1], 1, 3)[0]} cy={centre(BOARDS[1], 1, 3)[1]} value={3} />
        <Disc cx={centre(BOARDS[1], 2, 3)[0]} cy={centre(BOARDS[1], 2, 3)[1]} value={5} />
        <rect
          className="ring"
          data-anim="ring"
          x={BOARDS[1] - 3}
          y={BOARD_Y - 3}
          width={COLS * CELL + 6}
          height={ROWS * CELL + 6}
          rx="4"
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1.5"
        />
        <g className="badge" data-anim="badge">
          <rect x="137" y="50" width="46" height="16" rx="3" fill="var(--color-accent-strong)" />
          <text x="160" y="62" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="10" fill="var(--color-accent-fg)">
            v(s′)
          </text>
        </g>
      </g>
      <g className="chance">
        <Disc cx={centre(BOARDS[2], 1, 3)[0]} cy={centre(BOARDS[2], 1, 3)[1]} value={3} />
        <Disc cx={centre(BOARDS[2], 2, 3)[0]} cy={centre(BOARDS[2], 2, 3)[1]} value={5} />
        <g className="next" data-anim="next">
          <circle cx="264" cy="58" r="6.5" fill="var(--color-disc-gray-core)" stroke="var(--color-disc-gray)" strokeWidth="2" />
          <text x="264" y="58.5" textAnchor="middle" dominantBaseline="central" fontFamily="var(--font-mono)" fontSize="9" fontWeight={700} fill="var(--color-ink-1)">
            ?
          </text>
        </g>
      </g>
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)" textAnchor="middle">
        <text x="56" y="142">move</text>
        <text x="160" y="142">afterstate</text>
        <text x="264" y="142">chance</text>
      </g>
      <g className="tart-final">
        <text x="160" y="170" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          rank the afterstate, not the move
        </text>
      </g>
    </svg>
  );
}
