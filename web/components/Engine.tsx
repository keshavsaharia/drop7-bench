/**
 * Visual explainer components for the semantics-preserving fast engine
 * (approaches/lifetime-objective/fast-engine). Used from MDX via Mdx.tsx.
 *
 * Everything here is a server component: plain SVG, SMIL for frame sequencing,
 * and CSS keyframes for decorative motion. Every animated figure also renders a
 * static equivalent so the explanation never depends on motion, and CSS motion
 * stops under prefers-reduced-motion (see globals.css).
 *
 * No component computes or infers a research number. Figures that show
 * measured values receive them as props from the MDX page, which cites the
 * retained finding they come from.
 */

import { CellGlyph, parseBoard } from "./Board";

/* ---- chart tokens (dark console; validated with the dataviz palette tool) ---- */
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const GRID = "var(--color-rule)";
const SERIES = ["var(--color-series-1)", "var(--color-series-2)", "var(--color-series-3)"] as const;
const ACCENT = "var(--color-highlight)";

const FONT = "var(--font-sans)";

function fmt(n: number): string {
  return n.toLocaleString("en-US");
}

/** A horizontal bar with a 4px rounded data end and a square baseline end. */
function barPath(x: number, y: number, w: number, h: number, r = 4): string {
  const rr = Math.max(0, Math.min(r, w / 2, h / 2));
  return [
    `M${x},${y}`,
    `h${w - rr}`,
    `a${rr},${rr} 0 0 1 ${rr},${rr}`,
    `v${h - 2 * rr}`,
    `a${rr},${rr} 0 0 1 ${-rr},${rr}`,
    `h${-(w - rr)}`,
    "z",
  ].join(" ");
}

/* =========================================================================
 * Small board renderer shared by the figures (no figure/caption chrome).
 * ========================================================================= */

export function MiniBoard({
  cells,
  x = 0,
  y = 0,
  s = 40,
  highlight = [],
  dim = [],
}: {
  cells: string | readonly number[];
  x?: number;
  y?: number;
  s?: number;
  highlight?: readonly number[];
  dim?: readonly number[];
}) {
  const board = parseBoard(cells);
  return (
    <g transform={`translate(${x} ${y})`}>
      <rect width={7 * s} height={7 * s} rx={s * 0.15} fill="var(--color-cell)" />
      {board.map((cell, index) => {
        const cx = (index % 7) * s;
        const cy = Math.floor(index / 7) * s;
        return (
          <g key={index} opacity={dim.includes(index) ? 0.25 : 1}>
            <CellGlyph cell={cell} x={cx} y={cy} s={s} />
            {highlight.includes(index) && (
              <rect
                x={cx + 2}
                y={cy + 2}
                width={s - 4}
                height={s - 4}
                rx={s * 0.12}
                fill="none"
                stroke={ACCENT}
                strokeWidth={3}
              />
            )}
          </g>
        );
      })}
    </g>
  );
}

function Bits({
  bits,
  x,
  y,
  cell = 16,
  on = SERIES[0],
  labels,
}: {
  bits: readonly (0 | 1)[];
  x: number;
  y: number;
  cell?: number;
  on?: string;
  labels?: boolean;
}) {
  return (
    <g transform={`translate(${x} ${y})`}>
      {bits.map((bit, i) => (
        <g key={i}>
          <rect
            x={i * (cell + 2)}
            y={0}
            width={cell}
            height={cell}
            rx={3}
            fill={bit ? on : "var(--color-hover)"}
            stroke={GRID}
          />
          {labels && (
            <text
              x={i * (cell + 2) + cell / 2}
              y={cell / 2 + 0.5}
              textAnchor="middle"
              dominantBaseline="central"
              fontSize={cell * 0.6}
              fontFamily={FONT}
              fontWeight={700}
              fill={bit ? "var(--color-ink)" : INK_3}
            >
              {bit}
            </text>
          )}
        </g>
      ))}
    </g>
  );
}

/* =========================================================================
 * 1. Board scan: one pass → row masks, column masks, numbered bitboard.
 * ========================================================================= */

