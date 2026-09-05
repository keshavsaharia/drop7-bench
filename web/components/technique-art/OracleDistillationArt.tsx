/**
 * Card art for oracles and teachers: the same position seen twice, split by
 * the information boundary. The teacher's board shows every value; the
 * student's board shows grey covers where values are hidden. A label tag
 * crosses the line; the two hidden values try to follow and fade at it.
 *
 * Server component. Motion lives in oracle-distillation.css (transform and
 * opacity only); the markup is the resting frame with the tag on the right.
 */
import "./oracle-distillation.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

const CELL = 16;
const LEFT = { x: 26, y: 58 };
const RIGHT = { x: 230, y: 58 };

function gridPath(x: number, y: number, cols: number, rows: number, s: number): string {
  const parts = [`M${x},${y} h${cols * s} v${rows * s} h${-cols * s} z`];
  for (let c = 1; c < cols; c++) parts.push(`M${x + c * s},${y} v${rows * s}`);
  for (let r = 1; r < rows; r++) parts.push(`M${x},${y + r * s} h${cols * s}`);
  return parts.join(" ");
}

function centre(origin: { x: number; y: number }, col: number, row: number): [number, number] {
  return [origin.x + col * CELL + CELL / 2, origin.y + row * CELL + CELL / 2];
}

function Disc({ cx, cy, value }: { cx: number; cy: number; value: number }) {
  return (
    <>
      <circle cx={cx} cy={cy} r="6.6" fill={`var(--color-disc-${value})`} />
      <text
        x={cx}
        y={cy + 0.5}
        textAnchor="middle"
        dominantBaseline="central"
        fontFamily="var(--font-sans)"
        fontSize="9"
        fontWeight={700}
        fill={`var(--color-disc-${value}-fg)`}
      >
        {value}
      </text>
    </>
  );
}

function Cover({ cx, cy }: { cx: number; cy: number }) {
  return <circle cx={cx} cy={cy} r="6.6" fill="var(--color-disc-gray-core)" stroke="var(--color-disc-gray)" strokeWidth="2.4" />;
}

/** Discs visible to both; and the two whose values only the teacher sees. */
const VISIBLE: ReadonlyArray<readonly [number, number, number]> = [
  [0, 3, 4],
  [1, 3, 7],
];
const HIDDEN: ReadonlyArray<readonly [number, number, number]> = [
  [1, 2, 2],
  [2, 3, 5],
];

export function OracleDistillationArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--oracle-distillation", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "Oracles and teachers: the teacher's labels cross the information boundary, the hidden values do not"
      }
    >
      <g className="teacher">
        <path d={gridPath(LEFT.x, LEFT.y, 4, 4, CELL)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        {[...VISIBLE, ...HIDDEN].map(([col, row, value]) => {
          const [cx, cy] = centre(LEFT, col, row);
          return <Disc key={`${col}-${row}`} cx={cx} cy={cy} value={value} />;
        })}
      </g>
      <g className="student">
        <path d={gridPath(RIGHT.x, RIGHT.y, 4, 4, CELL)} fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="1" />
        {VISIBLE.map(([col, row, value]) => {
          const [cx, cy] = centre(RIGHT, col, row);
          return <Disc key={`${col}-${row}`} cx={cx} cy={cy} value={value} />;
        })}
        {HIDDEN.map(([col, row]) => {
          const [cx, cy] = centre(RIGHT, col, row);
          return <Cover key={`${col}-${row}`} cx={cx} cy={cy} />;
        })}
      </g>
      <line className="boundary" x1="160" y1="30" x2="160" y2="160" stroke="var(--color-rule-strong)" strokeWidth="1.5" />
      <g fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        <text x="26" y="48">teacher sees</text>
        <text x="230" y="48">student sees</text>
        <text x="160" y="24" textAnchor="middle">information boundary</text>
      </g>
      <g className="label" transform="translate(186 74)">
        <g className="tag" data-anim="tag">
          <rect x="0" y="0" width="26" height="14" rx="3" fill="var(--color-accent-strong)" />
          <path d="M13,3 v8 M9.5,7.5 L13,11 L16.5,7.5" fill="none" stroke="var(--color-accent-fg)" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
        </g>
      </g>
      <g className="escapees">
        {HIDDEN.map(([col, row, value], index) => {
          const [cx, cy] = centre(LEFT, col, row);
          return (
            <g key={value} className={`mover mover-${index}`} data-anim="mover" opacity="0">
              <Disc cx={cx} cy={cy} value={value} />
            </g>
          );
        })}
      </g>
      <g className="tart-final">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          labels cross; hidden values do not
        </text>
      </g>
    </svg>
  );
}
