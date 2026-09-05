/**
 * Figures for the Q-learning primer (web/content/learn/techniques/q-learning.mdx).
 *
 * The toy is the four-cell corridor from the primer: start at cell 1, moving
 * right is free, cell 4 pays 10 and ends the round, stepping left off cell 1
 * is a cliff that pays −10. The staircase of values (8.1, 9, 10) is that
 * toy's arithmetic with a discount of nine tenths. Nothing here is a Drop7
 * position or a research number. Server components: SVG with CSS keyframes
 * in ./q-learning.css on elements marked data-anim.
 */
import type { ReactNode } from "react";
import "./q-learning.css";

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
const ACCENT = "var(--color-accent)";
const VALUE = "var(--color-series-1)";
const AGENT = "var(--color-series-6)";
const CLIFF = "var(--color-series-7)";
const GOAL = "var(--color-series-3)";
const LAG = "var(--color-series-2)";

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-qlearning">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/* =========================================================================
 * 1. The corridor: an agent walks right, the goal pays, and value creeps
 *    back one cell per round.
 * ========================================================================= */

export function QLearningCorridor({ caption }: { caption?: string }) {
  const cellX = (i: number) => 84 + i * 108;
  const centre = (i: number) => cellX(i) + 45;
  const baseY = 210;
  const maxH = 70;
  const bars = [
    { cell: 0, value: 8.1, key: "bar-1" },
    { cell: 1, value: 9, key: "bar-2" },
    { cell: 2, value: 10, key: "bar-3" },
  ];
  return (
    <Fig
      viewBox="0 0 560 250"
      label="A four-cell corridor with a cliff on the left and a goal on the right, and a bar under each cell for the value of moving right"
      caption={
        caption ??
        "The four-cell corridor. The bars are the learned value of moving right from each cell. The first time the agent reaches the goal, the bar under cell 3 grows toward 10. On the next round the bar under cell 2 grows toward the best value available at cell 3, and so on backward down the corridor: each round moves the news one cell further from the goal. With a discount of nine tenths the values settle at 10, 9 and 8.1."
      }
    >
      {/* cliff */}
      <rect x={20} y={50} width={50} height={56} rx={6} fill={CLIFF} fillOpacity={0.16} stroke={CLIFF} strokeDasharray="4 3" />
      <text x={45} y={74} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={CLIFF}>cliff</text>
      <text x={45} y={90} textAnchor="middle" fontFamily={MONO} fontSize={11} fontWeight={700} fill={CLIFF}>−10</text>
      {/* cells */}
      {[0, 1, 2, 3].map((i) => (
        <g key={i}>
          <rect x={cellX(i)} y={50} width={90} height={56} rx={6} fill={SURFACE} stroke={i === 3 ? GOAL : RULE_STRONG} strokeWidth={i === 3 ? 2 : 1} />
          <text x={cellX(i) + 8} y={64} fontFamily={MONO} fontSize={10} fill={INK3}>cell {i + 1}</text>
          {i === 3 && (
            <>
              <text x={cellX(i) + 45} y={84} textAnchor="middle" fontFamily={MONO} fontSize={12} fontWeight={700} fill={GOAL}>+10</text>
              <text x={cellX(i) + 45} y={98} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>round ends</text>
            </>
          )}
          {i < 3 && <text x={cellX(i) + 82} y={82} textAnchor="end" fontFamily={MONO} fontSize={11} fill={INK3}>→</text>}
        </g>
      ))}
      <text x={cellX(0)} y={126} fontFamily={SANS} fontSize={10.5} fill={INK3}>moving right is free; the agent starts in cell 1</text>
      {/* the agent, drawn at cell 3 and moved by the animation */}
      <circle data-anim="agent" cx={centre(2)} cy={82} r={9} fill={AGENT} stroke={SURFACE} strokeWidth={2} />
      {/* value bars: a ghost of the final staircase, then the animated fill */}
      <line x1={cellX(0) - 10} y1={baseY} x2={cellX(3) + 90} y2={baseY} stroke={RULE_STRONG} />
      {bars.map((b) => {
        const h = (b.value / 10) * maxH;
        const x = centre(b.cell) - 14;
        return (
          <g key={b.key}>
            <rect x={x} y={baseY - h} width={28} height={h} rx={3} fill={VALUE} fillOpacity={0.22} />
            <rect data-anim={b.key} x={x} y={baseY - h} width={28} height={h} rx={3} fill={VALUE} />
            <text x={centre(b.cell)} y={baseY + 16} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK2}>
              Q(cell {b.cell + 1}, right)
            </text>
            <text x={centre(b.cell)} y={baseY + 30} textAnchor="middle" fontFamily={MONO} fontSize={11} fontWeight={700} fill={INK}>
              {b.value}
            </text>
          </g>
        );
      })}
      <text x={centre(3)} y={baseY + 16} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK3}>no move from here</text>
      {/* backups: the value flows one cell to the left */}
      {[
        { key: "backup-2", from: 2, to: 1 },
        { key: "backup-1", from: 1, to: 0 },
      ].map((a) => (
        <g key={a.key} data-anim={a.key}>
          <path d={`M${centre(a.from) - 18} 150 H${centre(a.to) + 24}`} stroke={ACCENT} strokeWidth={2} fill="none" />
          <path d={`M${centre(a.to) + 32} 150 l8 -5 v10 z`} fill={ACCENT} />
          <text x={(centre(a.from) + centre(a.to)) / 2} y={144} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={ACCENT}>backup</text>
        </g>
      ))}
    </Fig>
  );
}

