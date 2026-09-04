/**
 * Figures for the n-tuple primer (web/content/learn/techniques/n-tuple.mdx).
 *
 * The toy is the five-cell strip from the primer: cells are empty, red or
 * blue; the windows are the four neighbouring pairs; one shared table of
 * nine slots. The table's numbers are made up for the illustration. The
 * only real board is in NTupleDropWindows: the harvested position recorded
 * in web/content/learn/data/leaf-scenarios.json (`xray.board`), reached by
 * the TypeScript rules engine in a demonstration game. Server components:
 * SVG with CSS keyframes in ./n-tuple.css on elements marked data-anim.
 */
import type { ReactNode } from "react";
import { CellGlyph, parseBoard } from "@/components/discs";
import "./n-tuple.css";

const MONO = "var(--font-mono)";
const SANS = "var(--font-sans)";
const INK = "var(--color-ink)";
const INK1 = "var(--color-ink-1)";
const INK2 = "var(--color-ink-2)";
const INK3 = "var(--color-ink-3)";
const RULE = "var(--color-rule)";
const RULE_STRONG = "var(--color-rule-strong)";
const SURFACE = "var(--color-surface)";
const RAISED = "var(--color-raised)";
const CELL = "var(--color-cell)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const RED = "var(--color-disc-4)";
const BLUE = "var(--color-disc-7)";
const GOOD = "var(--color-series-3)";
const BAD = "var(--color-series-7)";
const ROW_WINDOW = "var(--color-series-1)";
const COLUMN_WINDOW = "var(--color-series-2)";
const BLOCK_WINDOW = "var(--color-series-3)";

/** The recorded position behind NTupleDropWindows: leaf-scenarios.json, xray.board. */
const XRAY_BOARD = "0000000000000000000389995018889703988893988888888";

type Colour = "empty" | "red" | "blue";
const STRIP: Colour[] = ["red", "red", "empty", "red", "red"];

/** The nine slots of the shared table, in a fixed order, with made-up weights. */
const TABLE: { code: [Colour, Colour]; weight: number }[] = [
  { code: ["empty", "empty"], weight: 0 },
  { code: ["empty", "red"], weight: 0 },
  { code: ["empty", "blue"], weight: 0 },
  { code: ["red", "empty"], weight: 1 },
  { code: ["red", "red"], weight: 3 },
  { code: ["red", "blue"], weight: -1 },
  { code: ["blue", "empty"], weight: -2 },
  { code: ["blue", "red"], weight: -1 },
  { code: ["blue", "blue"], weight: 2 },
];

function slotOf(a: Colour, b: Colour): number {
  return TABLE.findIndex((row) => row.code[0] === a && row.code[1] === b);
}

function signed(n: number): string {
  return n > 0 ? `+${n}` : n < 0 ? `−${Math.abs(n)}` : "0";
}

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-ntuple">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

function Swatch({ x, y, colour, s = 12 }: { x: number; y: number; colour: Colour; s?: number }) {
  return <rect x={x} y={y} width={s} height={s} rx={s * 0.2} fill={colour === "red" ? RED : colour === "blue" ? BLUE : CELL} stroke={colour === "empty" ? RULE_STRONG : "none"} />;
}

/* =========================================================================
 * 1. The strip, its four windows, and the shared table they read from.
 * ========================================================================= */

