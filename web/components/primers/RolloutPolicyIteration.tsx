/**
 * Figures for the rollouts and policy iteration primer
 * (web/content/learn/techniques/rollout-policy-iteration.mdx).
 *
 * The toy is the three-cell corridor from the primer: a cliff on the left, a
 * goal on the right, a gust that shoves the walker two cells left unless the
 * walker spends the turn bracing. Every walk drawn here is a toy walk; no
 * Drop7 position and no research number appears. Server components: SVG with
 * CSS keyframes in ./rollout-policy-iteration.css on elements marked
 * data-anim. Every figure is complete at its first frame and rests on a
 * designed frame under prefers-reduced-motion.
 */
import type { ReactNode } from "react";
import "./rollout-policy-iteration.css";

const MONO = "var(--font-mono)";
const SANS = "var(--font-sans)";
const INK = "var(--color-ink)";
const INK1 = "var(--color-ink-1)";
const INK2 = "var(--color-ink-2)";
const INK3 = "var(--color-ink-3)";
const RULE = "var(--color-rule-strong)";
const RAISED = "var(--color-raised)";
const ACCENT = "var(--color-accent)";
const RIGHT = "var(--color-series-1)";
const WAIT = "var(--color-series-3)";
const LEFT = "var(--color-series-4)";
const GUST = "var(--color-series-2)";
const GOAL = "var(--color-series-3)";
const FELL = "var(--color-series-7)";

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-rollout">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/** A gust flag on a pole: raised when a gust is blowing, hanging when it is calm. */
function Flag({ x, y, up, size = 1 }: { x: number; y: number; up: boolean; size?: number }) {
  return (
    <g transform={`translate(${x} ${y}) scale(${size})`}>
      <line x1={0} y1={0} x2={0} y2={20} stroke={INK2} strokeWidth={1.2} />
      {up ? <path d="M0 1h14l-3.5 4 3.5 4H0z" fill={GUST} /> : <path d="M0 8l4 3v6l-4-3z" fill={INK3} />}
    </g>
  );
}

/** The corridor: a cliff, three cells, a goal flag, and the walker. */
function Corridor({ x, y, walker, flagUp }: { x: number; y: number; walker: number; flagUp: boolean }) {
  const cell = 24;
  const gap = 4;
  const cellX = (i: number) => 14 + i * (cell + gap);
  return (
    <g transform={`translate(${x} ${y})`}>
      <path d="M8 0v24M8 2l-6 6M8 9l-6 6M8 16l-6 6" stroke={INK3} strokeWidth={1} fill="none" />
      {[0, 1, 2].map((i) => (
        <rect key={i} x={cellX(i)} y={0} width={cell} height={cell} rx={4} fill={RAISED} stroke={RULE} />
      ))}
      <g transform={`translate(${cellX(3) + 2} 0)`}>
        <line x1={0} y1={0} x2={0} y2={24} stroke={INK2} strokeWidth={1.2} />
        <path d="M0 2h12l-3 4 3 4H0z" fill={GOAL} />
      </g>
      <circle cx={cellX(walker) + cell / 2} cy={cell / 2} r={7} fill={ACCENT} />
      <Flag x={cellX(1) + cell / 2 - 7} y={-32} up={flagUp} />
    </g>
  );
}

/* =========================================================================
 * 1. A fan of futures: eight imagined walks per candidate first move.
 * ========================================================================= */

/** Where each of eight toy walks ended, as a fraction of the horizon; 1 means the goal was reached. */
const FAN: { name: string; color: string; ends: number[] }[] = [
  { name: "step right", color: RIGHT, ends: [1, 0.55, 1, 1, 0.3, 1, 0.7, 1] },
  { name: "wait", color: WAIT, ends: [1, 1, 1, 0.65, 1, 1, 1, 1] },
  { name: "step left", color: LEFT, ends: [0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1] },
];

