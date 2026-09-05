/**
 * Figures for the afterstates primer
 * (web/content/learn/techniques/afterstate.mdx).
 *
 * The one Drop7 position, the board each legal column leaves behind, and the
 * one-move fair look-ahead value of each are read from
 * web/content/learn/concept-scenarios.json, which
 * web/scripts/generate-concept-scenarios.ts computes with the TypeScript
 * engine. It is the teaching position the concept pages use, never gameplay
 * evidence, and no research number appears here. The stability figure is drawn
 * from toy futures written into this file. Server components: SVG with CSS
 * keyframes in ./afterstate.css on elements marked data-anim. Every figure is
 * complete at its first frame and rests on a designed frame under
 * prefers-reduced-motion.
 */
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { CSSProperties, ReactNode } from "react";
import { CellGlyph } from "@/components/discs";
import "./afterstate.css";

const MONO = "var(--font-mono)";
const SANS = "var(--font-sans)";
const INK = "var(--color-ink)";
const INK1 = "var(--color-ink-1)";
const INK2 = "var(--color-ink-2)";
const INK3 = "var(--color-ink-3)";
const RULE = "var(--color-rule-strong)";
const RAISED = "var(--color-raised)";
const ACCENT = "var(--color-accent)";
const GRAY = "var(--color-disc-gray)";
const GRAY_CORE = "var(--color-disc-gray-core)";
const HALF_1 = "var(--color-series-1)";
const HALF_2 = "var(--color-series-2)";
const AGREE = "var(--color-series-3)";
const DISAGREE = "var(--color-series-7)";

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-afterstate">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

interface ColumnNode {
  column: number;
  legal: boolean;
  board?: string;
  fair?: number;
}

interface TreeScenario {
  board: string;
  nextDisc: number;
  columns: ColumnNode[];
}

type ResolvedColumn = ColumnNode & { board: string };

function loadTree(): TreeScenario | null {
  const path = join(process.cwd(), "content", "learn", "concept-scenarios.json");
  if (!existsSync(path)) return null;
  const data = JSON.parse(readFileSync(path, "utf8")) as { tree?: TreeScenario };
  return data.tree ?? null;
}

function Missing() {
  return (
    <figure className="fig primer-afterstate">
      <div className="fig-frame">
        <p style={{ color: INK3, fontSize: 13, margin: "1rem" }}>
          The teaching position is not available in this checkout. Run <code>npm run figures:concepts</code> inside <code>web/</code> to
          regenerate <code>content/learn/concept-scenarios.json</code>.
        </p>
      </div>
    </figure>
  );
}

/** A 7 by 7 board from the engine's row-major cell string. */
function Board7({ cells, x, y, s }: { cells: string; x: number; y: number; s: number }) {
  const glyphs: ReactNode[] = [];
  for (let i = 0; i < 49; i += 1) {
    const row = Math.floor(i / 7);
    const col = i % 7;
    glyphs.push(<CellGlyph key={i} cell={Number(cells[i])} x={x + col * s} y={y + row * s} s={s} />);
  }
  return <g>{glyphs}</g>;
}

/** How many discs already stand in a column, read from the bottom of the board string. */
function stackHeight(cells: string, col: number): number {
  let height = 0;
  for (let row = 6; row >= 0; row -= 1) {
    if (cells[row * 7 + col] === "0") break;
    height += 1;
  }
  return height;
}

/** The same formatting the concept figures use for a recorded value. */
function fmt(n: number): string {
  return Number.isInteger(n) ? n.toLocaleString("en-US") : n.toFixed(1);
}

/** A disc whose number is still unknown: the next deal. Drawn as paths so no text animates. */
function UnknownDisc({ s }: { s: number }) {
  const c = s / 2;
  return (
    <g>
      <circle cx={c} cy={c} r={s * 0.42} fill={GRAY_CORE} stroke={GRAY} strokeWidth={1.4} />
      <path
        d={`M${c - 2.4} ${c - 2.6}a2.5 2.5 0 1 1 3.8 2.1c-1 .6-1.4 1-1.4 2.1`}
        stroke={GRAY}
        strokeWidth={1.4}
        fill="none"
        strokeLinecap="round"
      />
      <circle cx={c} cy={c + 4.2} r={0.95} fill={GRAY} />
    </g>
  );
}

