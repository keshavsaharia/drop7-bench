/**
 * Card art for `lifetime-objective/reveal-sampling`: the next disc and the
 * value under a cover are two separate pieces of luck, and the reference drew
 * them on one dial. Two dials joined by a rod turn together; the rod snaps;
 * they turn at their own rates; and the grid beside them fills from the seven
 * cells of a shared draw to all forty-nine pairs. The cycle closes on the
 * joined dials and the diagonal, which is also the resting frame.
 *
 * Server component. Motion lives in reveal-sampling.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import "./reveal-sampling.css";

const DIAL_A = { cx: 58, cy: 56 };
const DIAL_B = { cx: 58, cy: 124 };
const DIAL_R = 19;
const GRID = { x: 172, y: 42, cell: 13 };

const GRID_LINES = [
  ...Array.from({ length: 8 }, (_, i) => `M${GRID.x + i * GRID.cell},${GRID.y}v${7 * GRID.cell}`),
  ...Array.from({ length: 8 }, (_, i) => `M${GRID.x},${GRID.y + i * GRID.cell}h${7 * GRID.cell}`),
].join("");

function cellRect(col: number, row: number): string {
  const x = GRID.x + col * GRID.cell + 1.6;
  const y = GRID.y + row * GRID.cell + 1.6;
  const s = GRID.cell - 3.2;
  return `M${x},${y}h${s}v${s}h${-s}z`;
}

const DIAGONAL = Array.from({ length: 7 }, (_, i) => cellRect(i, i)).join("");
const REST = Array.from({ length: 49 }, (_, i) => [Math.floor(i / 7), i % 7])
  .filter(([row, col]) => row !== col)
  .map(([row, col]) => cellRect(col, row))
  .join("");

function Dial({ cx, cy, name }: { cx: number; cy: number; name: string }) {
  return (
    <g className={name} data-anim={name}>
      <circle cx={cx} cy={cy} r={DIAL_R} fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1.4" />
      <line
        x1={cx}
        y1={cy}
        x2={cx}
        y2={cy - DIAL_R + 4}
        stroke="var(--color-accent)"
        strokeWidth="2.4"
        strokeLinecap="round"
      />
      <circle cx={cx} cy={cy} r="2.4" fill="var(--color-ink-2)" />
    </g>
  );
}

export function RevealSamplingArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-reveal-sampling", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Two dials on one rod turn together until the rod snaps, and the sample grid fills from seven cells to forty-nine"
      }
    >
      <g className="rod" stroke="var(--color-ink-3)" strokeWidth="3.2" strokeLinecap="round">
        <line className="rod-up" data-anim="rod-up" x1={DIAL_A.cx} y1={DIAL_A.cy + DIAL_R} x2={DIAL_A.cx} y2={90} />
        <line className="rod-down" data-anim="rod-down" x1={DIAL_B.cx} y1={90} x2={DIAL_B.cx} y2={DIAL_B.cy - DIAL_R} />
      </g>
      <path
        className="snap"
        data-anim="snap"
        opacity="0"
        d="M50,86 l8,-4 l-6,10 l8,-4"
        fill="none"
        stroke="var(--color-series-2)"
        strokeWidth="1.8"
        strokeLinejoin="round"
      />
      <Dial cx={DIAL_A.cx} cy={DIAL_A.cy} name="dial-a" />
      <Dial cx={DIAL_B.cx} cy={DIAL_B.cy} name="dial-b" />
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="84" y="52">
          next disc
        </text>
        <text x="84" y="120">
          hidden value
        </text>
        <text x={GRID.x} y={GRID.y - 8}>
          sample pairs
        </text>
      </g>
      <path d={GRID_LINES} fill="none" stroke="var(--color-rule)" strokeWidth="0.8" />
      <path d={DIAGONAL} fill="var(--color-accent-strong)" />
      <path className="rest" data-anim="rest" d={REST} fill="var(--color-accent-strong)" opacity="0" />
      <g className="caption-b" data-anim="caption-b" opacity="0">
        <text x="172" y="150" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-accent)">
          7 × 7 drawn apart
        </text>
      </g>
      <g className="tart-final" data-anim="caption-a">
        <text x="172" y="150" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          7 shared draws
        </text>
      </g>
    </svg>
  );
}