export function NTupleStripWindows({ caption }: { caption?: string }) {
  const cellX = (i: number) => 30 + i * 40;
  const cellY = 40;
  const size = 34;
  const tableX = 330;
  const tableY = 30;
  const rowH = 22;
  const windows = [0, 1, 2, 3].map((k) => ({
    k,
    slot: slotOf(STRIP[k], STRIP[k + 1]),
    x1: cellX(k) + 2,
    x2: cellX(k + 1) + size - 2,
    y: cellY + size + 10 + (k % 2) * 14,
  }));
  const total = windows.reduce((sum, w) => sum + TABLE[w.slot].weight, 0);
  return (
    <Fig
      viewBox="0 0 560 260"
      label="A five-cell strip, its four pair windows, and a nine-slot table; each window points at the slot its contents select"
      caption={
        caption ??
        "A strip of five cells read through four windows, each a pair of neighbours. A window's contents select one slot in the table on the right; the four selected numbers are added and the sum is the strip's score. The first and last windows both read (red, red) and land on the same slot, which is how a pattern learned at one end is recognised at the other. The table's numbers are made up for the illustration."
      }
    >
      <text x={30} y={26} fontFamily={MONO} fontSize={10} fill={INK3}>the strip</text>
      {STRIP.map((c, i) => (
        <Swatch key={i} x={cellX(i)} y={cellY} colour={c} s={size} />
      ))}
      {/* windows as brackets under the strip */}
      {windows.map((w) => (
        <g key={w.k}>
          <path d={`M${w.x1} ${w.y} v5 H${w.x2} v-5`} fill="none" stroke={INK3} strokeWidth={1.5} />
          <text x={(w.x1 + w.x2) / 2} y={w.y + 16} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>window {w.k + 1}</text>
        </g>
      ))}
      {/* connectors to the table rows */}
      {windows.map((w) => {
        const rowY = tableY + w.slot * rowH + rowH / 2;
        const sx = (w.x1 + w.x2) / 2;
        const sy = w.y + 20;
        return <path key={`c${w.k}`} d={`M${sx} ${sy} C${sx} ${sy + 60}, ${tableX - 60} ${rowY}, ${tableX - 4} ${rowY}`} fill="none" stroke={INK3} strokeWidth={1} opacity={0.4} />;
      })}
      {/* the table */}
      <text x={tableX} y={tableY - 8} fontFamily={MONO} fontSize={10} fill={INK3}>one table, nine slots, shared by all four windows</text>
      {TABLE.map((row, i) => {
        const y = tableY + i * rowH;
        return (
          <g key={i}>
            <rect x={tableX} y={y} width={220} height={rowH - 2} rx={4} fill={RAISED} />
            <Swatch x={tableX + 8} y={y + 4} colour={row.code[0]} />
            <Swatch x={tableX + 24} y={y + 4} colour={row.code[1]} />
            <text x={tableX + 44} y={y + 14} fontFamily={MONO} fontSize={9.5} fill={INK3}>slot {i + 1}</text>
            <text x={tableX + 210} y={y + 14} textAnchor="end" fontFamily={MONO} fontSize={11} fontWeight={700} fill={INK}>{signed(row.weight)}</text>
          </g>
        );
      })}
      {/* the sum */}
      <text x={30} y={150} fontFamily={SANS} fontSize={11} fill={INK2}>read each window, look up its slot, add the four numbers:</text>
      <text x={30} y={172} fontFamily={MONO} fontSize={13} fontWeight={700} fill={INK}>
        {windows.map((w) => TABLE[w.slot].weight).join(" + ")} = {total}
      </text>
      <text x={30} y={198} fontFamily={SANS} fontSize={10.5} fill={INK2}>windows 1 and 4 both read (red, red): one slot, looked up twice.</text>
      <text x={30} y={214} fontFamily={SANS} fontSize={10.5} fill={INK3}>no window sees cells 1 and 5 together; nothing about that pair can be learned.</text>
      {/* animated highlights, one window at a time */}
      {windows.map((w) => {
        const rowY = tableY + w.slot * rowH;
        const sx = (w.x1 + w.x2) / 2;
        const sy = w.y + 20;
        return (
          <g key={`h${w.k}`} data-anim={`win-${w.k + 1}`}>
            <rect x={w.x1 - 4} y={cellY - 4} width={w.x2 - w.x1 + 8} height={size + 8} rx={6} fill="none" stroke={ACCENT} strokeWidth={2} />
            <path d={`M${sx} ${sy} C${sx} ${sy + 60}, ${tableX - 60} ${rowY + rowH / 2}, ${tableX - 4} ${rowY + rowH / 2}`} fill="none" stroke={ACCENT} strokeWidth={2} />
            <rect x={tableX - 2} y={rowY - 2} width={224} height={rowH + 2} rx={5} fill={ACCENT_SOFT} stroke={ACCENT} strokeWidth={1.5} />
          </g>
        );
      })}
    </Fig>
  );
}