/* =========================================================================
 * 1. Three frames: the position, the afterstate, and luck's turn.
 * ========================================================================= */

export function AfterstateThreeFrames() {
  const tree = loadTree();
  if (!tree) return <Missing />;
  const col = 5;
  const node = tree.columns[col];
  if (!node || typeof node.board !== "string") return <Missing />;
  const s = 14;
  const boardY = 62;
  const side = 7 * s;
  const frames = [30, 231, 432];
  const hoverY = boardY - 22;
  const landingRow = 6 - stackHeight(tree.board, col);
  const drop = boardY + landingRow * s - hoverY;
  const midY = boardY + side / 2;
  const shown = col + 1;
  return (
    <Fig
      viewBox="0 0 560 226"
      label="Three boards side by side: the position with a disc held above one column, the same board after the drop has resolved with a ring and a value badge around it, and that board again with an unknown disc waiting above it"
      caption={`One position from the concept pages' scenario file, generated by web/scripts/generate-concept-scenarios.ts with the TypeScript engine, with a ${tree.nextDisc} to drop. Left: the board before the move, the ${tree.nextDisc} held above column ${shown}. Middle: the board after the ${tree.nextDisc} has landed and every clear and fall has finished, which is the afterstate; the ring marks it and the badge stands for the one number an evaluator gives it. Right: the same board a moment later, with the next disc still unknown. The disc hovers, falls, and the ring flashes as the board resolves; under reduced motion the disc rests above column ${shown}.`}
    >
      <defs>
        <marker id="primer-afterstate-arrow" viewBox="0 0 10 10" refX={9} refY={5} markerWidth={7} markerHeight={7} orient="auto-start-reverse">
          <path d="M0 0L10 5L0 10z" fill={INK2} />
        </marker>
      </defs>

      <text x={frames[0] + side / 2} y={18} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fill={INK2}>
        1 · the position
      </text>
      <text x={frames[1] + side / 2} y={18} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fill={INK2}>
        2 · the afterstate
      </text>
      <text x={frames[2] + side / 2} y={18} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fill={INK2}>
        3 · luck&apos;s turn
      </text>

      {/* Frame 1: the position and the disc to drop. */}
      <Board7 cells={tree.board} x={frames[0]} y={boardY} s={s} />
      <g transform={`translate(${frames[0] + col * s} ${hoverY})`}>
        <g data-anim="hover" style={{ "--drop": `${drop}px` } as CSSProperties}>
          <CellGlyph cell={tree.nextDisc} x={0} y={0} s={s} />
        </g>
      </g>
      <text x={frames[0] + side / 2} y={boardY + side + 18} textAnchor="middle" fontFamily={SANS} fontSize={10.5} fill={INK3}>
        {`your move: the ${tree.nextDisc} goes in column ${shown}`}
      </text>

      {/* Frame 2: the resolved board, ringed, with a badge for its value. */}
      <rect x={frames[1] - 7} y={boardY - 7} width={side + 14} height={side + 14} rx={10} fill={ACCENT} opacity={0} data-anim="flash" />
      <rect x={frames[1] - 7} y={boardY - 7} width={side + 14} height={side + 14} rx={10} fill="none" stroke={ACCENT} strokeWidth={2} />
      <Board7 cells={node.board} x={frames[1]} y={boardY} s={s} />
      <g transform={`translate(${frames[1] + side / 2 - 34} ${boardY - 30})`}>
        <rect width={68} height={18} rx={9} fill={RAISED} stroke={ACCENT} strokeWidth={1.2} />
        <text x={34} y={12.5} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={ACCENT}>
          one value
        </text>
      </g>
      <text x={frames[1] + side / 2} y={boardY + side + 18} textAnchor="middle" fontFamily={SANS} fontSize={10.5} fill={INK3}>
        after every clear and fall has finished
      </text>

      {/* Frame 3: the same board, waiting on the deal. */}
      <Board7 cells={node.board} x={frames[2]} y={boardY} s={s} />
      <g transform={`translate(${frames[2] + col * s} ${hoverY})`}>
        <g data-anim="deal">
          <UnknownDisc s={s} />
        </g>
      </g>
      <text x={frames[2] + side / 2} y={boardY + side + 18} textAnchor="middle" fontFamily={SANS} fontSize={10.5} fill={INK3}>
        before the next disc is dealt
      </text>

      {/* Arrows between the frames. */}
      <path d={`M${frames[0] + side + 10} ${midY}H${frames[1] - 18}`} stroke={INK2} strokeWidth={1.4} fill="none" markerEnd="url(#primer-afterstate-arrow)" />
      <text x={(frames[0] + side + frames[1]) / 2} y={midY - 8} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>
        drop
      </text>
      <path d={`M${frames[1] + side + 10} ${midY}H${frames[2] - 18}`} stroke={INK2} strokeWidth={1.4} fill="none" markerEnd="url(#primer-afterstate-arrow)" />
      <text x={(frames[1] + side + frames[2]) / 2} y={midY - 8} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>
        deal
      </text>

      <text x={280} y={214} textAnchor="middle" fontFamily={SANS} fontSize={10.5} fill={INK1}>
        the evaluator is shown the ringed board and nothing else about the move
      </text>
    </Fig>
  );
}