export function BitboardScan({
  cells,
  caption,
}: {
  cells: string;
  caption?: string;
}) {
  const board = parseBoard(cells);
  const rowMask = (r: number) =>
    Array.from({ length: 7 }, (_, c) => (board[r * 7 + c] !== 0 ? 1 : 0) as 0 | 1);
  const colMask = (c: number) =>
    Array.from({ length: 7 }, (_, r) => (board[r * 7 + c] !== 0 ? 1 : 0) as 0 | 1);
  const numbered = board.map((v) => (v >= 1 && v <= 7 ? 1 : 0) as 0 | 1);
  const s = 34;
  const W = 640;
  const H = 7 * s + 150;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Board scan into bit masks">
        <text x={0} y={14} fontSize={12} fontFamily={FONT} fill={INK_2}>
          board (49 bytes)
        </text>
        <MiniBoard cells={cells} x={0} y={24} s={s} />

        {/* row masks */}
        <text x={7 * s + 36} y={14} fontSize={12} fontFamily={FONT} fill={INK_2}>
          row occupancy mask (7 bits each)
        </text>
        {Array.from({ length: 7 }, (_, r) => (
          <g key={r}>
            <path
              d={`M${7 * s + 4},${24 + r * s + s / 2} h24`}
              stroke={GRID}
              strokeWidth={1}
            />
            <Bits bits={rowMask(r)} x={7 * s + 36} y={24 + r * s + s / 2 - 8} labels />
            <text
              x={7 * s + 36 + 7 * 18 + 8}
              y={24 + r * s + s / 2 + 1}
              fontSize={11}
              fontFamily={FONT}
              fill={INK_3}
              dominantBaseline="central"
            >
              = {parseInt(rowMask(r).join(""), 2)}
            </text>
          </g>
        ))}

        {/* column masks */}
        <text x={0} y={24 + 7 * s + 22} fontSize={12} fontFamily={FONT} fill={INK_2}>
          column occupancy masks (top → bottom bit)
        </text>
        {Array.from({ length: 7 }, (_, c) => (
          <g key={c} transform={`translate(${c * s} ${24 + 7 * s + 30})`}>
            {colMask(c).map((bit, r) => (
              <rect
                key={r}
                x={s / 2 - 6}
                y={r * 9}
                width={12}
                height={7}
                rx={2}
                fill={bit ? SERIES[0] : "var(--color-hover)"}
                stroke={GRID}
              />
            ))}
          </g>
        ))}

        {/* numbered bitboard */}
        <text x={7 * s + 36} y={24 + 7 * s + 22} fontSize={12} fontFamily={FONT} fill={INK_2}>
          numbered bitboard (49 bits in one 64-bit word)
        </text>
        <g transform={`translate(${7 * s + 36} ${24 + 7 * s + 30})`}>
          {numbered.map((bit, i) => (
            <rect
              key={i}
              x={(i % 7) * 11}
              y={Math.floor(i / 7) * 9}
              width={9}
              height={7}
              rx={2}
              fill={bit ? SERIES[2] : "var(--color-hover)"}
              stroke={GRID}
            />
          ))}
          <text x={90} y={36} fontSize={11} fontFamily={FONT} fill={INK_3}>
            iterate set bits with ctz; skip every empty or gray cell for free
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 2. Run-length table: a 7-bit row pattern → run length of every position.
 * ========================================================================= */

function runLengths(mask: readonly (0 | 1)[]): number[] {
  const out = new Array<number>(7).fill(0);
  let c = 0;
  while (c < 7) {
    if (!mask[c]) {
      c += 1;
      continue;
    }
    const start = c;
    while (c < 7 && mask[c]) c += 1;
    for (let p = start; p < c; p += 1) out[p] = c - start;
  }
  return out;
}

export function RunLengthLookup({
  row,
  caption,
}: {
  /** Seven characters in serializeBoard encoding, one row. */
  row: string;
  caption?: string;
}) {
  const cells = parseBoard(row);
  const mask = cells.map((v) => (v !== 0 ? 1 : 0) as 0 | 1);
  const lengths = runLengths(mask);
  const index = parseInt(mask.join(""), 2);
  const poppers = cells
    .map((v, i) => (v >= 1 && v <= 7 && lengths[i] === v ? i : -1))
    .filter((i) => i >= 0);
  const s = 40;
  const W = 640;
  const H = 230;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Run-length table lookup">
        <text x={0} y={14} fontSize={12} fontFamily={FONT} fill={INK_2}>
          one row of the board
        </text>
        <g transform="translate(0 24)">
          <rect width={7 * s} height={s} rx={6} fill="var(--color-cell)" />
          {cells.map((cell, i) => (
            <g key={i}>
              <CellGlyph cell={cell} x={i * s} y={0} s={s} />
              {poppers.includes(i) && (
                <rect
                  x={i * s + 2}
                  y={2}
                  width={s - 4}
                  height={s - 4}
                  rx={6}
                  fill="none"
                  stroke={ACCENT}
                  strokeWidth={3}
                />
              )}
            </g>
          ))}
        </g>

        <text x={0} y={94} fontSize={12} fontFamily={FONT} fill={INK_2}>
          occupancy mask → table index {index} of 128
        </text>
        <Bits bits={mask} x={0} y={102} cell={22} labels />

        <text x={7 * s + 30} y={94} fontSize={12} fontFamily={FONT} fill={INK_2}>
          kRunLengthTable[{index}].length[0..6]
        </text>
        <g transform={`translate(${7 * s + 30} 102)`}>
          {lengths.map((len, i) => (
            <g key={i}>
              <rect
                x={i * 24}
                y={0}
                width={22}
                height={22}
                rx={3}
                fill={poppers.includes(i) ? ACCENT : "var(--color-hover)"}
                stroke={GRID}
              />
              <text
                x={i * 24 + 11}
                y={11.5}
                textAnchor="middle"
                dominantBaseline="central"
                fontSize={13}
                fontFamily={FONT}
                fontWeight={700}
                fill={poppers.includes(i) ? "var(--color-bg)" : INK}
              >
                {len}
              </text>
            </g>
          ))}
        </g>

        <text x={0} y={160} fontSize={12} fontFamily={FONT} fill={INK_2}>
          popper test per numbered cell: length[column] == disc value
        </text>
        {cells.map((cell, i) => {
          const numberedCell = cell >= 1 && cell <= 7;
          const pops = poppers.includes(i);
          return (
            <g key={i} transform={`translate(${i * 88} 170)`}>
              <rect
                width={82}
                height={36}
                rx={6}
                fill={pops ? "rgba(250,204,21,0.12)" : "var(--color-raised)"}
                stroke={pops ? ACCENT : GRID}
              />
              <text
                x={8}
                y={14}
                fontSize={10.5}
                fontFamily={FONT}
                fill={numberedCell ? INK : INK_3}
              >
                {numberedCell ? `disc ${cell}, run ${lengths[i]}` : cell === 0 ? "empty" : "gray"}
              </text>
              <text
                x={8}
                y={28}
                fontSize={10.5}
                fontFamily={FONT}
                fontWeight={700}
                fill={pops ? ACCENT : INK_3}
              >
                {numberedCell ? (pops ? "POPS" : "stays") : "skipped"}
              </text>
            </g>
          );
        })}
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 3. Cascade frames — SMIL-sequenced, with a static filmstrip underneath.
 * ========================================================================= */

export interface CascadeFrame {
  cells: string;
  label: string;
  /** Cells (0–48) outlined in this frame: the discs that pop, the covers hit. */
  highlight?: number[];
  note?: string;
}

export function CascadeAnimation({
  frames,
  secondsPerFrame = 1.6,
  caption,
}: {
  frames: CascadeFrame[];
  secondsPerFrame?: number;
  caption?: string;
}) {
  const n = frames.length;
  const dur = n * secondsPerFrame;
  const s = 36;
  const W = 7 * s + 400;
  const H = 7 * s + 10;
  const thumb = 14;
  return (
    <figure className="engine-fig">
      <div className="engine-motion">
        <svg
          viewBox={`0 0 ${W} ${H}`}
          role="img"
          aria-label="Animated cascade: each frame is one engine step"
          style={{ maxWidth: 660 }}
        >
          {frames.map((frame, i) => {
            const keyTimes =
              i === 0
                ? `0;${(1 / n).toFixed(4)}`
                : i === n - 1
                  ? `0;${(i / n).toFixed(4)}`
                  : `0;${(i / n).toFixed(4)};${((i + 1) / n).toFixed(4)}`;
            const values = i === 0 ? "1;0" : i === n - 1 ? "0;1" : "0;1;0";
            return (
              <g key={i} opacity={i === 0 ? 1 : 0}>
                <animate
                  attributeName="opacity"
                  calcMode="discrete"
                  values={values}
                  keyTimes={keyTimes}
                  dur={`${dur}s`}
                  repeatCount="indefinite"
                />
                <MiniBoard cells={frame.cells} s={s} highlight={frame.highlight ?? []} />
                <text
                  x={7 * s + 20}
                  y={24}
                  fontSize={12}
                  fontFamily={FONT}
                  fill={INK_3}
                >
                  step {i + 1} of {n}
                </text>
                <text
                  x={7 * s + 20}
                  y={48}
                  fontSize={15}
                  fontFamily={FONT}
                  fontWeight={700}
                  fill={INK}
                >
                  {frame.label}
                </text>
                {frame.note &&
                  frame.note.split("\n").map((line, li) => (
                    <text
                      key={li}
                      x={7 * s + 20}
                      y={72 + li * 17}
                      fontSize={11.5}
                      fontFamily={FONT}
                      fill={INK_2}
                    >
                      {line}
                    </text>
                  ))}
                {/* progress ticks */}
                {frames.map((_, t) => (
                  <rect
                    key={t}
                    x={7 * s + 20 + t * 14}
                    y={H - 14}
                    width={10}
                    height={4}
                    rx={2}
                    fill={t === i ? SERIES[0] : GRID}
                  />
                ))}
              </g>
            );
          })}
        </svg>
      </div>
      {/* static filmstrip: the same frames, always visible */}
      <div style={{ display: "flex", flexWrap: "wrap", gap: 10, marginTop: 10 }}>
        {frames.map((frame, i) => (
          <div key={i} style={{ width: 7 * thumb + 2 }}>
            <svg viewBox={`0 0 ${7 * thumb} ${7 * thumb}`} width={7 * thumb} height={7 * thumb}>
              <MiniBoard cells={frame.cells} s={thumb} highlight={frame.highlight ?? []} />
            </svg>
            <div style={{ fontSize: 10, color: INK_3, lineHeight: 1.3, marginTop: 2 }}>
              {i + 1}. {frame.label}
            </div>
          </div>
        ))}
      </div>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 4. The move pipeline — which steps exist and which ones got cheaper.
 * ========================================================================= */

export interface PipelineStep {
  title: string;
  before: string;
  after: string;
}

export function MovePipeline({
  steps,
  caption,
}: {
  steps: PipelineStep[];
  caption?: string;
}) {
  const perRow = 4;
  const boxW = 150;
  const boxH = 104;
  const gap = 26;
  const rowGap = 30;
  const rows = Math.ceil(steps.length / perRow);
  const W = perRow * (boxW + gap) - gap;
  const H = rows * (boxH + rowGap) - rowGap + 20;
  const centre = (i: number) => ({
    x: (i % perRow) * (boxW + gap) + boxW / 2,
    y: Math.floor(i / perRow) * (boxH + rowGap) + 16,
  });
  const centres = steps.map((_, i) => centre(i));
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Move application pipeline">
        {steps.map((step, i) => {
          const col = i % perRow;
          const row = Math.floor(i / perRow);
          const x = col * (boxW + gap);
          const y = 16 + row * (boxH + rowGap);
          const last = i === steps.length - 1;
          const endOfRow = col === perRow - 1;
          return (
            <g key={i} transform={`translate(${x} ${y})`}>
              <rect width={boxW} height={boxH} rx={10} fill="var(--color-raised)" stroke={GRID} />
              <text x={12} y={22} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                {i + 1}. {step.title}
              </text>
              <text x={12} y={44} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                reference
              </text>
              <text x={12} y={58} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
                {step.before}
              </text>
              <text x={12} y={78} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                fast engine
              </text>
              <text x={12} y={92} fontSize={10.5} fontFamily={FONT} fill={INK}>
                {step.after}
              </text>
              {!last && !endOfRow && (
                <path
                  d={`M${boxW + 4},${boxH / 2} h${gap - 10} m-6,-5 l6,5 l-6,5`}
                  stroke={INK_3}
                  strokeWidth={1.5}
                  fill="none"
                />
              )}
              {!last && endOfRow && (
                <path
                  d={`M${boxW / 2},${boxH + 4} v${rowGap - 10} m-5,-6 l5,6 l5,-6`}
                  stroke={INK_3}
                  strokeWidth={1.5}
                  fill="none"
                />
              )}
            </g>
          );
        })}
        {/* a disc travelling through the pipeline */}
        <g className="engine-motion">
          <circle r={6} fill={SERIES[0]}>
            <animate
              attributeName="cx"
              values={centres.map((c) => c.x).join(";")}
              keyTimes={centres.map((_, i) => (i / (centres.length - 1)).toFixed(3)).join(";")}
              calcMode="discrete"
              dur={`${steps.length * 0.9}s`}
              repeatCount="indefinite"
            />
            <animate
              attributeName="cy"
              values={centres.map((c) => c.y - 8).join(";")}
              keyTimes={centres.map((_, i) => (i / (centres.length - 1)).toFixed(3)).join(";")}
              calcMode="discrete"
              dur={`${steps.length * 0.9}s`}
              repeatCount="indefinite"
            />
          </circle>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 5. Search-tree shape: nearly every node is a leaf.
 * ========================================================================= */

export function TreeShape({
  depth,
  totalNodes,
  leafCalls,
  leafShare,
  interiorKeys,
  caption,
}: {
  depth: number;
  totalNodes: string;
  leafCalls: string;
  /** e.g. "96.1 %" — passed through verbatim from the finding. */
  leafShare: string;
  interiorKeys: string;
  caption?: string;
}) {
  const W = 620;
  const levelH = 34;
  const H = (depth + 1) * levelH + 24;
  const widths = Array.from({ length: depth + 1 }, (_, d) => 40 + (d / depth) ** 2 * 360);
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Shape of a depth-limited search tree">
        {widths.map((w, d) => {
          const isLeaf = d === depth;
          return (
            <g key={d} transform={`translate(0 ${8 + d * levelH})`}>
              <rect
                x={220 - w / 2}
                y={0}
                width={w}
                height={levelH - 8}
                rx={6}
                fill={isLeaf ? SERIES[0] : "var(--color-hover)"}
                stroke={isLeaf ? SERIES[0] : GRID}
              />
              <text
                x={440}
                y={(levelH - 8) / 2}
                dominantBaseline="central"
                fontSize={12}
                fontFamily={FONT}
                fill={isLeaf ? INK : INK_2}
                fontWeight={isLeaf ? 700 : 400}
              >
                {d === 0
                  ? "root: one real position"
                  : isLeaf
                    ? `ply ${d}: leaf evaluations — ${leafShare} of nodes`
                    : `ply ${d}: interior nodes, transposition key built`}
              </text>
            </g>
          );
        })}
        <text x={0} y={H - 6} fontSize={11} fontFamily={FONT} fill={INK_3}>
          one depth-{depth} decision: {totalNodes} nodes · {leafCalls} leaf calls · {interiorKeys} interior keys
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 6. Attribution: part-to-whole stacked bar (3 segments, validated slots).
 * ========================================================================= */

export interface Segment {
  label: string;
  /** Share in percent, as recorded. */
  share: number;
  detail: string;
}

export function AttributionBar({
  title,
  segments,
  caption,
}: {
  title: string;
  segments: Segment[];
  caption?: string;
}) {
  const W = 640;
  const barH = 22;
  const total = segments.reduce((a, s) => a + s.share, 0);
  const placed = segments.map((seg, i) => {
    const offset = segments
      .slice(0, i)
      .reduce((acc, prior) => acc + (prior.share / total) * W, 0);
    return {
      ...seg,
      x: offset,
      w: (seg.share / total) * W,
      color: SERIES[i % SERIES.length],
    };
  });
  return (
    <figure className="engine-fig">
      <div style={{ fontSize: 13, color: INK, fontWeight: 600 }}>{title}</div>
      <svg viewBox={`0 0 ${W} ${barH + 8}`} role="img" aria-label={title} style={{ marginTop: 8 }}>
        {placed.map((seg, i) => {
          const gap = i === 0 ? 0 : 2;
          const w = Math.max(0, seg.w - gap);
          const labelFits = w > 64;
          return (
            <g key={seg.label}>
              <title>{`${seg.label}: ${seg.share}% — ${seg.detail}`}</title>
              <rect x={seg.x + gap} y={4} width={w} height={barH} fill={seg.color} />
              {labelFits && (
                <text
                  x={seg.x + gap + 8}
                  y={4 + barH / 2}
                  dominantBaseline="central"
                  fontSize={12}
                  fontFamily={FONT}
                  fontWeight={700}
                  fill="var(--color-ink)"
                >
                  {seg.share}%
                </text>
              )}
            </g>
          );
        })}
      </svg>
      <div className="engine-legend">
        {placed.map((seg) => (
          <span key={seg.label} style={{ ["--swatch" as string]: seg.color }}>
            {seg.label} · {seg.share}%
          </span>
        ))}
      </div>
      <details className="engine-table">
        <summary>table view</summary>
        <table>
          <thead>
            <tr>
              <th>component</th>
              <th>share</th>
              <th>detail</th>
            </tr>
          </thead>
          <tbody>
            {segments.map((seg) => (
              <tr key={seg.label}>
                <td>{seg.label}</td>
                <td>{seg.share}%</td>
                <td style={{ textAlign: "left" }}>{seg.detail}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </details>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 7. Speedup bars — single series, value at the tip, 1× reference line.
 * ========================================================================= */

export interface SpeedupRow {
  label: string;
  value: number;
  note?: string;
}

export function SpeedupBars({
  title,
  rows,
  max = 4,
  caption,
}: {
  title: string;
  rows: SpeedupRow[];
  max?: number;
  caption?: string;
}) {
  const labelW = 250;
  const plotW = 340;
  const rowH = 26;
  const barH = 14;
  const W = labelW + plotW + 50;
  const H = rows.length * rowH + 34;
  const xOf = (v: number) => labelW + (v / max) * plotW;
  const ticks = Array.from({ length: max + 1 }, (_, i) => i);
  return (
    <figure className="engine-fig">
      <div style={{ fontSize: 13, color: INK, fontWeight: 600 }}>{title}</div>
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label={title} style={{ marginTop: 6 }}>
        {ticks.map((t) => (
          <g key={t}>
            <path d={`M${xOf(t)},4 V${H - 22}`} stroke={t === 1 ? INK_3 : GRID} strokeWidth={1} />
            <text
              x={xOf(t)}
              y={H - 8}
              textAnchor="middle"
              fontSize={10.5}
              fontFamily={FONT}
              fill={INK_3}
            >
              {t}×
            </text>
          </g>
        ))}
        <text x={xOf(1) + 4} y={12} fontSize={10} fontFamily={FONT} fill={INK_3}>
          1× = frozen reference
        </text>
        {rows.map((row, i) => {
          const y = 18 + i * rowH;
          const w = xOf(row.value) - labelW;
          return (
            <g key={row.label}>
              <title>{`${row.label}: ${row.value}×${row.note ? ` (${row.note})` : ""}`}</title>
              <text
                x={labelW - 10}
                y={y + barH / 2}
                textAnchor="end"
                dominantBaseline="central"
                fontSize={11.5}
                fontFamily={FONT}
                fill={INK_2}
              >
                {row.label}
              </text>
              <path d={barPath(labelW, y, w, barH)} fill={SERIES[0]} />
              <text
                x={labelW + w + 6}
                y={y + barH / 2}
                dominantBaseline="central"
                fontSize={11.5}
                fontFamily={FONT}
                fontWeight={700}
                fill={INK}
              >
                {row.value.toFixed(2)}×
              </text>
            </g>
          );
        })}
      </svg>
      <details className="engine-table">
        <summary>table view</summary>
        <table>
          <thead>
            <tr>
              <th>configuration</th>
              <th>speedup</th>
              <th>note</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.label}>
                <td>{row.label}</td>
                <td>{row.value.toFixed(2)}×</td>
                <td style={{ textAlign: "left" }}>{row.note ?? ""}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </details>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 8. Packed transposition key: 49 cells × 4 bits + next disc + moves + depth.
 * ========================================================================= */

export function PackedKey({ caption }: { caption?: string }) {
  const W = 640;
  const bytes = 32;
  const bw = W / bytes;
  const fields = [
    { from: 0, to: 24.5, label: "49 cells × 4 bits = 196 bits", color: SERIES[0] },
    { from: 24.5, to: 25.5, label: "next disc", color: SERIES[1] },
    { from: 25.5, to: 26.5, label: "moves left", color: SERIES[2] },
    { from: 26.5, to: 27.5, label: "depth", color: "var(--color-series-7)" },
    { from: 27.5, to: 32, label: "unused / zeroed", color: "var(--color-hover)" },
  ];
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} 62`} role="img" aria-label="Packed 32-byte transposition key">
        {fields.map((f) => (
          <g key={f.label}>
            <rect
              x={f.from * bw + 1}
              y={10}
              width={(f.to - f.from) * bw - 2}
              height={26}
              rx={4}
              fill={f.color}
              stroke={GRID}
            />
          </g>
        ))}
        {Array.from({ length: bytes + 1 }, (_, i) => (
          <path key={i} d={`M${i * bw},38 v5`} stroke={GRID} strokeWidth={1} />
        ))}
        <text x={0} y={56} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          byte 0
        </text>
        <text x={W} y={56} textAnchor="end" fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          byte 31
        </text>
      </svg>
      <div className="engine-legend">
        {fields.map((f) => (
          <span key={f.label} style={{ ["--swatch" as string]: f.color }}>
            {f.label}
          </span>
        ))}
      </div>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 9. Differential gates as a checklist.
 * ========================================================================= */

export interface GateCard {
  name: string;
  compares: string;
  scope: string;
  result: string;
}

export function GateLadder({ gates, caption }: { gates: GateCard[]; caption?: string }) {
  return (
    <figure className="engine-fig">
      <div
        style={{
          display: "grid",
          gridTemplateColumns: "repeat(auto-fit, minmax(220px, 1fr))",
          gap: 10,
        }}
      >
        {gates.map((gate, i) => (
          <div
            key={gate.name}
            style={{
              border: `1px solid ${GRID}`,
              borderRadius: 10,
              padding: "10px 12px",
              background: "var(--color-raised)",
            }}
          >
            <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
              <svg width={22} height={22} viewBox="0 0 22 22" aria-hidden="true">
                <circle cx={11} cy={11} r={10} fill="rgba(25,158,112,0.18)" stroke={SERIES[2]} />
                <path
                  d="M6.5 11.5l3 3 6-6"
                  stroke={SERIES[2]}
                  strokeWidth={2}
                  fill="none"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                />
              </svg>
              <div style={{ fontSize: 13, fontWeight: 700, color: INK }}>
                gate {i + 1} · {gate.name}
              </div>
            </div>
            <div style={{ fontSize: 11.5, color: INK_2, marginTop: 6 }}>{gate.compares}</div>
            <div style={{ fontSize: 11.5, color: INK_3, marginTop: 4 }}>{gate.scope}</div>
            <div style={{ fontSize: 12, color: INK, fontWeight: 600, marginTop: 6 }}>
              {gate.result}
            </div>
          </div>
        ))}
      </div>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 10. Future work: array-of-structs vs structure-of-arrays vs bit-planes.
 * ========================================================================= */

export function BatchLayout({ caption }: { caption?: string }) {
  const W = 640;
  const H = 216;
  const boards = 6;
  const cell = 5;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Batch memory layouts">
        {/* AoS */}
        <text x={0} y={14} fontSize={12} fontFamily={FONT} fontWeight={700} fill={INK}>
          today: one board at a time
        </text>
        <text x={0} y={30} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          49 bytes per board; one thread walks one cascade
        </text>
        {Array.from({ length: boards }, (_, b) => (
          <g key={b} transform={`translate(${b * 44} 40)`}>
            {Array.from({ length: 49 }, (_, i) => (
              <rect
                key={i}
                x={(i % 7) * cell}
                y={Math.floor(i / 7) * cell}
                width={cell - 1}
                height={cell - 1}
                fill={b === 0 ? SERIES[0] : "var(--color-rule)"}
              />
            ))}
          </g>
        ))}

        {/* SoA / bit-planes */}
        <text x={320} y={14} fontSize={12} fontFamily={FONT} fontWeight={700} fill={INK}>
          batched: thousands of boards in lockstep
        </text>
        <text x={320} y={30} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          one array per cell (or per bit-plane); lane b owns board b
        </text>
        {Array.from({ length: 10 }, (_, plane) => (
          <g key={plane} transform={`translate(320 ${40 + plane * 8})`}>
            <text x={-4} y={6} textAnchor="end" fontSize={7} fontFamily={FONT} fill={INK_3}>
              {plane === 0 ? "cell 0" : plane === 9 ? "cell 48" : plane === 4 ? "…" : ""}
            </text>
            {Array.from({ length: 40 }, (_, lane) => (
              <rect
                key={lane}
                x={lane * 7}
                y={0}
                width={6}
                height={6}
                fill={lane === 0 ? SERIES[0] : "var(--color-rule)"}
              />
            ))}
          </g>
        ))}
        <path d="M320,126 h280" stroke={GRID} />
        <text x={320} y={140} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
          a &ldquo;wave&rdquo; is then one vectorised step over every lane:
        </text>
        {["scan masks", "find poppers", "hit covers", "clear + reveal", "gravity"].map(
          (label, i) => (
            <g key={label} transform={`translate(${320 + i * 58} 150)`}>
              <rect width={54} height={22} rx={5} fill="var(--color-raised)" stroke={GRID} />
              <text
                x={27}
                y={11.5}
                textAnchor="middle"
                dominantBaseline="central"
                fontSize={8.5}
                fontFamily={FONT}
                fill={INK_2}
              >
                {label}
              </text>
            </g>
          ),
        )}
        <text x={320} y={190} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          lanes whose cascade has ended idle until the whole
        </text>
        <text x={320} y={204} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          batch settles — this divergence is the cost to measure
        </text>
        <text x={0} y={120} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          irregular: each board&apos;s cascade has its own
        </text>
        <text x={0} y={134} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          number of waves, reveals and moved columns
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 11. A labelled "where the next speedup could come from" ladder.
 * ========================================================================= */

export interface Lever {
  name: string;
  status: string;
  evidence: string;
  what: string;
}

export function LeverList({ levers }: { levers: Lever[] }) {
  return (
    <div className="engine-fig" style={{ padding: 0, overflow: "hidden" }}>
      {levers.map((lever, i) => (
        <div
          key={lever.name}
          style={{
            display: "grid",
            gridTemplateColumns: "32px 1fr",
            gap: 12,
            padding: "12px 14px",
            borderTop: i === 0 ? "none" : `1px solid ${GRID}`,
          }}
        >
          <div
            style={{
              width: 28,
              height: 28,
              borderRadius: 8,
              background: "var(--color-raised)",
              border: `1px solid ${GRID}`,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              fontSize: 12,
              fontWeight: 700,
              color: INK_2,
            }}
          >
            {i + 1}
          </div>
          <div>
            <div style={{ display: "flex", flexWrap: "wrap", gap: 8, alignItems: "baseline" }}>
              <span style={{ fontSize: 13.5, fontWeight: 700, color: INK }}>{lever.name}</span>
              <span
                style={{
                  fontSize: 10.5,
                  padding: "1px 6px",
                  borderRadius: 4,
                  background: "var(--color-rule)",
                  color: INK_2,
                }}
              >
                {lever.status}
              </span>
            </div>
            <div style={{ fontSize: 12.5, color: INK_2, marginTop: 4, lineHeight: 1.55 }}>
              {lever.what}
            </div>
            <div style={{ fontSize: 11.5, color: INK_3, marginTop: 4 }}>{lever.evidence}</div>
          </div>
        </div>
      ))}
    </div>
  );
}

/* A tiny helper exposed to MDX for inline numbers with thousands separators. */
export function Num({ v }: { v: number }) {
  return <span style={{ fontVariantNumeric: "tabular-nums" }}>{fmt(v)}</span>;
}

/* =========================================================================
 * 11. Nibble-packed column + PEXT gravity (rust-engine).
 * ========================================================================= */

/**
 * One packed column word and gravity as a bit gather.  Seven nibbles hold the
 * seven cells of a column; the least-significant nibble is the bottom row, so
 * compacting the non-zero nibbles toward the bottom IS gravity.  Drawn
 * left-to-right as the word is read: the top row's nibble on the left, the
 * bottom row's nibble on the right, discs falling rightward.
 */
export function PackedColumn({
  before,
  after,
  caption,
}: {
  /** Seven nibble values, top row first; 0 = empty. */
  before: number[];
  /** Seven nibble values after gravity, top row first. */
  after: number[];
  caption?: string;
}) {
  const cellW = 46;
  const gap = 4;
  const rowH = 34;
  const W = 8 * (cellW + gap) + 8;
  const H = 2 * rowH + 74;
  const glyph = (v: number) => (v === 0 ? "" : v === 8 ? "●" : v === 9 ? "◐" : String(v));

  const row = (
    values: number[],
    y: number,
    label: string,
  ) => (
    <g>
      <text x={0} y={y - 8} fontSize={11} fontFamily={FONT} fill={INK_3}>
        {label}
      </text>
      {/* the unused top nibble, bits 28-31 */}
      <rect
        x={4}
        y={y}
        width={cellW}
        height={rowH}
        rx={5}
        fill="var(--color-hover)"
        stroke={GRID}
        strokeDasharray="3 3"
      />
      {values.map((v, i) => {
        // values[0] is the top row = nibble 6; drawn just right of the unused nibble.
        const x = 4 + (i + 1) * (cellW + gap);
        return (
          <g key={i}>
            <rect
              x={x}
              y={y}
              width={cellW}
              height={rowH}
              rx={5}
              fill={v === 0 ? "var(--color-raised)" : "var(--color-accent-soft)"}
              stroke={v === 0 ? GRID : SERIES[0]}
              strokeWidth={v === 0 ? 1 : 1.5}
            />
            <text
              x={x + cellW / 2}
              y={y + rowH / 2}
              textAnchor="middle"
              dominantBaseline="central"
              fontSize={14}
              fontFamily={FONT}
              fontWeight={700}
              fill={v === 0 ? INK_3 : INK}
            >
              {glyph(v)}
            </text>
            <text
              x={x + cellW / 2}
              y={y + rowH + 12}
              textAnchor="middle"
              fontSize={9}
              fontFamily={FONT}
              fill={INK_3}
            >
              r{6 - i}
            </text>
          </g>
        );
      })}
    </g>
  );

  const arrowY = rowH + 34;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Gravity as a PEXT bit gather">
        {row(before, 20, "one u32 column word (4 bits per cell)")}
        <text
          x={W / 2}
          y={arrowY + 8}
          textAnchor="middle"
          fontSize={11.5}
          fontFamily={FONT}
          fill={ACCENT}
        >
          {"↓  gravity = pext(word, expanded-nonzero-mask)  — discs gather toward the bottom row"}
        </text>
        {row(after, arrowY + 22, "after: bottom-packed, order preserved")}
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

