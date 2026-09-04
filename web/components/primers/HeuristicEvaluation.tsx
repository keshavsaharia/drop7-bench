/**
 * Figures for the heuristic-evaluation primer
 * (web/content/learn/techniques/heuristic-evaluation.mdx).
 *
 * The toy is the tower game from the primer: three columns, one block per
 * turn, a point for landing level with a neighbour, and a loss when any
 * column reaches height five. The evaluator counts the tallest column,
 * the columns at height four, and the level pairs, with weights −3, −10
 * and +2. Every number here is that toy's arithmetic; nothing is a Drop7
 * position or a research result. Server components: SVG with CSS keyframes
 * in ./heuristic-evaluation.css on elements marked data-anim.
 */
import type { ReactNode } from "react";
import "./heuristic-evaluation.css";

const MONO = "var(--font-mono)";
const SANS = "var(--font-sans)";
const INK = "var(--color-ink)";
const INK1 = "var(--color-ink-1)";
const INK2 = "var(--color-ink-2)";
const INK3 = "var(--color-ink-3)";
const RULE = "var(--color-rule)";
const RULE_STRONG = "var(--color-rule-strong)";
const SURFACE = "var(--color-surface)";
const CELL = "var(--color-cell)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const BLOCK = "var(--color-series-1)";
const HEIGHT = "var(--color-series-6)";
const DANGER = "var(--color-series-7)";
const PAIR = "var(--color-series-3)";

const CELL_SIZE = 22;
const GAP = 4;
const STEP = CELL_SIZE + GAP;
const ROWS = 5;

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-heuristic">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/**
 * A three-column tower board with its corner at (x, y). Row 0 is the bottom;
 * row 4 is the losing row, drawn as a dashed outline. `fresh` marks the block
 * a candidate move would add.
 */
function Tower({ x, y, heights, fresh, cell = CELL_SIZE }: { x: number; y: number; heights: number[]; fresh?: { column: number; row: number }; cell?: number }) {
  const step = cell + GAP;
  const top = (row: number) => y + (ROWS - 1 - row) * step;
  return (
    <g>
      {heights.map((_, column) => (
        <g key={column}>
          {Array.from({ length: ROWS }, (_, row) => (
            <rect key={row} x={x + column * step} y={top(row)} width={cell} height={cell} rx={3} fill={CELL} stroke={row === ROWS - 1 ? DANGER : RULE} strokeDasharray={row === ROWS - 1 ? "3 3" : undefined} />
          ))}
          {Array.from({ length: heights[column] }, (_, row) => {
            const isFresh = fresh && fresh.column === column && fresh.row === row;
            return <rect key={`b${row}`} x={x + column * step + 1} y={top(row) + 1} width={cell - 2} height={cell - 2} rx={3} fill={isFresh ? ACCENT : BLOCK} />;
          })}
        </g>
      ))}
    </g>
  );
}

/* =========================================================================
 * 1. Three counters sweep a board and their weighted sum forms.
 * ========================================================================= */