/* =========================================================================
 * 2. Seven afterstates, one evaluator, one ordering.
 * ========================================================================= */

export function AfterstateSevenBoards() {
  const tree = loadTree();
  if (!tree) return <Missing />;
  const legal = tree.columns.filter((c): c is ResolvedColumn => c.legal && typeof c.board === "string");
  if (legal.length === 0) return <Missing />;

  // Columns that leave the same board are one afterstate and share one value.
  const groups = new Map<string, ResolvedColumn[]>();
  for (const c of legal) {
    const list = groups.get(c.board) ?? [];
    list.push(c);
    groups.set(c.board, list);
  }
  const ordered = [...groups.values()].sort((a, b) => (b[0].fair ?? 0) - (a[0].fair ?? 0));

  const rootS = 10;
  const rootX = 245;
  const rootY = 14;
  const s = 8;
  const boardY = 108;
  const pitch = 66;
  const firstX = 48;
  const boxX = 174;
  const boxW = 212;
  const boxY = 202;
  const boxH = 40;
  const slotGap = 8;
  const slotW = Math.min(96, (512 - (ordered.length - 1) * slotGap) / ordered.length);
  const slotsX = 280 - (ordered.length * slotW + (ordered.length - 1) * slotGap) / 2;
  const slotY = 266;

  const columnsLabel = (list: ResolvedColumn[]) =>
    list.length === 1 ? `column ${list[0].column + 1}` : `columns ${list.map((c) => c.column + 1).join(", ")}`;

  return (
    <Fig
      viewBox="0 0 560 322"
      label="One position at the top fans out to the board each legal column leaves behind; every one of those boards feeds a single evaluator box that is never told the column, and the evaluator's numbers put the boards in order"
      caption={`The same position, with a ${tree.nextDisc} to drop, and the board each of its ${legal.length} legal columns leaves behind, read from the concept pages' scenario file. Columns that leave the same board are one afterstate and share one slot in the ordering, so ${legal.length} moves make ${ordered.length} afterstates here. One evaluator is shown every afterstate without being told which column produced it and returns one number each; the number here is the scenario file's one-move fair look-ahead value (the average, over the seven possible next discs, of the best reply's points), standing in for a learned value. The highlight steps across the boards in turn.`}
    >
      <defs>
        <marker id="primer-afterstate-arrow-2" viewBox="0 0 10 10" refX={9} refY={5} markerWidth={7} markerHeight={7} orient="auto-start-reverse">
          <path d="M0 0L10 5L0 10z" fill={INK2} />
        </marker>
      </defs>

      {/* The position. */}
      <text x={rootX - 12} y={rootY + 40} textAnchor="end" fontFamily={SANS} fontSize={11} fill={INK2}>
        the position
      </text>
      <Board7 cells={tree.board} x={rootX} y={rootY} s={rootS} />
      <text x={rootX + 7 * rootS + 12} y={rootY + 30} fontFamily={MONO} fontSize={9} fill={INK3}>
        next disc
      </text>
      <CellGlyph cell={tree.nextDisc} x={rootX + 7 * rootS + 12} y={rootY + 36} s={14} />

      {/* Fan to every legal afterstate. */}
      {legal.map((c, i) => {
        const bx = firstX + i * pitch;
        return <line key={`fan-${c.column}`} x1={rootX + 3.5 * rootS} y1={rootY + 7 * rootS + 2} x2={bx + 3.5 * s} y2={boardY - 3} stroke={RULE} strokeWidth={1.2} />;
      })}

      {/* The stepping highlight rests on the first board. */}
      <rect x={firstX - 5} y={boardY - 5} width={7 * s + 10} height={7 * s + 10} rx={6} fill={ACCENT} opacity={0.14} data-anim="scan" />

      {/* The afterstates. */}
      {legal.map((c, i) => {
        const bx = firstX + i * pitch;
        return (
          <g key={`board-${c.column}`}>
            <Board7 cells={c.board} x={bx} y={boardY} s={s} />
            <text x={bx + 3.5 * s} y={boardY + 7 * s + 14} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>
              {`column ${c.column + 1}`}
            </text>
            <line x1={bx + 3.5 * s} y1={boardY + 7 * s + 20} x2={boxX + 22 + i * ((boxW - 44) / Math.max(1, legal.length - 1))} y2={boxY} stroke={RULE} strokeWidth={1.2} />
          </g>
        );
      })}

      {/* One evaluator. */}
      <rect x={boxX} y={boxY} width={boxW} height={boxH} rx={8} fill={RAISED} stroke={ACCENT} strokeWidth={1.4} />
      <text x={boxX + boxW / 2} y={boxY + 17} textAnchor="middle" fontFamily={SANS} fontSize={12} fontWeight={600} fill={INK}>
        one evaluator
      </text>
      <text x={boxX + boxW / 2} y={boxY + 32} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>
        never told the column
      </text>
      <path d={`M280 ${boxY + boxH}V${slotY - 6}`} stroke={INK2} strokeWidth={1.4} fill="none" markerEnd="url(#primer-afterstate-arrow-2)" />
      <text x={24} y={slotY - 8} fontFamily={MONO} fontSize={9.5} fill={INK2}>
        an ordering, best on the left
      </text>

      {/* The ordering: one slot per distinct afterstate. */}
      {ordered.map((list, k) => {
        const x = slotsX + k * (slotW + slotGap);
        const value = list[0].fair;
        return (
          <g key={list[0].board}>
            <rect x={x} y={slotY} width={slotW} height={38} rx={6} fill={RAISED} stroke={RULE} />
            <text x={x + slotW / 2} y={slotY + 16} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fill={INK}>
              {typeof value === "number" ? fmt(value) : "?"}
            </text>
            <text x={x + slotW / 2} y={slotY + 30} textAnchor="middle" fontFamily={MONO} fontSize={8.5} fill={INK3}>
              {columnsLabel(list)}
            </text>
          </g>
        );
      })}
    </Fig>
  );
}

