/**
 * Figures for the NNUE technique primer at content/learn/techniques/nnue.mdx.
 *
 * The toy is a three-by-three board of lights, so the reader can see the
 * gather-and-add first layer before meeting Drop7. The one Drop7 board, in
 * NnueOneFlip, is the engine-generated "drop" scenario from
 * content/learn/rules-scenarios.json (written by
 * scripts/generate-rules-scenarios.ts), read at render time and never drawn by
 * hand. The budget figure quotes numbers recorded on the learned-leaf approach
 * page and nothing else.
 *
 * Server components: SVG with CSS keyframes from ./nnue.css on elements marked
 * data-anim. Only opacity animates; text never does.
 */
import "./nnue.css";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { ReactNode } from "react";
import { CellGlyph } from "@/components/discs";

const SANS = "var(--font-sans)";
const MONO = "var(--font-mono)";
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const INK_4 = "var(--color-ink-4)";
const RULE = "var(--color-rule)";
const SURFACE = "var(--color-surface)";
const RAISED = "var(--color-raised)";
const CELL = "var(--color-cell)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const LIT = "var(--color-highlight)";
const S7 = "var(--color-series-7)";

function Fig({ caption, children }: { caption: string; children: ReactNode }) {
  return (
    <figure className="fig">
      <div className="fig-frame">{children}</div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/** The toy board: nine lights, row-major; these five are on. */
const LIT_CELLS = [0, 1, 4, 7, 8];

/** A fixed texture per table row so no two rows look alike; not data. */
function texture(row: number, column: number): number {
  return 0.25 + 0.7 * (((row * 7 + column * 3) % 5) / 4);
}

function Light({ x, y, s, on }: { x: number; y: number; s: number; on: boolean }) {
  return (
    <rect
      x={x}
      y={y}
      width={s}
      height={s}
      rx={s * 0.18}
      fill={on ? LIT : CELL}
      stroke={on ? LIT : RULE}
      strokeWidth={1}
    />
  );
}

function TableRow({
  x,
  y,
  w,
  h,
  index,
  lit,
}: {
  x: number;
  y: number;
  w: number;
  h: number;
  index: number;
  lit: boolean;
}) {
  return (
    <g>
      <rect x={x} y={y} width={w} height={h} rx={3} fill={lit ? ACCENT_SOFT : RAISED} stroke={lit ? ACCENT : RULE} />
      <text x={x + 6} y={y + h * 0.72} fontSize={8.5} fontFamily={MONO} fill={lit ? INK : INK_3}>
        row {index + 1}
      </text>
      {Array.from({ length: 6 }, (_, c) => (
        <rect
          key={c}
          x={x + w - 4 - (6 - c) * 8}
          y={y + 4}
          width={5}
          height={h - 8}
          rx={1}
          fill={lit ? ACCENT : INK_4}
          opacity={texture(index, c)}
        />
      ))}
    </g>
  );
}

/* =========================================================================
 * 1. Gather, don't multiply: lit rows of a table add up to the hidden vector.
 * ========================================================================= */

export function NnueGather() {
  const s = 26;
  const gap = 5;
  const gx = 24;
  const gy = 50;
  const tx = 168;
  const ty = 34;
  const tw = 118;
  const th = 16;
  const tgap = 4;
  const rowY = (i: number) => ty + i * (th + tgap);
  const accX = 332;
  const accW = 24;
  const accBottom = rowY(8) + th;
  const headX1 = 404;
  const headX2 = 444;
  const head1 = [134, 158, 182, 206];
  const head2 = [146, 170, 194];
  const outX = 478;
  const outY = 150;
  return (
    <Fig caption="The toy board has nine lights and five are on. The first layer is a table with one row per light; the board's hidden vector is the sum of the five lit rows, gathered and added with no multiplication. Two small dense layers then turn that short vector into one number.">
      <svg
        className="primer-nnue"
        viewBox="0 0 560 250"
        role="img"
        aria-label="Five lit cells select five rows of a table, which add up to one accumulator, which two small layers turn into one number"
      >
        <text x={gx} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the board
        </text>
        <text x={tx} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the table: one row per light
        </text>
        <text x={accX - 6} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          sum
        </text>
        <text x={headX1 - 12} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          two small layers
        </text>
        {Array.from({ length: 9 }, (_, i) => {
          const col = i % 3;
          const row = Math.floor(i / 3);
          return <Light key={i} x={gx + col * (s + gap)} y={gy + row * (s + gap)} s={s} on={LIT_CELLS.includes(i)} />;
        })}
        <text x={gx} y={gy + 3 * (s + gap) + 12} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          9 lights, 5 on
        </text>
        {/* connectors from lit lights to their rows */}
        {LIT_CELLS.map((i) => {
          const col = i % 3;
          const row = Math.floor(i / 3);
          const x1 = gx + col * (s + gap) + s;
          const y1 = gy + row * (s + gap) + s / 2;
          const y2 = rowY(i) + th / 2;
          return <path key={i} d={`M${x1},${y1} C${x1 + 30},${y1} ${tx - 30},${y2} ${tx},${y2}`} fill="none" stroke={INK_4} strokeWidth={1} />;
        })}
        {Array.from({ length: 9 }, (_, i) => (
          <TableRow key={i} x={tx} y={rowY(i)} w={tw} h={th} index={i} lit={LIT_CELLS.includes(i)} />
        ))}
        {/* the accumulator: one segment per lit row, stacked */}
        {LIT_CELLS.map((i, j) => {
          const segY = accBottom - (j + 1) * th;
          const y1 = rowY(i) + th / 2;
          return (
            <g key={i}>
              <path d={`M${tx + tw},${y1} C${tx + tw + 20},${y1} ${accX - 20},${segY + th / 2} ${accX},${segY + th / 2}`} fill="none" stroke={INK_4} strokeWidth={1} />
              <rect x={accX} y={segY} width={accW} height={th} fill={ACCENT} stroke={SURFACE} strokeWidth={1} />
            </g>
          );
        })}
        <text x={accX - 6} y={accBottom + 16} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          5 rows added
        </text>
        <text x={accX - 22} y={accBottom + 30} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          the hidden vector
        </text>
        {/* head: fully connected, tiny */}
        {head1.map((y1) => (
          <path key={y1} d={`M${accX + accW},${accBottom - 2.5 * th} L${headX1 - 7},${y1}`} stroke={INK_4} strokeWidth={0.8} />
        ))}
        {head1.map((y1) => head2.map((y2) => <path key={`${y1}-${y2}`} d={`M${headX1 + 7},${y1} L${headX2 - 7},${y2}`} stroke={INK_4} strokeWidth={0.8} />))}
        {head2.map((y2) => (
          <path key={y2} d={`M${headX2 + 7},${y2} L${outX},${outY + 20}`} stroke={INK_4} strokeWidth={0.8} />
        ))}
        {head1.map((y) => (
          <circle key={y} cx={headX1} cy={y} r={7} fill={SURFACE} stroke={INK_2} strokeWidth={1.2} />
        ))}
        {head2.map((y) => (
          <circle key={y} cx={headX2} cy={y} r={7} fill={SURFACE} stroke={INK_2} strokeWidth={1.2} />
        ))}
        <text x={headX1 - 12} y={accBottom + 16} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          tiny dense layers
        </text>
        <rect x={outX} y={outY} width={72} height={40} rx={6} fill={SURFACE} stroke={ACCENT} />
        <text x={outX + 36} y={outY + 17} textAnchor="middle" fontSize={9.5} fontFamily={SANS} fill={INK_2}>
          how good
        </text>
        <text x={outX + 36} y={outY + 31} textAnchor="middle" fontSize={10} fontFamily={MONO} fontWeight={700} fill={ACCENT}>
          one number
        </text>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 2. One cell flips: one row joins the sum, everything else stays as it was.
 * ========================================================================= */

interface DropScenarioFile {
  drop?: { initial: { board: string; nextDisc: number }; final: { board: string } };
}

function loadDrop(): { before: string; after: string; changed: number[] } | null {
  const path = join(process.cwd(), "content", "learn", "rules-scenarios.json");
  if (!existsSync(path)) return null;
  const file = JSON.parse(readFileSync(path, "utf8")) as DropScenarioFile;
  if (!file.drop) return null;
  const before = file.drop.initial.board;
  const after = file.drop.final.board;
  const changed = [...after].map((cell, i) => (cell !== before[i] ? i : -1)).filter((i) => i >= 0);
  return { before, after, changed };
}

export function NnueOneFlip() {
  const drop = loadDrop();
  const s = 22;
  const gap = 4;
  const gx = 20;
  const gy = 52;
  const flip = 5;
  const tx = 118;
  const ty = 34;
  const tw = 104;
  const th = 14;
  const tgap = 3;
  const rowY = (i: number) => ty + i * (th + tgap);
  const accX = 246;
  const accW = 22;
  const accBottom = rowY(8) + th;
  const flipCol = flip % 3;
  const flipRow = Math.floor(flip / 3);
  const flipX = gx + flipCol * (s + gap);
  const flipY = gy + flipRow * (s + gap);
  const bs = 18;
  const bx = 338;
  const by = 36;
  return (
    <Fig caption="On the left, the light in the second row and third column switches on and off; only row 6 of the table lights with it, and only one segment joins the sum. On the right, the same update on a Drop7 board: the engine's drop scenario from the rules page lands one disc, so one cell changes and one row is added, and the other 48 cells are not recomputed.">
      <svg
        className="primer-nnue"
        viewBox="0 0 560 240"
        role="img"
        aria-label="One light toggles and one table row and one accumulator segment toggle with it; beside it a Drop7 board where one dropped disc changes one cell"
      >
        <text x={gx} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          one light flips
        </text>
        {Array.from({ length: 9 }, (_, i) => {
          const col = i % 3;
          const row = Math.floor(i / 3);
          return <Light key={i} x={gx + col * (s + gap)} y={gy + row * (s + gap)} s={s} on={LIT_CELLS.includes(i)} />;
        })}
        <g data-anim="nnue-flip">
          <Light x={flipX} y={flipY} s={s} on />
        </g>
        <rect x={flipX - 3} y={flipY - 3} width={s + 6} height={s + 6} rx={6} fill="none" stroke={ACCENT} strokeWidth={1.2} strokeDasharray="3 2" />
        <text x={gx} y={gy + 3 * (s + gap) + 14} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          this light: on, off, on
        </text>
        {Array.from({ length: 9 }, (_, i) => (
          <TableRow key={i} x={tx} y={rowY(i)} w={tw} h={th} index={i} lit={LIT_CELLS.includes(i)} />
        ))}
        <g data-anim="nnue-flip">
          <TableRow x={tx} y={rowY(flip)} w={tw} h={th} index={flip} lit />
        </g>
        <path
          d={`M${flipX + s},${flipY + s / 2} C${flipX + s + 24},${flipY + s / 2} ${tx - 24},${rowY(flip) + th / 2} ${tx},${rowY(flip) + th / 2}`}
          fill="none"
          stroke={ACCENT}
          strokeWidth={1}
          strokeDasharray="3 2"
        />
        {LIT_CELLS.map((i, j) => (
          <rect key={i} x={accX} y={accBottom - (j + 1) * th} width={accW} height={th} fill={ACCENT} opacity={0.55} stroke={SURFACE} />
        ))}
        <g data-anim="nnue-flip">
          <rect x={accX} y={accBottom - 6 * th} width={accW} height={th} fill={ACCENT} stroke={SURFACE} />
        </g>
        <path
          d={`M${tx + tw},${rowY(flip) + th / 2} C${tx + tw + 12},${rowY(flip) + th / 2} ${accX - 12},${accBottom - 5.5 * th} ${accX},${accBottom - 5.5 * th}`}
          fill="none"
          stroke={ACCENT}
          strokeWidth={1}
          strokeDasharray="3 2"
        />
        <text x={accX + accW + 6} y={accBottom - 5.5 * th + 3} fontSize={9.5} fontFamily={SANS} fill={ACCENT}>
          + one row
        </text>
        <text x={accX + accW + 6} y={accBottom - 2 * th} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          the rest
        </text>
        <text x={accX + accW + 6} y={accBottom - 2 * th + 12} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          is untouched
        </text>
        <text x={tx} y={accBottom + 18} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          row 6 joins or leaves the sum
        </text>
        {/* the Drop7 panel */}
        <text x={bx - 8} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the same update on a Drop7 board
        </text>
        {drop ? (
          <g>
            <rect x={bx - 4} y={by - 4} width={7 * bs + 8} height={7 * bs + 8} rx={6} fill={SURFACE} />
            {[...drop.before].map((char, i) => (
              <CellGlyph key={i} cell={Number(char)} x={bx + (i % 7) * bs} y={by + Math.floor(i / 7) * bs} s={bs} />
            ))}
            <g data-anim="nnue-flip">
              {drop.changed.map((i) => (
                <CellGlyph key={i} cell={Number(drop.after[i])} x={bx + (i % 7) * bs} y={by + Math.floor(i / 7) * bs} s={bs} />
              ))}
            </g>
            {drop.changed.map((i) => (
              <rect
                key={i}
                x={bx + (i % 7) * bs + 1}
                y={by + Math.floor(i / 7) * bs + 1}
                width={bs - 2}
                height={bs - 2}
                rx={4}
                fill="none"
                stroke={ACCENT}
                strokeWidth={1.4}
              />
            ))}
            <text x={bx - 8} y={by + 7 * bs + 22} fontSize={9.5} fontFamily={SANS} fill={INK_2}>
              one drop lands: {drop.changed.length} cell{drop.changed.length === 1 ? "" : "s"} changed, {drop.changed.length} row{drop.changed.length === 1 ? "" : "s"} added
            </text>
            <text x={bx - 8} y={by + 7 * bs + 36} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
              {49 - drop.changed.length} cells unchanged: nothing recomputed
            </text>
            <text x={bx - 8} y={by + 7 * bs + 50} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
              engine output: the rules page&apos;s drop scenario
            </text>
          </g>
        ) : (
          <text x={bx - 8} y={by + 20} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
            The drop scenario is not in this checkout.
          </text>
        )}
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 3. The budget: what one board may cost when a decision scores 615,090 of them.
 * ========================================================================= */

export function NnueBudget() {
  const x0 = 64;
  const x1 = 520;
  const axisY = 118;
  const decades = 4;
  const px = (micros: number) => x0 + (Math.log10(micros) / decades) * (x1 - x0);
  const ticks = [
    { t: 1, label: "1 µs" },
    { t: 10, label: "10 µs" },
    { t: 100, label: "100 µs" },
    { t: 1000, label: "1 ms" },
    { t: 10000, label: "10 ms" },
  ];
  const nnueX = px(1.33);
  const convX = px(4122);
  return (
    <Fig caption="Time per board on a logarithmic scale, as recorded on the learned-leaf page: the NNUE-shaped student at 1.33 microseconds and the convolutional network it replaced at 4,122 microseconds. Above the scale is the number of boards one four-move decision scores, measured over 30 real decisions.">
      <svg
        className="primer-nnue"
        viewBox="0 0 560 200"
        role="img"
        aria-label="A logarithmic time scale from one microsecond to ten milliseconds with the NNUE at 1.33 microseconds and a convolutional network at 4,122 microseconds"
      >
        <text x={x0} y={28} fontSize={11} fontFamily={MONO} fontWeight={700} fill={INK}>
          615,090 boards scored per decision
        </text>
        <text x={x0} y={44} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          four-move fair search, five chance samples; 2,271,280 with seven
        </text>
        <path d={`M${x0},${axisY} H${x1}`} stroke={INK_2} strokeWidth={1.2} />
        {ticks.map((tick) => (
          <g key={tick.t}>
            <path d={`M${px(tick.t)},${axisY - 4} V${axisY + 4}`} stroke={INK_2} strokeWidth={1} />
            <text x={px(tick.t)} y={axisY + 18} textAnchor="middle" fontSize={9} fontFamily={MONO} fill={INK_3}>
              {tick.label}
            </text>
          </g>
        ))}
        <text x={x1} y={axisY + 32} textAnchor="end" fontSize={9} fontFamily={SANS} fill={INK_4}>
          time to score one board (log scale)
        </text>
        <g>
          <path d={`M${nnueX},${axisY} V${axisY - 30}`} stroke={ACCENT} strokeWidth={1.4} />
          <circle cx={nnueX} cy={axisY} r={4.5} fill={ACCENT} />
          <text x={nnueX + 6} y={axisY - 32} fontSize={10} fontFamily={MONO} fontWeight={700} fill={ACCENT}>
            NNUE-shaped student: 1.33 µs
          </text>
          <text x={nnueX + 6} y={axisY - 20} fontSize={9} fontFamily={SANS} fill={INK_3}>
            572,367 parameters, held-out correlation 0.8564
          </text>
        </g>
        <g>
          <path d={`M${convX},${axisY} V${axisY - 56}`} stroke={S7} strokeWidth={1.4} />
          <circle cx={convX} cy={axisY} r={4.5} fill={S7} />
          <text x={convX - 6} y={axisY - 58} textAnchor="end" fontSize={10} fontFamily={MONO} fontWeight={700} fill={S7}>
            convolutional network: 4,122 µs
          </text>
          <text x={convX - 6} y={axisY - 46} textAnchor="end" fontSize={9} fontFamily={SANS} fill={INK_3}>
            3,006,543 parameters, held-out correlation 0.8646
          </text>
        </g>
        <text x={x0} y={172} fontSize={9.5} fontFamily={SANS} fill={INK_2}>
          One decision: 0.887 s with the reference leaf; 2,535 s with the convolutional network, a factor of 2,860.
        </text>
        <text x={x0} y={186} fontSize={9} fontFamily={SANS} fill={INK_3}>
          Numbers as recorded on the learned-leaf page; the scale is drawn from them.
        </text>
      </svg>
    </Fig>
  );
}
