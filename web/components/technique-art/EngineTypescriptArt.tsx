/**
 * Card art for the TypeScript engine in the browser: a window frame with a
 * board inside, seven column values rising over it as the worker thread's
 * bar beneath the window fills. The main thread's lane stays idle.
 *
 * Server component. Motion lives in engine-typescript.css (transform and
 * opacity only); the markup is the resting frame.
 */
import "./engine-typescript.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const CELL = 11;
const BOARD = { x: 34, y: 50, cols: 7, rows: 4 };
const VALUE_BASELINE = 46;
/** Relative column values, tallest is the recommended column. */
const VALUES = [6, 9, 14, 16, 8, 11, 5] as const;
const BEST = 3;

function gridPath(x: number, y: number, cols: number, rows: number, s: number): string {
  const parts = [`M${x},${y} h${cols * s} v${rows * s} h${-cols * s} z`];
  for (let c = 1; c < cols; c++) parts.push(`M${x + c * s},${y} v${rows * s}`);
  for (let r = 1; r < rows; r++) parts.push(`M${x},${y + r * s} h${cols * s}`);
  return parts.join(" ");
}

const DISCS: ReadonlyArray<readonly [number, number, number]> = [
  [0, 3, 4],
  [1, 3, 2],
  [3, 3, 6],
  [1, 2, 7],
];

export function EngineTypescriptArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--engine-typescript", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "TypeScript engine: the solver runs in a Web Worker while the page and its board stay responsive"}
    >
      <g className="window">
        <rect x="16" y="12" width="288" height="108" rx="6" fill="var(--color-surface)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <path d="M16,28 V18 a6,6 0 0 1 6,-6 H298 a6,6 0 0 1 6,6 V28 Z" fill="var(--color-raised)" />
        <path
          d="M26,20 a3,3 0 1 0 6,0 a3,3 0 1 0 -6,0 M36,20 a3,3 0 1 0 6,0 a3,3 0 1 0 -6,0 M46,20 a3,3 0 1 0 6,0 a3,3 0 1 0 -6,0"
          fill="var(--color-ink-4)"
        />
        <text x="60" y="23.5" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          solver.worker.ts
        </text>
      </g>
      <g className="board">
        <path d={gridPath(BOARD.x, BOARD.y, BOARD.cols, BOARD.rows, CELL)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        {DISCS.map(([col, row, value]) => (
          <circle
            key={`${col}-${row}`}
            cx={BOARD.x + col * CELL + CELL / 2}
            cy={BOARD.y + row * CELL + CELL / 2}
            r="4.6"
            fill={`var(--color-disc-${value})`}
          />
        ))}
      </g>
      <g className="values">
        {VALUES.map((height, col) => (
          <rect
            key={col}
            className={`value value-${col}`}
            data-anim="value"
            x={BOARD.x + col * CELL + 2}
            y={VALUE_BASELINE - height}
            width={CELL - 4}
            height={height}
            rx="1"
            fill={col === BEST ? "var(--color-accent)" : "var(--color-series-1)"}
          />
        ))}
        <path
          className="best"
          data-anim="best"
          d={`M${BOARD.x + BEST * CELL + CELL / 2},104 l-4,5 h8 z`}
          fill="var(--color-accent)"
        />
      </g>
      <g fontFamily="var(--font-mono)">
        <text x="140" y="66" fontSize="10" fill="var(--color-ink-2)">fastEvaluateMoves()</text>
        <text x="140" y="82" fontSize="9" fill="var(--color-ink-3)">runs in a Web Worker</text>
        <text x="140" y="98" fontSize="9" fill="var(--color-ink-3)">one value per column</text>
      </g>
      <g className="threads">
        <text x="16" y="141" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">main</text>
        <line x1="70" y1="138" x2="304" y2="138" stroke="var(--color-rule-strong)" strokeWidth="2" strokeLinecap="round" />
        <text x="16" y="159" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">worker</text>
        <rect x="70" y="154" width="234" height="8" rx="4" fill="var(--color-raised)" />
        <rect className="busy" data-anim="busy" x="70" y="154" width="234" height="8" rx="4" fill="var(--color-series-1)" />
      </g>
      <g className="tart-final">
        <text x="16" y="176" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          the page stays responsive while the worker searches
        </text>
      </g>
    </svg>
  );
}