/* =========================================================================
 * 3. Label stability: do two halves of the futures agree on the ranking?
 * ========================================================================= */

interface Fan {
  name: string;
  /** How long each imagined future lasted, as a fraction of the longest drawn. */
  ends: number[];
}

/** Eight toy futures per afterstate, chosen so the two halves rank the three differently. */
const FEW: Fan[] = [
  { name: "A", ends: [0.9, 0.3, 0.85, 0.2, 0.95, 0.9, 0.8, 0.85] },
  { name: "B", ends: [0.8, 0.9, 0.7, 0.85, 0.2, 0.3, 0.9, 0.25] },
  { name: "C", ends: [0.4, 0.6, 0.5, 0.35, 0.6, 0.7, 0.55, 0.65] },
];

/** Many toy futures per afterstate from a fixed generator, spread evenly around a centre. */
function toyFutures(center: number, count: number, seed: number): number[] {
  let a = seed >>> 0;
  const out: number[] = [];
  for (let i = 0; i < count; i += 1) {
    a = (Math.imul(a, 1664525) + 1013904223) >>> 0;
    out.push(center + (a / 4294967296 - 0.5) * 0.16);
  }
  return out;
}

const MANY: Fan[] = [
  { name: "A", ends: toyFutures(0.82, 32, 11) },
  { name: "B", ends: toyFutures(0.6, 32, 23) },
  { name: "C", ends: toyFutures(0.38, 32, 37) },
];

function halfMean(ends: number[], half: 0 | 1): number {
  const n = ends.length / 2;
  const part = ends.slice(half * n, half * n + n);
  return part.reduce((sum, v) => sum + v, 0) / part.length;
}

function ranking(fans: Fan[], half: 0 | 1): string {
  return [...fans]
    .sort((a, b) => halfMean(b.ends, half) - halfMean(a.ends, half))
    .map((fan) => fan.name)
    .join(", ");
}