export function RolloutFanOfFutures() {
  const startX = 236;
  const horizonX = 430;
  const rows = [78, 158, 238];
  return (
    <Fig
      viewBox="0 0 560 296"
      label="From one position, three candidate first moves, each played out eight times with the habit; the share of walks that reach the goal fills a bar per candidate"
      caption="From the middle cell with the flag up, each candidate first move is played out eight times with the habit (always step right) and its own run of gusts. A circle marks a walk that reached the goal before the horizon and a cross marks one that went over the cliff. The bar for each candidate is the share of its eight walks that reached the goal: the rollout value of the move."
    >
      <text x={12} y={20} fontFamily={SANS} fontSize={11.5} fill={INK2}>
        the position: middle cell, flag up
      </text>
      <Corridor x={14} y={126} walker={1} flagUp />
      <text x={12} y={176} fontFamily={MONO} fontSize={9.5} fill={INK3}>
        cliff · 3 cells · goal
      </text>

      <line x1={horizonX} y1={44} x2={horizonX} y2={266} stroke={INK3} strokeDasharray="3 4" />
      <text x={horizonX} y={36} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>
        horizon
      </text>
      <text x={502} y={36} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK3}>
        reached the goal
      </text>

      {FAN.map((candidate, r) => {
        const yc = rows[r];
        const reached = candidate.ends.filter((f) => f === 1).length;
        return (
          <g key={candidate.name}>
            <text x={224} y={yc + 4} textAnchor="end" fontFamily={MONO} fontSize={11} fill={candidate.color}>
              {candidate.name}
            </text>
            <circle cx={startX} cy={yc} r={4} fill={candidate.color} />
            {candidate.ends.map((f, i) => {
              const x2 = startX + f * (horizonX - startX);
              const y2 = yc + (i - 3.5) * 6.2;
              return (
                <g key={i}>
                  <line
                    x1={startX}
                    y1={yc}
                    x2={x2}
                    y2={y2}
                    stroke={candidate.color}
                    strokeWidth={1}
                    strokeOpacity={0.8}
                    pathLength={1}
                    strokeDasharray="1"
                    data-anim="race"
                    style={{ animationDelay: `${i * 0.06}s` }}
                  />
                  <g data-anim="mark" style={{ animationDelay: `${i * 0.06}s` }}>
                    {f === 1 ? (
                      <circle cx={x2} cy={y2} r={2.6} fill={GOAL} />
                    ) : (
                      <path d={`M${x2 - 3} ${y2 - 3}l6 6M${x2 + 3} ${y2 - 3}l-6 6`} stroke={FELL} strokeWidth={1.4} />
                    )}
                  </g>
                </g>
              );
            })}
            <text x={452} y={yc - 11} fontFamily={MONO} fontSize={9.5} fill={INK2}>
              {`${reached} of 8`}
            </text>
            <rect x={452} y={yc - 6} width={100} height={12} rx={3} fill={RAISED} stroke={RULE} />
            <rect x={452} y={yc - 6} width={(reached / 8) * 100} height={12} rx={3} fill={candidate.color} data-anim="bar" />
          </g>
        );
      })}

      <g transform="translate(236 284)">
        <circle cx={0} cy={0} r={2.6} fill={GOAL} />
        <text x={8} y={3.5} fontFamily={MONO} fontSize={9.5} fill={INK3}>
          reached the goal
        </text>
        <path d="M118 -3l6 6M124 -3l-6 6" stroke={FELL} strokeWidth={1.4} />
        <text x={130} y={3.5} fontFamily={MONO} fontSize={9.5} fill={INK3}>
          went over the cliff
        </text>
      </g>
    </Fig>
  );
}

/* =========================================================================
 * 2. Shared gusts: the same recorded future replayed for every candidate.
 * ========================================================================= */

/** One recorded run of eight turns: true is a gust, false is calm. */
const TAPE = [true, false, false, true, false, false, false, false];

/** Where the walker stands after each turn under TAPE: 0 left, 1 middle, 2 right, 3 goal, -1 cliff. */
const WALKS: { name: string; color: string; cells: number[]; outcome: string }[] = [
  { name: "step right", color: RIGHT, cells: [0, 1, 2, 1, 2, 3], outcome: "goal on turn 6" },
  { name: "wait", color: WAIT, cells: [1, 2, 3], outcome: "goal on turn 3" },
  { name: "step left", color: LEFT, cells: [-1], outcome: "cliff on turn 1" },
];

