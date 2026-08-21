/**
 * Figures for the /learn/concepts pages: search trees, chance handling and the
 * sibling-ranking problem, demonstrated on a real position.
 *
 * The position and every number come from web/content/learn/concept-scenarios.json,
 * which web/scripts/generate-concept-scenarios.ts computes with the repository's
 * TypeScript engine. The "evaluator" in these figures is deliberately the
 * simplest possible one — points scored by the move — so a reader can check
 * every number by hand. It is a teaching device, not a research policy.
 *
 * Server components only: SVG + SMIL, with static equivalents for every
 * animation; CSS motion is disabled under prefers-reduced-motion (globals.css).
 */

import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { CellGlyph } from "./Board";
import { MiniBoard } from "./Engine";

const INK = "#fafafa";
const INK_2 = "#a1a1aa";
const INK_3 = "#71717a";
const GRID = "#27272a";
const BLUE = "#3987e5";
const ORANGE = "#d95926";
const AQUA = "#199e70";
const ACCENT = "#facc15";
const FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif";

interface Reply { column: number; points: number; board: string; waves: number }
interface Branch { disc: number; replies: Reply[]; best: Reply | null }
interface ColumnNode {
  column: number;
  legal: boolean;
  points?: number;
  board?: string;
  waves?: number;
  gameOver?: boolean;
  branches?: Branch[];
  fair?: number;
  optimistic?: number;
  pessimistic?: number;
}
interface TreeScenario {
  board: string;
  nextDisc: number;
  columns: ColumnNode[];
  choice: { greedy: number; fair: number; optimistic: number; pessimistic: number };
}

function loadTree(): TreeScenario | null {
  const path = join(process.cwd(), "content", "learn", "concept-scenarios.json");
  if (!existsSync(path)) return null;
  const data = JSON.parse(readFileSync(path, "utf8")) as { tree?: TreeScenario };
  return data.tree ?? null;
}

function Missing({ what }: { what: string }) {
  return (
    <div className="engine-fig" style={{ color: INK_3, fontSize: 13 }}>
      {what} is not available in this checkout. Run <code>npm run concept:scenarios</code>{" "}
      inside <code>web/</code> to regenerate <code>content/learn/concept-scenarios.json</code>.
    </div>
  );
}

function fmt(n: number): string {
  return Number.isInteger(n) ? n.toLocaleString("en-US") : n.toFixed(1);
}

/* =========================================================================
 * 1. The root position, the seven choices, and what each scores right now.
 * ========================================================================= */