/* =========================================================================
 * 2. The nudge: a good outcome ticks every looked-up slot up; a bad one
 *    ticks them down.
 * ========================================================================= */

export function NTupleNudge({ caption }: { caption?: string }) {
  const rows = [
    { slot: slotOf("red", "red"), times: 2 },
    { slot: slotOf("red", "empty"), times: 1 },
    { slot: slotOf("empty", "red"), times: 1 },
  ];
  const zero = 300;
  const unit = 14;
  return (
    <Fig
      viewBox="0 0 560 220"
      label="The three slots the strip looked up, as bars, ticking up after a good game and down after a bad one"
      caption={
        caption ??
        "Learning is a nudge. When a game ends well, every slot that was looked up during it moves up by a small step; when it ends badly, every one moves down. The (red, red) slot was looked up twice by this strip, so it moves twice as far. Slots that keep appearing in good games drift upward, and a slot that appears in both kinds stays near zero."
      }
    >
      {/* outcome badges, swapped by opacity */}
      <g data-anim="good">
        <rect x={30} y={22} width={150} height={26} rx={13} fill={GOOD} fillOpacity={0.18} stroke={GOOD} />
        <text x={105} y={39} textAnchor="middle" fontFamily={MONO} fontSize={11} fontWeight={700} fill={GOOD}>the game went well</text>
      </g>
      <g data-anim="bad">
        <rect x={30} y={22} width={150} height={26} rx={13} fill={BAD} fillOpacity={0.18} stroke={BAD} />
        <text x={105} y={39} textAnchor="middle" fontFamily={MONO} fontSize={11} fontWeight={700} fill={BAD}>the game went badly</text>
      </g>
      <text x={200} y={39} fontFamily={SANS} fontSize={11} fill={INK2}>every slot that was looked up moves one unit toward the outcome</text>
      {/* zero line */}
      <line x1={zero} y1={62} x2={zero} y2={186} stroke={RULE_STRONG} />
      <text x={zero} y={200} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>0</text>
      {rows.map((r, i) => {
        const y = 74 + i * 38;
        const row = TABLE[r.slot];
        const w = row.weight * unit;
        const barX = w >= 0 ? zero : zero + w;
        const end = zero + w;
        const tick = r.times * unit;
        return (
          <g key={r.slot}>
            <Swatch x={40} y={y + 2} colour={row.code[0]} s={16} />
            <Swatch x={60} y={y + 2} colour={row.code[1]} s={16} />
            <text x={84} y={y + 14} fontFamily={MONO} fontSize={10} fill={INK3}>
              slot {r.slot + 1}
              {r.times > 1 ? ` · looked up ${r.times}×` : ""}
            </text>
            <text x={286} y={y + 14} textAnchor="end" fontFamily={MONO} fontSize={11} fontWeight={700} fill={INK}>{signed(row.weight)}</text>
            <rect x={barX} y={y + 2} width={Math.abs(w)} height={16} rx={3} fill={RAISED} stroke={RULE} />
            {/* the nudge up: appended to the right of the bar's end */}
            <rect data-anim="tick-up" x={end} y={y + 2} width={tick} height={16} rx={3} fill={GOOD} />
            {/* the nudge down: taken off the right end, or extended left of zero */}
            <rect data-anim="tick-down" x={end - tick} y={y + 2} width={tick} height={16} rx={3} fill={BAD} />
            <g data-anim="good">
              <text x={end + tick + 8} y={y + 14} fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={GOOD}>
                {r.times > 1 ? `+${r.times}` : "+1"}
              </text>
            </g>
            <g data-anim="bad">
              <text x={end + tick + 8} y={y + 14} fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={BAD}>
                {r.times > 1 ? `−${r.times}` : "−1"}
              </text>
            </g>
          </g>
        );
      })}
    </Fig>
  );
}