function StabilityPanel({ x, title, sub, fans, verdict, agree }: { x: number; title: string; sub: string; fans: Fan[]; verdict: string; agree: boolean }) {
  const startX = x + 96;
  const maxLen = 160;
  const rows = [66, 114, 162];
  return (
    <g>
      <text x={x + 14} y={22} fontFamily={SANS} fontSize={11.5} fill={INK2}>
        {title}
      </text>
      <text x={x + 14} y={36} fontFamily={MONO} fontSize={9} fill={INK3}>
        {sub}
      </text>
      {fans.map((fan, r) => {
        const yc = rows[r];
        const n = fan.ends.length;
        const spread = n > 8 ? 0.9 : 3.6;
        const opacity = n > 8 ? 0.55 : 0.85;
        return (
          <g key={fan.name}>
            <text x={x + 14} y={yc + 4} fontFamily={MONO} fontSize={10} fill={INK1}>
              {`afterstate ${fan.name}`}
            </text>
            <circle cx={startX} cy={yc} r={3.5} fill={ACCENT} />
            {([0, 1] as const).map((half) => (
              <g key={half} data-anim={half === 0 ? "half-1" : "half-2"}>
                {fan.ends.slice((half * n) / 2, ((half + 1) * n) / 2).map((f, k) => {
                  const i = (half * n) / 2 + k;
                  const x2 = startX + f * maxLen;
                  const y2 = yc + (i - (n - 1) / 2) * spread;
                  return (
                    <line
                      key={i}
                      x1={startX}
                      y1={yc}
                      x2={x2}
                      y2={y2}
                      stroke={half === 0 ? HALF_1 : HALF_2}
                      strokeWidth={1}
                      strokeOpacity={opacity}
                      pathLength={1}
                      strokeDasharray="1"
                      data-anim="future"
                      style={{ animationDelay: `${i * 0.02}s` }}
                    />
                  );
                })}
              </g>
            ))}
          </g>
        );
      })}
      <text x={x + 14} y={208} fontFamily={MONO} fontSize={10} fill={HALF_1}>
        {`first half ranks ${ranking(fans, 0)}`}
      </text>
      <text x={x + 14} y={226} fontFamily={MONO} fontSize={10} fill={HALF_2}>
        {`second half ranks ${ranking(fans, 1)}`}
      </text>
      <text x={x + 14} y={254} fontFamily={SANS} fontSize={11} fill={agree ? AGREE : DISAGREE}>
        {verdict}
      </text>
    </g>
  );
}

export function AfterstateLabelStability() {
  return (
    <Fig
      viewBox="0 0 560 300"
      label="Two panels of three afterstates, each with thin lines for its imagined futures; on the left, eight futures per afterstate split into two halves whose rankings disagree, on the right many futures whose halves agree"
      caption="Toy futures for three afterstates at one position, drawn as thin lines whose length is how long the imagined game lasted. On the left each afterstate was played out eight times; the first four futures are in one colour and the last four in the other, and the two halves rank the three afterstates differently, so the label cannot be trusted. On the right each afterstate was played out many more times, and the two halves agree. The lines redraw and the two halves take turns standing out; under reduced motion everything is shown at once."
    >
      <StabilityPanel
        x={0}
        title="eight futures per afterstate"
        sub="line length: how long the imagined game lasted"
        fans={FEW}
        verdict="the halves disagree: the label is noise"
        agree={false}
      />
      <line x1={280} y1={12} x2={280} y2={262} stroke={RULE} />
      <StabilityPanel
        x={290}
        title="many futures per afterstate"
        sub="the same three afterstates, played out many more times"
        fans={MANY}
        verdict="the halves agree: the label holds still"
        agree
      />
      <g transform="translate(14 282)">
        <line x1={0} y1={0} x2={22} y2={0} stroke={HALF_1} strokeWidth={1.6} />
        <text x={28} y={3.5} fontFamily={MONO} fontSize={9.5} fill={INK3}>
          first half of the futures
        </text>
        <line x1={170} y1={0} x2={192} y2={0} stroke={HALF_2} strokeWidth={1.6} />
        <text x={198} y={3.5} fontFamily={MONO} fontSize={9.5} fill={INK3}>
          second half
        </text>
      </g>
    </Fig>
  );
}