export function RootAndChoices({ caption }: { caption?: string }) {
  const tree = loadTree();
  if (!tree) return <Missing what="The look-ahead position" />;
  const s = 26;
  const t = 13;
  const W = 760;
  const H = 7 * s + 7 * t + 110;
  const legal = tree.columns.filter((c) => c.legal);
  const bestNow = Math.max(...legal.map((c) => c.points ?? 0));
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -${s + 4} ${W} ${H}`} role="img" aria-label="A position and its seven possible moves">
        <text x={0} y={-s + 10} fontSize={11} fontFamily={FONT} fill={INK_3}>
          the position · next disc
        </text>
        <g transform={`translate(150 ${-s - 2})`}>
          <CellGlyph cell={tree.nextDisc} x={0} y={0} s={s} />
        </g>
        <MiniBoard cells={tree.board} s={s} />
        <g transform={`translate(${7 * s + 30} 0)`}>
          <text y={12} fontSize={13} fontFamily={FONT} fontWeight={700} fill={INK}>
            Seven columns, seven futures
          </text>
          {[
            "The player knows the next disc. What they do not know is the",
            "disc after that, nor what is hidden under any gray disc.",
            "Each column below shows the board after the drop, and the",
            "points the move scores by itself. Points are the simplest",
            "possible way to judge a move — and, as the next figures",
            "show, judging by points alone is a trap.",
          ].map((line, i) => (
            <text key={i} y={34 + i * 16} fontSize={11.5} fontFamily={FONT} fill={INK_2}>
              {line}
            </text>
          ))}
        </g>
        <g transform={`translate(0 ${7 * s + 26})`}>
          {tree.columns.map((c, i) => (
            <g key={c.column} transform={`translate(${i * 108} 0)`}>
              <text x={0} y={-8} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                column {c.column}
              </text>
              {c.legal && c.board ? (
                <>
                  <MiniBoard cells={c.board} s={t} />
                  <text
                    x={0}
                    y={7 * t + 16}
                    fontSize={12}
                    fontFamily={FONT}
                    fontWeight={700}
                    fill={(c.points ?? 0) === bestNow ? ACCENT : INK}
                  >
                    +{fmt(c.points ?? 0)} now
                  </text>
                  <text x={0} y={7 * t + 30} fontSize={10} fontFamily={FONT} fill={INK_3}>
                    {c.waves === 0 ? "no clear" : `${c.waves} wave${c.waves === 1 ? "" : "s"}`}
                  </text>
                </>
              ) : (
                <text y={7 * t} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                  column full
                </text>
              )}
            </g>
          ))}
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 2. Expand one column into its chance node: seven possible next discs, the
 *    best reply to each, and the average forming.
 * ========================================================================= */

export function ChanceNode({
  column,
  caption,
  secondsPerBranch = 0.7,
}: {
  column: number;
  caption?: string;
  secondsPerBranch?: number;
}) {
  const tree = loadTree();
  if (!tree) return <Missing what="The look-ahead position" />;
  const node = tree.columns.find((c) => c.column === column);
  if (!node || !node.legal || !node.branches || !node.board) {
    return <Missing what={`Column ${column}`} />;
  }
  const branches = node.branches;
  const t = 11;
  const W = 760;
  const H = 7 * t + 232;
  const values = branches.map((b) => b.best?.points ?? 0);
  const avg = values.reduce((a, b) => a + b, 0) / values.length;
  const max = Math.max(...values, 1);
  const barW = 96;
  const total = branches.length * secondsPerBranch + 2.4;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label={`Chance node after playing column ${column}`}>
        <text y={14} fontSize={13} fontFamily={FONT} fontWeight={700} fill={INK}>
          After column {column} (+{fmt(node.points ?? 0)} now): which disc comes next?
        </text>
        <text y={32} fontSize={11} fontFamily={FONT} fill={INK_2}>
          Seven possibilities, each equally likely. For each one, the best immediate reply and what it scores.
        </text>
        {branches.map((b, i) => {
          const x = i * 108;
          const p = b.best?.points ?? 0;
          const t0 = ((i * secondsPerBranch) / total).toFixed(4);
          return (
            <g key={b.disc} transform={`translate(${x} 48)`}>
              {/* static content */}
              <g>
                <text x={0} y={10} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                  next disc
                </text>
                <g transform="translate(52 -4)">
                  <CellGlyph cell={b.disc} x={0} y={0} s={18} />
                </g>
                {b.best && <MiniBoard cells={b.best.board} y={20} s={t} />}
                <text x={0} y={20 + 7 * t + 14} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
                  reply: column {b.best?.column ?? "—"}
                </text>
                <rect x={0} y={20 + 7 * t + 22} width={barW} height={8} rx={3} fill="#1f1f23" />
                <rect x={0} y={20 + 7 * t + 22} width={(p / max) * barW} height={8} rx={3} fill={BLUE} />
                <text x={0} y={20 + 7 * t + 44} fontSize={12} fontFamily={FONT} fontWeight={700} fill={INK}>
                  +{fmt(p)}
                </text>
              </g>
              {/* travelling highlight: which branch is being "considered" */}
              <g className="engine-motion">
                <rect x={-4} y={-8} width={104} height={7 * t + 78} rx={8} fill="none" stroke={ACCENT} strokeWidth={2} opacity={0}>
                  <animate
                    attributeName="opacity"
                    calcMode="discrete"
                    values="0;1;0"
                    keyTimes={`0;${t0};${(((i + 1) * secondsPerBranch) / total).toFixed(4)}`}
                    dur={`${total}s`}
                    repeatCount="indefinite"
                  />
                </rect>
              </g>
            </g>
          );
        })}
        {/* the average */}
        <g transform={`translate(0 ${7 * t + 150})`}>
          <path d={`M0,0 H${W - 8}`} stroke={GRID} />
          <text y={22} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
            Fair value of column {column} = {fmt(node.points ?? 0)} now + average of the seven best replies
          </text>
          <text y={40} fontSize={12} fontFamily={FONT} fill={INK_2}>
            = {fmt(node.points ?? 0)} + ({values.map(fmt).join(" + ")}) ÷ 7 = {fmt(node.points ?? 0)} + {fmt(avg)} = <tspan fontWeight={700} fill={INK}>{fmt(node.fair ?? 0)}</tspan>
          </text>
          <text y={60} fontSize={11} fontFamily={FONT} fill={INK_3}>
            Optimistic (assume the best disc comes): {fmt(node.points ?? 0)} + {fmt(Math.max(...values))} = {fmt(node.optimistic ?? 0)} · Pessimistic (assume the worst): {fmt(node.points ?? 0)} + {fmt(Math.min(...values))} = {fmt(node.pessimistic ?? 0)}
          </text>
          <g className="engine-motion">
            <rect x={-4} y={6} width={W} height={60} rx={8} fill="none" stroke={ACCENT} strokeWidth={2} opacity={0}>
              <animate
                attributeName="opacity"
                calcMode="discrete"
                values="0;1;0"
                keyTimes={`0;${((branches.length * secondsPerBranch) / total).toFixed(4)};${((total - 0.01) / total).toFixed(4)}`}
                dur={`${total}s`}
                repeatCount="indefinite"
              />
            </rect>
          </g>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 3. Four ways to handle chance, as small multiples; the pick of each.
 * ========================================================================= */

export function ChanceStyles({ caption }: { caption?: string }) {
  const tree = loadTree();
  if (!tree) return <Missing what="The look-ahead position" />;
  const styles: { key: "greedy" | "optimistic" | "fair" | "pessimistic"; title: string; sub: string; value: (c: ColumnNode) => number }[] = [
    { key: "greedy", title: "Greedy", sub: "points right now, ignore the future", value: (c) => c.points ?? 0 },
    { key: "optimistic", title: "Optimistic", sub: "assume the luckiest next disc", value: (c) => c.optimistic ?? 0 },
    { key: "fair", title: "Fair (expectimax)", sub: "average over every next disc", value: (c) => c.fair ?? 0 },
    { key: "pessimistic", title: "Pessimistic", sub: "assume the unluckiest next disc", value: (c) => c.pessimistic ?? 0 },
  ];
  const panelW = 180;
  const panelH = 160;
  const W = 4 * (panelW + 12);
  const barMaxH = 70;
  const globalMax = Math.max(...styles.flatMap((st) => tree.columns.filter((c) => c.legal).map(st.value)), 1);
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${panelH + 10}`} role="img" aria-label="Four ways to value the same seven columns">
        {styles.map((st, si) => {
          const pick = tree.choice[st.key];
          return (
            <g key={st.key} transform={`translate(${si * (panelW + 12)} 0)`}>
              <rect width={panelW} height={panelH} rx={10} fill="#18181b" stroke={GRID} />
              <text x={10} y={18} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                {st.title}
              </text>
              <text x={10} y={32} fontSize={9.5} fontFamily={FONT} fill={INK_3}>
                {st.sub}
              </text>
              {tree.columns.map((c, i) => {
                const v = c.legal ? st.value(c) : 0;
                const h = (v / globalMax) * barMaxH;
                const x = 12 + i * 23;
                const chosen = c.column === pick;
                return (
                  <g key={c.column}>
                    <title>{`${st.title}: column ${c.column} = ${fmt(v)}`}</title>
                    <rect x={x} y={56 + (barMaxH - h)} width={16} height={h} rx={3} fill={chosen ? ACCENT : BLUE} opacity={c.legal ? 1 : 0.2} />
                    <text x={x + 8} y={140} textAnchor="middle" fontSize={9.5} fontFamily={FONT} fill={chosen ? ACCENT : INK_3} fontWeight={chosen ? 700 : 400}>
                      {c.column}
                    </text>
                    {chosen && (
                      <text x={x + 8} y={56 + (barMaxH - h) - 4} textAnchor="middle" fontSize={9.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                        {fmt(v)}
                      </text>
                    )}
                  </g>
                );
              })}
              <text x={10} y={154} fontSize={9.5} fontFamily={FONT} fill={INK_2}>
                picks column {pick}
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
              <th>column</th>
              {styles.map((st) => (
                <th key={st.key}>{st.title}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {tree.columns.map((c) => (
              <tr key={c.column}>
                <td>{c.column}{c.legal ? "" : " (full)"}</td>
                {styles.map((st) => (
                  <td key={st.key}>{c.legal ? fmt(st.value(c)) : "—"}</td>
                ))}
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
 * 4. Why the tree explodes: choice × chance per ply.
 * ========================================================================= */

export function TreeGrowth({ caption }: { caption?: string }) {
  const rows = [1, 2, 3, 4, 5].map((d) => ({ depth: d, leaves: Math.pow(49, d) }));
  const W = 640;
  const rowH = 26;
  const H = rows.length * rowH + 52;
  const labelW = 150;
  const maxLog = Math.log10(rows[rows.length - 1].leaves);
  return (
    <figure className="engine-fig">
      <div style={{ fontSize: 13, color: INK, fontWeight: 600 }}>
        Positions at the bottom of a full-width tree: 7 columns × 7 possible next discs per ply
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Tree size by depth" style={{ marginTop: 6 }}>
        {rows.map((r, i) => {
          const y = 10 + i * rowH;
          const w = (Math.log10(r.leaves) / maxLog) * 380;
          return (
            <g key={r.depth}>
              <text x={labelW - 10} y={y + 8} textAnchor="end" dominantBaseline="central" fontSize={11.5} fontFamily={FONT} fill={INK_2}>
                look {r.depth} move{r.depth === 1 ? "" : "s"} ahead
              </text>
              <rect x={labelW} y={y} width={w} height={14} rx={4} fill={BLUE} />
              <text x={labelW + w + 6} y={y + 8} dominantBaseline="central" fontSize={11.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                {r.leaves.toLocaleString("en-US")}
              </text>
            </g>
          );
        })}
        <text x={labelW} y={H - 20} fontSize={10} fontFamily={FONT} fill={INK_3}>
          bar length is logarithmic — each step is 49× the last.
        </text>
        <text x={labelW} y={H - 7} fontSize={10} fontFamily={FONT} fill={INK_3}>
          gray-disc reveals add further chance branches on top of these.
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 5. The sibling-ranking problem: what the training data did and did not see.
 * ========================================================================= */

export function SiblingTrap({
  played,
  caption,
}: {
  /** The column the behaviour policy actually played at this root. */
  played: number;
  caption?: string;
}) {
  const tree = loadTree();
  if (!tree) return <Missing what="The look-ahead position" />;
  const t = 12;
  const W = 760;
  const rowH = 7 * t + 64;
  const H = rowH * 2 + 70;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Sibling coverage in training data">
        <text y={14} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
          How the data was collected in most earlier attempts: one root, one move played, one label
        </text>
        <g transform="translate(0 26)">
          {tree.columns.map((c, i) => {
            const seen = c.column === played;
            return (
              <g key={c.column} transform={`translate(${i * 108} 0)`}>
                {c.legal && c.board ? <MiniBoard cells={c.board} s={t} dim={seen ? [] : Array.from({ length: 49 }, (_, k) => k)} /> : null}
                <rect x={0} y={7 * t + 6} width={96} height={18} rx={4} fill={seen ? "rgba(25,158,112,0.2)" : "rgba(217,89,38,0.15)"} stroke={seen ? AQUA : ORANGE} />
                <text x={48} y={7 * t + 15.5} textAnchor="middle" dominantBaseline="central" fontSize={9.5} fontFamily={FONT} fontWeight={700} fill={seen ? AQUA : ORANGE}>
                  {seen ? "labelled" : "no label"}
                </text>
                <text x={0} y={7 * t + 40} fontSize={10} fontFamily={FONT} fill={INK_3}>
                  column {c.column}
                </text>
              </g>
            );
          })}
        </g>
        <text y={rowH + 44} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
          What deployment asks: rank all seven — including the six the model was never shown
        </text>
        <g transform={`translate(0 ${rowH + 56})`}>
          {tree.columns.map((c, i) => {
            const seen = c.column === played;
            return (
              <g key={c.column} transform={`translate(${i * 108} 0)`}>
                {c.legal && c.board ? <MiniBoard cells={c.board} s={t} /> : null}
                <text x={48} y={7 * t + 18} textAnchor="middle" fontSize={16} fontFamily={FONT} fontWeight={800} fill={seen ? AQUA : ORANGE}>
                  {seen ? "✓" : "?"}
                </text>
                <text x={48} y={7 * t + 34} textAnchor="middle" fontSize={9.5} fontFamily={FONT} fill={INK_3}>
                  {seen ? "measured" : "extrapolated"}
                </text>
              </g>
            );
          })}
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}
