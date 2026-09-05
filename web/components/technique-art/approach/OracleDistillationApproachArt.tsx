/**
 * Card art for `oracle-curriculum/oracle-distillation`: the teacher's board
 * with the covered value shown, a small network, and the student's board with
 * the cover still on. On play the labels flow left into the net, the net emits
 * a column, and the column it names is not the teacher's — the cross is the
 * result this approach recorded.
 *
 * Both boards are the same crop (columns 3-6, rows 4-6) of the `reveal`
 * position in web/content/learn/rules-scenarios.json, whose covered disc that
 * scenario later reveals as a 5.
 *
 * Server component. Motion lives in oracle-distillation.css (transform and
 * opacity only); the markup is the resting frame.
 */
import { CellGlyph } from "@/components/discs";
import type { ArtProps } from "../registry";
import "./oracle-distillation.css";

const CELL = 16;
const COLS = 4;
const ROWS = 3;
const LEFT = { x: 14, y: 74 };
const RIGHT = { x: 242, y: 74 };
/** The crop, as [column within the crop, row within the crop, cell]. */
const SHARED: ReadonlyArray<readonly [number, number, number]> = [
  [1, 1, 5],
  [2, 1, 6],
  [1, 2, 7],
  [2, 2, 4],
];
const COVERED: readonly [number, number] = [2, 0];

function grid(origin: { x: number; y: number }): string {
  return [
    ...Array.from({ length: COLS + 1 }, (_, i) => `M${origin.x + i * CELL},${origin.y}v${ROWS * CELL}`),
    ...Array.from({ length: ROWS + 1 }, (_, i) => `M${origin.x},${origin.y + i * CELL}h${COLS * CELL}`),
  ].join("");
}

function Crop({ origin, cover }: { origin: { x: number; y: number }; cover: boolean }) {
  return (
    <g>
      <path d={grid(origin)} fill="none" stroke="var(--color-rule)" strokeWidth="0.8" />
      {SHARED.map(([col, row, cell]) => (
        <CellGlyph key={`${col}-${row}`} cell={cell} x={origin.x + col * CELL} y={origin.y + row * CELL} s={CELL} />
      ))}
      <CellGlyph
        cell={cover ? 9 : 5}
        x={origin.x + COVERED[0] * CELL}
        y={origin.y + COVERED[1] * CELL}
        s={CELL}
      />
    </g>
  );
}

const NET_IN = [86, 104, 122];
const NET_HID = [95, 113];
const NET_EDGES = NET_IN.flatMap((y) => NET_HID.map((y2) => `M136,${y}L166,${y2}`))
  .concat(NET_HID.map((y) => `M166,${y}L196,104`))
  .join("");

export function OracleDistillationApproachArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--approach-oracle-distillation", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={
        title ?? "A teacher that sees the covered value labels boards for a student that cannot, and the student names the wrong column"
      }
    >
      <Crop origin={LEFT} cover={false} />
      <Crop origin={RIGHT} cover />
      <path d={NET_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth="0.8" />
      <g className="net" fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1.1">
        {NET_IN.map((y) => (
          <circle key={`i${y}`} cx="136" cy={y} r="4" />
        ))}
        {NET_HID.map((y) => (
          <circle key={`h${y}`} cx="166" cy={y} r="4" />
        ))}
        <circle cx="196" cy="104" r="5" />
      </g>
      <g className="tags" data-anim="tags" opacity="0">
        <rect x="86" y="60" width="20" height="11" rx="2" fill="var(--color-reads-teacher)" />
        <rect x="86" y="136" width="20" height="11" rx="2" fill="var(--color-reads-teacher)" />
      </g>
      <g className="pick" data-anim="pick">
        <path
          d="M206,104h22"
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1.6"
          strokeLinecap="round"
        />
        <path d="M230,104l-6,-4v8z" fill="var(--color-accent)" />
      </g>
      <g className="cross" data-anim="cross">
        <path
          d={`M${RIGHT.x + 3 * CELL - 5},${RIGHT.y - 14}l10,10m0,-10l-10,10`}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth="2"
          strokeLinecap="round"
        />
      </g>
      <path
        d={`M${RIGHT.x + 1 * CELL + 8},${RIGHT.y - 6}v-8m-4,4l4,-4l4,4`}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth="1.2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <g fontFamily="var(--font-mono)" fontSize="9">
        <text x="14" y="60" fill="var(--color-reads-teacher)">
          teacher
        </text>
        <text x="14" y="140" fill="var(--color-ink-3)">
          value seen
        </text>
        <text x="242" y="60" fill="var(--color-ink-3)">
          student
        </text>
        <text x="242" y="140" fill="var(--color-ink-3)">
          value covered
        </text>
        <text x="136" y="152" fill="var(--color-ink-3)">
          net
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-2)">
          the column the teacher chose is not recoverable
        </text>
      </g>
    </svg>
  );
}