/* =========================================================================
 * 2. Three places to keep the numbers: a table, a few weighted features, or
 *    a network. Same input, same output shape.
 * ========================================================================= */

export function QLearningThreeShapes({ caption }: { caption?: string }) {
  const table = [
    { cell: "cell 1", left: "−10", right: "8.1" },
    { cell: "cell 2", left: "7.3", right: "9" },
    { cell: "cell 3", left: "8.1", right: "10" },
    { cell: "cell 4", left: "·", right: "·" },
  ];
  const inputs = [70, 96, 122, 148];
  const hidden = [60, 82, 104, 126, 148];
  const outputs = [90, 130];
  return (
    <Fig
      viewBox="0 0 560 236"
      label="Three ways to hold action values: a table of numbers, two weighted features, and a small neural network"
      caption={
        caption ??
        "Three places to keep the same numbers. A table has one entry per situation and action, which is fine for eight numbers and impossible for a board. A linear model describes the situation with a few measurements and learns one weight each. A network learns its own measurements. In every case the input is where you are and the output is one number per action, and the same nudge rule trains all three."
      }
    >
      {[
        { title: "a table", sub: "one entry per cell and action" },
        { title: "a few features", sub: "measurements × learned weights" },
        { title: "a network", sub: "learned measurements, layered" },
      ].map((p, i) => (
        <g key={p.title} transform={`translate(${i * 192} 0)`}>
          <rect x={0} y={4} width={176} height={226} rx={8} fill={SURFACE} stroke={RULE} />
          <text x={12} y={24} fontFamily={SANS} fontSize={13} fontWeight={700} fill={INK}>{p.title}</text>
          <text x={12} y={39} fontFamily={MONO} fontSize={9.5} fill={INK3}>{p.sub}</text>
          <text x={88} y={54} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>where am I ↓</text>
          <text x={88} y={218} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>↓ one number per action</text>
        </g>
      ))}
      {/* panel 1: the table */}
      <g transform="translate(0 0)">
        <text x={80} y={76} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>left</text>
        <text x={132} y={76} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>right</text>
        {table.map((row, r) => (
          <g key={row.cell} transform={`translate(0 ${86 + r * 28})`}>
            <rect x={14} y={0} width={148} height={24} rx={4} fill={RAISED} />
            <text x={22} y={16} fontFamily={MONO} fontSize={10} fill={INK3}>{row.cell}</text>
            <text x={80} y={16} textAnchor="middle" fontFamily={MONO} fontSize={11} fill={INK}>{row.left}</text>
            <text x={132} y={16} textAnchor="middle" fontFamily={MONO} fontSize={11} fill={INK}>{row.right}</text>
          </g>
        ))}
      </g>
      {/* panel 2: two dials */}
      <g transform="translate(192 0)">
        {[
          { label: "distance to goal", w: "w₁", cx: 50, angle: -40 },
          { label: "distance to cliff", w: "w₂", cx: 126, angle: 30 },
        ].map((d) => (
          <g key={d.label}>
            <circle cx={d.cx} cy={112} r={26} fill={RAISED} stroke={RULE_STRONG} />
            <line x1={d.cx} y1={112} x2={d.cx + 20 * Math.cos(((d.angle - 90) * Math.PI) / 180)} y2={112 + 20 * Math.sin(((d.angle - 90) * Math.PI) / 180)} stroke={ACCENT} strokeWidth={2.5} strokeLinecap="round" />
            <circle cx={d.cx} cy={112} r={3} fill={ACCENT} />
            <text x={d.cx} y={152} textAnchor="middle" fontFamily={SANS} fontSize={9.5} fill={INK2}>{d.label}</text>
            <text x={d.cx} y={165} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>× {d.w}</text>
          </g>
        ))}
        <text x={88} y={118} textAnchor="middle" fontFamily={MONO} fontSize={16} fill={INK3}>+</text>
        <text x={88} y={190} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK1}>weighted sum = value</text>
      </g>
      {/* panel 3: a small net */}
      <g transform="translate(384 0)">
        {inputs.map((y) => hidden.map((h) => <line key={`${y}-${h}`} x1={44} y1={y} x2={88} y2={h} stroke={RULE_STRONG} strokeWidth={0.8} />))}
        {hidden.map((h) => outputs.map((o) => <line key={`${h}-${o}`} x1={88} y1={h} x2={132} y2={o} stroke={RULE_STRONG} strokeWidth={0.8} />))}
        {inputs.map((y) => <circle key={`i${y}`} cx={44} cy={y} r={6} fill={RAISED} stroke={INK3} />)}
        {hidden.map((h) => <circle key={`h${h}`} cx={88} cy={h} r={6} fill={RAISED} stroke={INK3} />)}
        {outputs.map((o, i) => (
          <g key={`o${o}`}>
            <circle cx={132} cy={o} r={7} fill={SURFACE} stroke={ACCENT} strokeWidth={2} />
            <text x={146} y={o + 4} fontFamily={MONO} fontSize={9.5} fill={INK2}>{i === 0 ? "left" : "right"}</text>
          </g>
        ))}
        <text x={88} y={190} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK1}>every line is a learned weight</text>
      </g>
    </Fig>
  );
}