export function RolloutSharedGusts() {
  const slotX = 112;
  const slotW = 22;
  const pitch = 28;
  const rowH = 62;
  const top = 40;
  const levelY = (rowTop: number, cell: number) => rowTop + 46 - cell * 8;
  return (
    <Fig
      viewBox="0 0 560 232"
      label="Three candidate first moves, each replayed against the same recorded run of eight gusts, with the walker's path under each"
      caption="One recorded run of eight turns (a raised flag is a gust, a hanging one is calm) is replayed for every candidate. Under each tape the line shows where the walker stands after each turn, rising toward the goal flag or falling to the cross. Because the weather is identical, the difference between the three lines is the difference between the moves."
    >
      <text x={12} y={22} fontFamily={SANS} fontSize={11.5} fill={INK2}>
        the same eight turns of weather for every candidate
      </text>
      <rect x={slotX} y={top - 4} width={slotW} height={rowH * 3 + 2} rx={3} fill={ACCENT} opacity={0.14} data-anim="playhead" />

      {WALKS.map((walk, r) => {
        const rowTop = top + r * rowH;
        const points: string[] = [`${slotX - 12},${levelY(rowTop, 1)}`];
        walk.cells.forEach((cell, i) => {
          const cx = slotX + i * pitch + slotW / 2;
          const level = cell === 3 ? 2 : cell === -1 ? 0 : cell;
          points.push(`${cx},${levelY(rowTop, level)}`);
        });
        const last = walk.cells[walk.cells.length - 1];
        const lastX = slotX + (walk.cells.length - 1) * pitch + slotW / 2;
        return (
          <g key={walk.name}>
            <text x={12} y={rowTop + 15} fontFamily={MONO} fontSize={11} fill={walk.color}>
              {walk.name}
            </text>
            {TAPE.map((gust, i) => (
              <g key={i} transform={`translate(${slotX + i * pitch} ${rowTop})`}>
                <rect width={slotW} height={slotW} rx={4} fill={RAISED} stroke={RULE} />
                <Flag x={5} y={2} up={gust} size={0.85} />
              </g>
            ))}
            <line x1={slotX - 12} y1={levelY(rowTop, 0)} x2={slotX + 8 * pitch - 6} y2={levelY(rowTop, 0)} stroke={RULE} strokeDasharray="2 3" />
            <line x1={slotX - 12} y1={levelY(rowTop, 2)} x2={slotX + 8 * pitch - 6} y2={levelY(rowTop, 2)} stroke={RULE} strokeDasharray="2 3" />
            <polyline points={points.join(" ")} fill="none" stroke={walk.color} strokeWidth={1.8} strokeLinejoin="round" />
            <circle cx={slotX - 12} cy={levelY(rowTop, 1)} r={3} fill={ACCENT} />
            {last === 3 ? (
              <g transform={`translate(${lastX} ${levelY(rowTop, 2) - 22})`}>
                <line x1={0} y1={0} x2={0} y2={20} stroke={INK2} strokeWidth={1.2} />
                <path d="M0 1h11l-3 4 3 4H0z" fill={GOAL} />
              </g>
            ) : (
              <path d={`M${lastX - 4} ${levelY(rowTop, 0) - 4}l8 8M${lastX + 4} ${levelY(rowTop, 0) - 4}l-8 8`} stroke={FELL} strokeWidth={1.6} />
            )}
            <text x={352} y={rowTop + 15} fontFamily={MONO} fontSize={10} fill={INK1}>
              {walk.outcome}
            </text>
            <text x={352} y={rowTop + 30} fontFamily={SANS} fontSize={9.5} fill={INK3}>
              {r === 0 ? "gust on turn 1 shoves it to the left cell" : r === 1 ? "braces through the gust, then walks" : "a step into the gust, straight over"}
            </text>
          </g>
        );
      })}
    </Fig>
  );
}

/* =========================================================================
 * 3. The confidence gate: switch only when the whole interval clears the habit.
 * ========================================================================= */

interface GateRow {
  name: string;
  color: string;
  mean: number;
  lo: number;
  hi: number;
}

interface GatePosition {
  title: string;
  flagUp: boolean;
  habit: number;
  rows: GateRow[];
  verdict: string;
}