/* =========================================================================
 * 3. Three of the 92 windows on a real Drop7 position.
 * ========================================================================= */

export function NTupleDropWindows({ caption }: { caption?: string }) {
  const cells = parseBoard(XRAY_BOARD);
  const s = 24;
  const ox = 16;
  const oy = 24;
  const windows = [
    { title: "a row of four", note: "28 positions on the board", colour: ROW_WINDOW, indexes: [4 * 7 + 3, 4 * 7 + 4, 4 * 7 + 5, 4 * 7 + 6], table: "row table" },
    { title: "a column of four", note: "28 positions on the board", colour: COLUMN_WINDOW, indexes: [3 * 7 + 0, 4 * 7 + 0, 5 * 7 + 0, 6 * 7 + 0], table: "column table" },
    { title: "a two-by-two block", note: "36 positions on the board", colour: BLOCK_WINDOW, indexes: [2 * 7 + 5, 2 * 7 + 6, 3 * 7 + 5, 3 * 7 + 6], table: "block table" },
  ];
  return (
    <Fig
      viewBox="0 0 560 250"
      label="A real Drop7 board with a row window, a column window and a two-by-two block outlined, each pointing at its table"
      caption={
        caption ??
        "A position the rules engine reached in a demonstration game (the board recorded for the site's leaf scenarios), with three of the 92 windows the native implementation reads. Each window's four cells become a four-digit code; the code, together with the rise phase and the next disc, selects one learned number in that shape's table; the 92 numbers are added. Gray discs and empty cells are codes too."
      }
    >
      {cells.map((cell, i) => {
        const x = ox + (i % 7) * s;
        const y = oy + Math.floor(i / 7) * s;
        return (
          <g key={i}>
            <rect x={x + 1} y={y + 1} width={s - 2} height={s - 2} rx={3} fill={CELL} />
            <CellGlyph cell={cell} x={x} y={y} s={s} />
          </g>
        );
      })}
      {windows.map((w) =>
        w.indexes.map((index) => (
          <rect key={`${w.title}${index}`} x={ox + (index % 7) * s + 1} y={oy + Math.floor(index / 7) * s + 1} width={s - 2} height={s - 2} rx={4} fill="none" stroke={w.colour} strokeWidth={2.5} />
        )),
      )}
      <text x={ox} y={oy + 7 * s + 16} fontFamily={MONO} fontSize={9.5} fill={INK3}>a recorded engine position</text>
      {windows.map((w, i) => {
        const y = 44 + i * 62;
        const code = w.indexes.map((index) => cells[index]).join(" ");
        return (
          <g key={w.title}>
            <rect x={214} y={y - 18} width={10} height={10} rx={2} fill={w.colour} />
            <text x={232} y={y - 9} fontFamily={SANS} fontSize={12} fontWeight={700} fill={INK}>{w.title}</text>
            <text x={232} y={y + 6} fontFamily={MONO} fontSize={9.5} fill={INK3}>{w.note}</text>
            <text x={232} y={y + 22} fontFamily={MONO} fontSize={11} fill={INK1}>code {code}</text>
            <path d={`M${378} ${y + 2} H${436}`} stroke={INK3} strokeWidth={1.2} />
            <path d={`M${436} ${y + 2} l-6 -4 v8 z`} fill={INK3} />
            <rect x={444} y={y - 14} width={96} height={34} rx={5} fill={SURFACE} stroke={w.colour} />
            <text x={492} y={y + 1} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>{w.table}</text>
            <text x={492} y={y + 14} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>10,000 slots</text>
          </g>
        );
      })}
      <text x={214} y={234} fontFamily={MONO} fontSize={11} fontWeight={700} fill={INK}>92 windows · 17 shared tables · one sum</text>
    </Fig>
  );
}