/* =========================================================================
 * 3. Why "double": one copy chooses, a lagged copy scores.
 * ========================================================================= */

export function QLearningDouble({ caption }: { caption?: string }) {
  return (
    <Fig
      viewBox="0 0 560 200"
      label="Two copies of a value network: the live copy picks the best next action and a lagged copy scores it, refreshed every N steps"
      caption={
        caption ??
        "Double DQN keeps two copies of the network. The live copy is nudged every step and picks which next action looks best. A lagged copy, frozen between refreshes, says what that action is worth, and that number goes into the target. Because the copy that chooses is never the copy that scores, a random overestimate cannot pick itself and then confirm itself. The bar counts steps until the lagged copy is refreshed from the live one."
      }
    >
      {/* live copy */}
      <rect x={40} y={56} width={200} height={78} rx={8} fill={SURFACE} stroke={VALUE} strokeWidth={2} />
      <text x={52} y={78} fontFamily={SANS} fontSize={13} fontWeight={700} fill={INK}>live copy</text>
      <text x={52} y={96} fontFamily={SANS} fontSize={10.5} fill={INK2}>nudged every step</text>
      <text x={52} y={112} fontFamily={SANS} fontSize={10.5} fill={INK2}>picks the best next action</text>
      {/* lagged copy */}
      <rect x={320} y={56} width={200} height={78} rx={8} fill={SURFACE} stroke={LAG} strokeWidth={2} />
      <text x={332} y={78} fontFamily={SANS} fontSize={13} fontWeight={700} fill={INK}>lagged copy</text>
      <text x={332} y={96} fontFamily={SANS} fontSize={10.5} fill={INK2}>frozen between refreshes</text>
      <text x={332} y={112} fontFamily={SANS} fontSize={10.5} fill={INK2}>scores that action</text>
      {/* arrows between */}
      <path d="M244 82 H308" stroke={INK3} strokeWidth={1.5} fill="none" />
      <path d="M308 82 l-7 -4 v8 z" fill={INK3} />
      <text x={276} y={74} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>which action?</text>
      <path d="M316 112 H252" stroke={INK3} strokeWidth={1.5} fill="none" />
      <path d="M252 112 l7 -4 v8 z" fill={INK3} />
      <text x={280} y={126} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>its value → the target</text>
      {/* the copy, over the top */}
      <g data-anim="copy">
        <path d="M140 52 C140 18 420 18 420 52" stroke={ACCENT} strokeWidth={2} fill="none" strokeDasharray="5 4" />
        <path d="M420 54 l-7 -6 h10 z" fill={ACCENT} />
        <text x={280} y={30} textAnchor="middle" fontFamily={MONO} fontSize={10} fontWeight={700} fill={ACCENT}>copy the weights across</text>
      </g>
      {/* the lag counter */}
      <text x={40} y={160} fontFamily={MONO} fontSize={9.5} fill={INK3}>steps since the last refresh</text>
      <rect x={40} y={166} width={480} height={8} rx={4} fill={RAISED} />
      <rect data-anim="lag" x={40} y={166} width={480} height={8} rx={4} fill={LAG} />
      <text x={520} y={190} textAnchor="end" fontFamily={MONO} fontSize={9.5} fill={INK3}>N steps: refresh</text>
    </Fig>
  );
}