/** Toy rollout values (share of walks that reached the goal) with toy intervals. */
const GATE: GatePosition[] = [
  {
    title: "middle cell, flag up",
    flagUp: true,
    habit: 0.62,
    rows: [
      { name: "step right (the habit)", color: RIGHT, mean: 0.62, lo: 0.4, hi: 0.84 },
      { name: "wait", color: WAIT, mean: 0.88, lo: 0.7, hi: 0.98 },
      { name: "step left", color: LEFT, mean: 0.0, lo: 0.0, hi: 0.1 },
    ],
    verdict: "the whole interval for wait clears the habit: switch",
  },
  {
    title: "middle cell, flag down",
    flagUp: false,
    habit: 0.9,
    rows: [
      { name: "step right (the habit)", color: RIGHT, mean: 0.9, lo: 0.75, hi: 0.99 },
      { name: "wait", color: WAIT, mean: 0.8, lo: 0.6, hi: 0.95 },
      { name: "step left", color: LEFT, mean: 0.55, lo: 0.35, hi: 0.75 },
    ],
    verdict: "no interval clears the habit: keep stepping right",
  },
];

function Check({ x, y }: { x: number; y: number }) {
  return <path d={`M${x - 6} ${y}l4 4 8-9`} stroke={GOAL} strokeWidth={2.2} fill="none" strokeLinecap="round" strokeLinejoin="round" />;
}

function Lock({ x, y }: { x: number; y: number }) {
  return (
    <g>
      <rect x={x - 6} y={y - 1} width={12} height={9} rx={2} fill={INK3} />
      <path d={`M${x - 3.5} ${y - 1}v-3a3.5 3.5 0 0 1 7 0v3`} stroke={INK3} strokeWidth={1.6} fill="none" />
    </g>
  );
}

function GatePanel({ position }: { position: GatePosition }) {
  const x0 = 190;
  const x1 = 470;
  const px = (v: number) => x0 + v * (x1 - x0);
  const rows = [96, 146, 196];
  const habitX = px(position.habit);
  return (
    <g>
      <text x={12} y={24} fontFamily={SANS} fontSize={11.5} fill={INK1}>
        {position.title}
      </text>
      <Corridor x={12} y={62} walker={1} flagUp={position.flagUp} />
      <line x1={habitX} y1={62} x2={habitX} y2={214} stroke={INK2} strokeDasharray="4 4" />
      <text x={habitX} y={54} textAnchor="middle" fontFamily={MONO} fontSize={9.5} fill={INK2}>
        the habit&apos;s own value
      </text>
      {position.rows.map((row, i) => {
        const y = rows[i];
        const clears = row.lo > position.habit;
        return (
          <g key={row.name}>
            <text x={176} y={y + 4} textAnchor="end" fontFamily={MONO} fontSize={10.5} fill={row.color}>
              {row.name}
            </text>
            <line x1={px(row.lo)} y1={y} x2={px(row.hi)} y2={y} stroke={row.color} strokeWidth={2} />
            <line x1={px(row.lo)} y1={y - 5} x2={px(row.lo)} y2={y + 5} stroke={row.color} strokeWidth={2} />
            <line x1={px(row.hi)} y1={y - 5} x2={px(row.hi)} y2={y + 5} stroke={row.color} strokeWidth={2} />
            <circle cx={px(row.mean)} cy={y} r={4.5} fill={row.color} />
            {clears ? <Check x={508} y={y} /> : <Lock x={508} y={y} />}
          </g>
        );
      })}
      {[0, 0.5, 1].map((t) => (
        <g key={t}>
          <line x1={px(t)} y1={214} x2={px(t)} y2={218} stroke={INK3} />
          <text x={px(t)} y={230} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>
            {t}
          </text>
        </g>
      ))}
      <text x={330} y={244} textAnchor="middle" fontFamily={MONO} fontSize={9} fill={INK3}>
        share of walks that reached the goal, with an interval
      </text>
      <text x={12} y={266} fontFamily={SANS} fontSize={11} fill={INK}>
        {position.verdict}
      </text>
    </g>
  );
}

export function RolloutConfidenceGate() {
  return (
    <Fig
      viewBox="0 0 560 276"
      label="Three candidates with interval whiskers against a dashed line at the habit's value; only a whisker that sits entirely above the line earns a check mark, the others a lock"
      caption="Each candidate's rollout value is drawn with an interval around it, and the dashed line is the value of the move the habit would make. Only an interval that sits entirely past the line earns a switch (the check); the rest keep the habit's move (the lock). The figure alternates between the same cell with the flag up, where waiting clears the line, and with the flag down, where nothing does."
    >
      <g data-anim="pos-a">
        <GatePanel position={GATE[0]} />
      </g>
      <g data-anim="pos-b" opacity={0}>
        <GatePanel position={GATE[1]} />
      </g>
    </Fig>
  );
}
