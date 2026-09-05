/**
 * NNUE card art. A 3x3 board, a tall thin feature table, an accumulator bar
 * and one output box. On play one cell changes, one table row lights, the
 * accumulator grows by one unit and the output value changes. Everything
 * else stays as it was, which is the point of the technique.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./nnue.css";

const CELL = 30;
const OX = 22;
const OY = 44;
const TABLE_X = 142;
const TABLE_Y = 30;
const ROW_H = 12;

const STATIC_DISCS: { col: number; row: number; value: number }[] = [
  { col: 1, row: 0, value: 6 },
  { col: 0, row: 1, value: 3 },
  { col: 2, row: 2, value: 1 },
];
const FLIPPED = { col: 2, row: 1, value: 4 };
const LIT_ROWS = [1, 4, 8];
const FLASH_ROW = 6;

const GRID = "M52 44v90M82 44v90M22 74h90M22 104h90";
const ROW_LINES = Array.from({ length: 9 }, (_, i) => `M142 ${TABLE_Y + (i + 1) * ROW_H}h36`).join("");
const ARROW = (x: number) => `M${x} 89H${x + 24}M${x + 19} 85L${x + 24} 89L${x + 19} 93`;

function Disc({ col, row, value }: { col: number; row: number; value: number }) {
  const cx = OX + CELL / 2 + col * CELL;
  const cy = OY + CELL / 2 + row * CELL;
  return (
    <>
      <circle cx={cx} cy={cy} r={11} fill={`var(--color-disc-${value})`} />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={11}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${value}-fg)`}
      >
        {value}
      </text>
    </>
  );
}

export function NnueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "nnue",
        "One cell of a small board changes, one feature row lights, the accumulator grows by one unit and the output updates",
        props,
      )}
    >
      <g className="board">
        <rect
          x={OX}
          y={OY}
          width={3 * CELL}
          height={3 * CELL}
          rx={3}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={GRID} stroke="var(--color-rule)" fill="none" />
        {STATIC_DISCS.map((d) => (
          <Disc key={`${d.col}${d.row}`} {...d} />
        ))}
        <g data-anim="flip" className="flip">
          <Disc {...FLIPPED} />
        </g>
      </g>
      <g className="arrows" stroke="var(--color-ink-3)" strokeWidth={1.25} fill="none" strokeLinecap="round">
        <path d={ARROW(114)} />
        <path d={ARROW(180)} />
        <path d={ARROW(228)} />
      </g>
      <g className="table">
        <rect
          x={TABLE_X}
          y={TABLE_Y}
          width={36}
          height={10 * ROW_H}
          rx={3}
          fill="var(--color-raised)"
          stroke="var(--color-rule-strong)"
        />
        <path d={ROW_LINES} stroke="var(--color-rule)" fill="none" />
        {LIT_ROWS.map((row) => (
          <rect
            key={row}
            x={TABLE_X + 1}
            y={TABLE_Y + row * ROW_H + 1}
            width={34}
            height={ROW_H - 2}
            fill="var(--color-accent)"
            fillOpacity={0.5}
          />
        ))}
        <rect
          data-anim="row"
          x={TABLE_X + 1}
          y={TABLE_Y + FLASH_ROW * ROW_H + 1}
          width={34}
          height={ROW_H - 2}
          fill="var(--color-highlight)"
          fillOpacity={0.85}
        />
      </g>
      <g className="accumulator">
        <rect x={208} y={30} width={18} height={120} rx={3} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <rect x={209} y={85} width={16} height={64} fill="var(--color-accent)" fillOpacity={0.6} />
        <rect data-anim="unit" className="unit" x={209} y={73} width={16} height={11} fill="var(--color-highlight)" />
      </g>
      <g className="output" fontFamily={ART_MONO} textAnchor="middle">
        <rect x={254} y={75} width={52} height={28} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <text data-anim="out-0" x={280} y={89} dominantBaseline="central" fontSize={12} fontWeight={700} fill="var(--color-ink)" opacity={0}>
          0.41
        </text>
        <g className="tart-final" data-anim="out-1">
          <text x={280} y={89} dominantBaseline="central" fontSize={12} fontWeight={700} fill="var(--color-ink)">
            0.47
          </text>
        </g>
      </g>
      <g className="labels" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)" textAnchor="middle">
        <text x={160} y={22}>
          table
        </text>
        <text x={217} y={22}>
          acc
        </text>
        <text x={280} y={66}>
          out
        </text>
      </g>
    </svg>
  );
}