export function HeuristicThreeCounters({ caption }: { caption?: string }) {
  const bx = 30;
  const by = 44;
  const boardW = 3 * STEP - GAP;
  const boardBottom = by + ROWS * STEP - GAP;
  const heightFourTop = by + (ROWS - 4) * STEP;
  const rows = [
    { key: "row-1", y: 78, label: "tallest column", value: "4 × −3 = −12", color: HEIGHT },
    { key: "row-2", y: 110, label: "columns at height four", value: "1 × −10 = −10", color: DANGER },
    { key: "row-3", y: 142, label: "level pairs", value: "1 × +2 = +2", color: PAIR },
  ];
  return (
    <Fig
      viewBox="0 0 560 250"
      label="A tower board with three columns of heights 4, 3 and 3, three counters, and their weighted sum of −20"
      caption={
        caption ??
        "A tower board with columns of height 4, 3 and 3, and the three things the toy evaluator counts. The tallest column is 4 (weight −3), one column is already at height four (weight −10), and the two right-hand columns make one level pair (weight +2). Multiplied and added, the board scores −20. The dashed row is height five, where the game is lost."
      }
    >
      <text x={bx} y={30} fontFamily={MONO} fontSize={10} fill={INK3}>height 5 loses</text>
      <Tower x={bx} y={by} heights={[4, 3, 3]} />
      {[1, 2, 3].map((c) => (
        <text key={c} x={bx + (c - 1) * STEP + CELL_SIZE / 2} y={boardBottom + 14} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK3}>
          {c}
        </text>
      ))}
      {/* counter 1: the tallest-column ruler, slides up to the top of height 4 */}
      <g data-anim="ruler-height">
        <line x1={bx - 8} y1={heightFourTop} x2={bx + boardW + 8} y2={heightFourTop} stroke={HEIGHT} strokeWidth={2} strokeDasharray="5 3" />
        <text x={bx + boardW + 12} y={heightFourTop + 4} fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={HEIGHT}>tallest 4</text>
      </g>
      {/* counter 2: the column already at height four */}
      <g data-anim="ruler-danger">
        <rect x={bx - 2} y={heightFourTop - 2} width={CELL_SIZE + 4} height={4 * STEP} rx={4} fill={DANGER} fillOpacity={0.18} stroke={DANGER} strokeWidth={1.5} />
        <text x={bx + CELL_SIZE / 2} y={heightFourTop - 8} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={DANGER}>at four</text>
      </g>
      {/* counter 3: the level pair (columns 2 and 3) */}
      <g data-anim="ruler-pair">
        <path d={`M${bx + STEP} ${boardBottom + 22} v6 h${2 * STEP - GAP} v-6`} fill="none" stroke={PAIR} strokeWidth={2} />
        <text x={bx + STEP + (2 * STEP - GAP) / 2} y={boardBottom + 42} textAnchor="middle" fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={PAIR}>level pair</text>
      </g>
      {/* the sum */}
      <text x={230} y={52} fontFamily={SANS} fontSize={12} fontWeight={700} fill={INK}>count, multiply by the weight, add</text>
      {rows.map((r) => (
        <g key={r.key}>
          <rect data-anim={r.key} x={220} y={r.y - 17} width={330} height={26} rx={5} fill={ACCENT_SOFT} />
          <circle cx={232} cy={r.y - 4} r={4} fill={r.color} />
          <text x={244} y={r.y} fontFamily={SANS} fontSize={12} fill={INK1}>{r.label}</text>
          <text x={540} y={r.y} textAnchor="end" fontFamily={MONO} fontSize={12} fill={INK}>{r.value}</text>
        </g>
      ))}
      <line x1={230} y1={158} x2={540} y2={158} stroke={RULE_STRONG} />
      <rect data-anim="row-total" x={220} y={166} width={330} height={28} rx={5} fill={ACCENT_SOFT} />
      <text x={244} y={185} fontFamily={SANS} fontSize={12.5} fontWeight={700} fill={INK}>score for this board</text>
      <text x={540} y={185} textAnchor="end" fontFamily={MONO} fontSize={14} fontWeight={700} fill={ACCENT}>−20</text>
      <text x={230} y={216} fontFamily={SANS} fontSize={11} fill={INK3}>The weights −3, −10 and +2 were chosen by hand. Change one and the ranking of boards changes.</text>
    </Fig>
  );
}

/* =========================================================================
 * 2. One weight on a dial; the chosen move flips as it turns.
 * ========================================================================= */

export function HeuristicWeightDial({ caption }: { caption?: string }) {
  const small = 18;
  const trackX = 120;
  const trackW = 320;
  const trackY = 250;
  const ticks = Array.from({ length: 10 }, (_, i) => -(i + 1));
  const tickX = (w: number) => trackX + ((-w - 1) / 9) * trackW;
  return (
    <Fig
      viewBox="0 0 560 290"
      label="Two candidate moves from a board of heights 4, 3 and 1, and a slider for the danger weight that decides between them"
      caption={
        caption ??
        "From heights 4, 3, 1 there are two moves that do not lose. Placing on column 2 lands level with column 1 and scores a point, but leaves two columns at height four. Placing on column 3 scores nothing and leaves one. With the danger weight at −1 the formula prefers the point (−12 against −13); from −3 down it prefers the safer board (at −10, −30 against −22). The slider is the only thing that changed."
      }
    >
      {/* the position now */}
      <text x={24} y={38} fontFamily={MONO} fontSize={10.5} fill={INK3}>now: 4, 3, 1</text>
      <Tower x={24} y={48} heights={[4, 3, 1]} cell={small} />
      <text x={100} y={110} fontFamily={MONO} fontSize={14} fill={INK3}>→</text>

      {/* candidate: place on column 2 */}
      <g>
        <rect data-anim="pick-left" x={140} y={26} width={170} height={172} rx={8} fill="none" stroke={ACCENT} strokeWidth={2} />
        <text x={150} y={42} fontFamily={SANS} fontSize={11.5} fontWeight={700} fill={INK}>place on column 2 → 4, 4, 1</text>
        <Tower x={150} y={52} heights={[4, 4, 1]} fresh={{ column: 1, row: 3 }} cell={small} />
        <text x={228} y={76} fontFamily={SANS} fontSize={10.5} fill={INK2}>scores a point</text>
        <text x={228} y={92} fontFamily={SANS} fontSize={10.5} fill={INK2}>two columns at four</text>
        <text x={228} y={116} fontFamily={MONO} fontSize={10.5} fill={INK1}>at −1: −12</text>
        <text x={228} y={132} fontFamily={MONO} fontSize={10.5} fill={INK1}>at −10: −30</text>
        <text x={150} y={188} fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={ACCENT}>chosen at −1</text>
      </g>

      {/* candidate: place on column 3 */}
      <g>
        <rect data-anim="pick-right" x={340} y={26} width={190} height={172} rx={8} fill="none" stroke={ACCENT} strokeWidth={2} />
        <text x={350} y={42} fontFamily={SANS} fontSize={11.5} fontWeight={700} fill={INK}>place on column 3 → 4, 3, 2</text>
        <Tower x={350} y={52} heights={[4, 3, 2]} fresh={{ column: 2, row: 1 }} cell={small} />
        <text x={428} y={76} fontFamily={SANS} fontSize={10.5} fill={INK2}>no point</text>
        <text x={428} y={92} fontFamily={SANS} fontSize={10.5} fill={INK2}>one column at four</text>
        <text x={428} y={116} fontFamily={MONO} fontSize={10.5} fill={INK1}>at −1: −13</text>
        <text x={428} y={132} fontFamily={MONO} fontSize={10.5} fill={INK1}>at −10: −22</text>
        <text x={350} y={188} fontFamily={MONO} fontSize={10.5} fontWeight={700} fill={ACCENT}>chosen from −3 down</text>
      </g>

      {/* the dial */}
      <text x={trackX} y={224} fontFamily={SANS} fontSize={11} fill={INK2}>weight per column at height four (the tallest-column and level-pair weights stay at −3 and +2)</text>
      <line x1={trackX} y1={trackY} x2={trackX + trackW} y2={trackY} stroke={RULE_STRONG} strokeWidth={3} strokeLinecap="round" />
      {ticks.map((w) => (
        <g key={w}>
          <line x1={tickX(w)} y1={trackY - 5} x2={tickX(w)} y2={trackY + 5} stroke={INK3} />
          <text x={tickX(w)} y={trackY + 22} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>{w}</text>
        </g>
      ))}
      <line x1={tickX(-2)} y1={trackY - 14} x2={tickX(-2)} y2={trackY + 8} stroke={ACCENT} strokeDasharray="2 2" />
      <text x={tickX(-2)} y={trackY - 18} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={ACCENT}>tie at −2</text>
      <circle data-anim="knob" cx={trackX} cy={trackY} r={8} fill={ACCENT} stroke={SURFACE} strokeWidth={2} />
    </Fig>
  );
}

/* =========================================================================
 * 3. Two boards with identical counts and different futures.
 * ========================================================================= */

export function HeuristicLookalikes({ caption }: { caption?: string }) {
  const small = 18;
  const boards = [
    { heights: [4, 3, 3], label: "4, 3, 3", room: 2, y: 40 },
    { heights: [4, 2, 2], label: "4, 2, 2", room: 4, y: 40 },
  ];
  return (
    <Fig
      viewBox="0 0 560 230"
      label="Two tower boards with the same three counts and score, and the number of blocks each can still take"
      caption={
        caption ??
        "Heights 4, 3, 3 and heights 4, 2, 2 have the same three counts (tallest 4, one column at four, one level pair) and the same score, −20. The formula cannot tell them apart. The first board has room for two more blocks before a column must reach five; the second has room for four. What the counts leave out, the score leaves out."
      }
    >
      {boards.map((b, i) => (
        <g key={b.label} transform={`translate(${30 + i * 110} 0)`}>
          <text x={0} y={30} fontFamily={MONO} fontSize={10.5} fill={INK3}>{b.label}</text>
          <Tower x={0} y={b.y} heights={b.heights} cell={small} />
        </g>
      ))}
      <text x={30} y={168} fontFamily={MONO} fontSize={10} fill={INK2}>tallest 4 · at four 1 · pairs 1</text>
      <text x={30} y={184} fontFamily={MONO} fontSize={11} fontWeight={700} fill={INK}>both score −20</text>

      {/* futures */}
      <text x={280} y={30} fontFamily={SANS} fontSize={11.5} fontWeight={700} fill={INK}>blocks that can still be placed</text>
      {boards.map((b, i) => {
        const y = 66 + i * 64;
        return (
          <g key={b.label}>
            <text x={280} y={y - 14} fontFamily={MONO} fontSize={10.5} fill={INK3}>{b.label}</text>
            {Array.from({ length: 4 }, (_, k) => (
              <rect key={k} x={280 + k * 30} y={y} width={22} height={22} rx={3} fill={k < b.room ? BLOCK : CELL} stroke={k < b.room ? BLOCK : RULE} />
            ))}
            <g transform={`translate(${280 + b.room * 30 + 4} ${y})`}>
              <rect x={0} y={0} width={22} height={22} rx={3} fill={CELL} stroke={DANGER} strokeDasharray="3 3" />
              <path d="M6 6 L16 16 M16 6 L6 16" stroke={DANGER} strokeWidth={2} strokeLinecap="round" />
            </g>
            <text x={280 + b.room * 30 + 34} y={y + 15} fontFamily={SANS} fontSize={11} fill={INK2}>
              {b.room === 2 ? "then any placement loses" : "twice the room"}
            </text>
          </g>
        );
      })}
      <line x1={250} y1={44} x2={250} y2={200} stroke={RULE} />
    </Fig>
  );
}
