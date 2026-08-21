/**
 * Figures for the /learn/concepts pages on privileged information and on the
 * four families of learning tried here.
 *
 * The position in the oracle figures comes from
 * web/content/learn/oracle-scenario.json, which
 * web/scripts/generate-oracle-scenario.ts plays through the repository's
 * TypeScript engine in latent mode, so both the player's view and the hidden
 * values are engine output rather than a drawing. The learning figures show
 * mechanism only: where a figure needs a shape that no retained record
 * provides (a policy's seven probabilities, a search's visit counts), the
 * figure says on its face that the shape is illustrative.
 *
 * Server components only: SVG + SMIL, with a static equivalent for every
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

interface OracleScenario {
  seed: string;
  movesPlayed: number;
  level: number;
  movesRemaining: number;
  nextDisc: number;
  /** What the player sees: "8" solid gray, "9" cracked gray. */
  board: string;
  /** The same board with each covered cell replaced by its hidden number. */
  oracle: string;
  covered: number[];
  cracked: number[];
}

function loadOracle(): OracleScenario | null {
  const path = join(process.cwd(), "content", "learn", "oracle-scenario.json");
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8")) as OracleScenario;
}

function Missing({ what }: { what: string }) {
  return (
    <div className="engine-fig" style={{ color: INK_3, fontSize: 13 }}>
      {what} is not available in this checkout. Run{" "}
      <code>node --experimental-strip-types scripts/generate-oracle-scenario.ts</code> inside{" "}
      <code>web/</code> to regenerate <code>content/learn/oracle-scenario.json</code>.
    </div>
  );
}

/** Renders wrapped text lines at a fixed width, for SVG label columns. */
function wrap(text: string, width: number): string[] {
  const lines: string[] = [];
  let line = "";
  for (const word of text.split(" ")) {
    if (line.length === 0) line = word;
    else if ((line + " " + word).length <= width) line += " " + word;
    else {
      lines.push(line);
      line = word;
    }
  }
  if (line) lines.push(line);
  return lines;
}

/* =========================================================================
 * 1. One position, two information states: the player's view and the oracle's.
 * ========================================================================= */

export function OracleSplit({ caption }: { caption?: string }) {
  const scene = loadOracle();
  if (!scene) return <Missing what="The hidden-information position" />;
  const s = 30;
  const boardW = 7 * s;
  const rightX = boardW + 56;
  const textX = rightX + boardW + 34;
  const W = 780;
  const H = 7 * s + 118;
  // Up to six covered cells are linked across the two panels by a travelling
  // highlight; the numbers themselves are always drawn, so the static figure
  // carries the same information.
  const linked = scene.covered.filter((_, i) => i % Math.ceil(scene.covered.length / 6) === 0).slice(0, 6);
  const step = 0.9;
  const total = linked.length * step + 1.2;
  const assignments = Math.pow(7, scene.covered.length);
  const panels: { x: number; cells: string; title: string; sub: string; tone: string }[] = [
    {
      x: 0,
      cells: scene.board,
      title: "What the player sees",
      sub: "and the only thing a legal policy may read",
      tone: AQUA,
    },
    {
      x: rightX,
      cells: scene.oracle,
      title: "What a privileged planner sees",
      sub: "the same board with the answer key filled in",
      tone: ORANGE,
    },
  ];
  return (
    <figure className="engine-fig">
      <svg
        viewBox={`0 -${s + 22} ${W} ${H + s + 22}`}
        role="img"
        aria-label="The same Drop7 position as seen by the player and by a privileged planner"
      >
        <text x={0} y={-s - 8} fontSize={11} fontFamily={FONT} fill={INK_3}>
          next disc
        </text>
        <g transform={`translate(60 ${-s - 20})`}>
          <CellGlyph cell={scene.nextDisc} x={0} y={0} s={s} />
        </g>
        <text x={100} y={-s - 8} fontSize={11} fontFamily={FONT} fill={INK_3}>
          {scene.movesRemaining} drop{scene.movesRemaining === 1 ? "" : "s"} until the next rise
        </text>
        {panels.map((panel) => (
          <g key={panel.title} transform={`translate(${panel.x} 0)`}>
            <text y={-14} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={panel.tone}>
              {panel.title}
            </text>
            <text y={-1} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
              {panel.sub}
            </text>
            <MiniBoard cells={panel.cells} y={8} s={s} />
            {linked.map((index, i) => {
              const cx = (index % 7) * s;
              const cy = Math.floor(index / 7) * s + 8;
              return (
                <g key={index} className="engine-motion">
                  <rect
                    x={cx + 1}
                    y={cy + 1}
                    width={s - 2}
                    height={s - 2}
                    rx={s * 0.14}
                    fill="none"
                    stroke={ACCENT}
                    strokeWidth={2.5}
                    opacity={0}
                  >
                    <animate
                      attributeName="opacity"
                      calcMode="discrete"
                      values="0;1;0"
                      keyTimes={`0;${((i * step) / total).toFixed(4)};${(((i + 1) * step) / total).toFixed(4)}`}
                      dur={`${total}s`}
                      repeatCount="indefinite"
                    />
                  </rect>
                </g>
              );
            })}
          </g>
        ))}
        <g transform={`translate(${textX} 0)`}>
          <text y={-1} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
            One position, two information states
          </text>
          {wrap(
            `Every gray disc already has a number; the game fixed it when the row appeared. The player never sees it until two clears land beside it. Here ${scene.covered.length} discs are covered and ${scene.cracked.length} of them ${scene.cracked.length === 1 ? "is" : "are"} already cracked.`,
            36,
          ).map((line, i) => (
            <text key={i} y={20 + i * 15} fontSize={11.5} fontFamily={FONT} fill={INK_2}>
              {line}
            </text>
          ))}
          <text y={132} fontSize={11.5} fontFamily={FONT} fill={INK_2}>
            Possible answer keys for this board:
          </text>
          <text y={152} fontSize={13} fontFamily={FONT} fontWeight={700} fill={ACCENT}>
            7^{scene.covered.length} = {assignments.toLocaleString("en-US")}
          </text>
          <text y={172} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
            The planner is handed the one that is true.
          </text>
        </g>
        <g transform={`translate(0 ${7 * s + 30})`}>
          <path d={`M0,0 H${W - 12}`} stroke={GRID} />
          <text y={20} fontSize={11} fontFamily={FONT} fill={INK_3}>
            Position played by the engine in latent mode from figure seed {scene.seed}, after {scene.movesPlayed} moves
          </text>
          <text y={35} fontSize={11} fontFamily={FONT} fill={INK_3}>
            (level {scene.level}). A figure, not gameplay evidence.
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 2. Teacher, labels, student, blind evaluation — and where the line is.
 * ========================================================================= */

export function TeacherStudentFlow({ caption }: { caption?: string }) {
  const W = 780;
  const boxH = 132;
  const H = boxH + 112;
  const boxes: { x: number; w: number; title: string; tone: string; lines: string[] }[] = [
    {
      x: 0,
      w: 178,
      title: "1 · Privileged teacher",
      tone: ORANGE,
      lines: [
        "reads the board, the next disc,",
        "the rise clock —",
        "and the hidden gray numbers",
        "and the future disc tape.",
        "Runs offline only.",
      ],
    },
    {
      x: 198,
      w: 148,
      title: "2 · Labels",
      tone: ORANGE,
      lines: [
        "for each public position:",
        "the column the teacher",
        "preferred, or the value it",
        "assigned. The privileged",
        "inputs are not stored.",
      ],
    },
    {
      x: 420,
      w: 158,
      title: "3 · Public student",
      tone: AQUA,
      lines: [
        "trained to reproduce the",
        "labels from public inputs",
        "alone: board, next disc,",
        "rise clock, terminal flag.",
        "Then frozen.",
      ],
    },
    {
      x: 598,
      w: 182,
      title: "4 · Blind evaluation",
      tone: AQUA,
      lines: [
        "the frozen student plays",
        "fresh unseen games through",
        "the public interface, paired",
        "against fair D4, judged on",
        "whole-game mean score.",
      ],
    },
  ];
  const boundaryX = 384;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -18 ${W} ${H}`} role="img" aria-label="Teacher, labels, student, and blind evaluation">
        {boxes.map((box) => (
          <g key={box.title} transform={`translate(${box.x} 0)`}>
            <rect width={box.w} height={boxH} rx={10} fill="#18181b" stroke={box.tone} strokeOpacity={0.5} />
            <text x={12} y={22} fontSize={12} fontFamily={FONT} fontWeight={700} fill={box.tone}>
              {box.title}
            </text>
            {box.lines.map((line, i) => (
              <text key={i} x={12} y={44 + i * 15} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
                {line}
              </text>
            ))}
          </g>
        ))}
        {[
          { from: 178, to: 198 },
          { from: 346, to: 420 },
          { from: 578, to: 598 },
        ].map((arrow) => (
          <g key={arrow.from}>
            <path
              d={`M${arrow.from + 2},${boxH / 2} H${arrow.to - 8}`}
              stroke={INK_3}
              strokeWidth={1.5}
              fill="none"
            />
            <path
              d={`M${arrow.to - 8},${boxH / 2 - 4} L${arrow.to - 2},${boxH / 2} L${arrow.to - 8},${boxH / 2 + 4} Z`}
              fill={INK_3}
            />
          </g>
        ))}
        {/* the information boundary */}
        <path d={`M${boundaryX},-14 V${boxH + 16}`} stroke={ORANGE} strokeWidth={2} strokeDasharray="6 5" />
        <text x={boundaryX - 6} y={-4} textAnchor="end" fontSize={10.5} fontFamily={FONT} fontWeight={700} fill={ORANGE}>
          information boundary
        </text>
        <text x={boundaryX + 8} y={-4} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          only public inputs past here
        </text>
        {/* a label crossing the boundary, animated; the arrow beneath is static */}
        <g className="engine-motion">
          <circle r={5} fill={ACCENT}>
            <animate attributeName="cx" values="352;414" dur="2.6s" repeatCount="indefinite" />
            <animate attributeName="cy" values={`${boxH / 2};${boxH / 2}`} dur="2.6s" repeatCount="indefinite" />
            <animate attributeName="opacity" values="0;1;1;0" keyTimes="0;0.15;0.85;1" dur="2.6s" repeatCount="indefinite" />
          </circle>
        </g>
        <g transform={`translate(0 ${boxH + 34})`}>
          <rect width={W - 12} height={54} rx={8} fill="rgba(217,89,38,0.12)" stroke={ORANGE} strokeOpacity={0.5} />
          <text x={12} y={19} fontSize={11} fontFamily={FONT} fontWeight={700} fill={ORANGE}>
            The teacher is never a result.
          </text>
          <text x={12} y={35} fontSize={11} fontFamily={FONT} fill={INK_2}>
            Its score measures how much the hidden information is worth. Only box 4 can produce a
          </text>
          <text x={12} y={48} fontSize={11} fontFamily={FONT} fill={INK_2}>
            policy number, and only if nothing privileged reached box 3.
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 3. An n-tuple network: many small windows, each looking up one number.
 * ========================================================================= */

export function NTupleWindows({ caption }: { caption?: string }) {
  const scene = loadOracle();
  if (!scene) return <Missing what="The board used by the n-tuple figure" />;
  const s = 22;
  const cells = scene.board;
  const windows: { title: string; note: string; indexes: number[] }[] = [
    {
      title: "a row window",
      note: "4 cells across · 28 of them",
      indexes: [4 * 7 + 3, 4 * 7 + 4, 4 * 7 + 5, 4 * 7 + 6],
    },
    {
      title: "a column window",
      note: "4 cells down · 28 of them",
      indexes: [2 * 7 + 0, 3 * 7 + 0, 4 * 7 + 0, 5 * 7 + 0],
    },
    {
      title: "a 2×2 block",
      note: "4 neighbouring cells · 36 of them",
      indexes: [4 * 7 + 1, 4 * 7 + 2, 5 * 7 + 1, 5 * 7 + 2],
    },
  ];
  const panelW = 7 * s + 24;
  const W = 780;
  const H = 7 * s + 140;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -16 ${W} ${H}`} role="img" aria-label="How an n-tuple network reads a board">
        {windows.map((win, wi) => {
          const code = win.indexes.map((index) => cells[index]).join("");
          return (
            <g key={win.title} transform={`translate(${wi * panelW} 0)`}>
              <text y={-2} fontSize={11.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                {win.title}
              </text>
              <MiniBoard cells={cells} y={8} s={s} dim={Array.from({ length: 49 }, (_, k) => k).filter((k) => !win.indexes.includes(k))} />
              {win.indexes.map((index) => (
                <rect
                  key={index}
                  x={(index % 7) * s + 1}
                  y={Math.floor(index / 7) * s + 9}
                  width={s - 2}
                  height={s - 2}
                  rx={s * 0.14}
                  fill="none"
                  stroke={BLUE}
                  strokeWidth={2}
                />
              ))}
              <text y={7 * s + 26} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
                {win.note}
              </text>
              <text y={7 * s + 44} fontSize={12} fontFamily={FONT} fontWeight={700} fill={ACCENT}>
                pattern {code}
              </text>
              <text y={7 * s + 60} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
                → one learned number
              </text>
            </g>
          );
        })}
        <g transform={`translate(${3 * panelW + 8} 20)`}>
          <text y={0} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
            92 windows, one addition
          </text>
          {wrap(
            "Every window reads four cells and turns them into a four-digit code. The code, the rise phase and the next disc select one slot in a big table of learned numbers. Add the 92 numbers up and that sum is the board's score. Windows that differ only by where they sit share a table, so 92 windows use 17 tables.",
            34,
          ).map((line, i) => (
            <text key={i} y={20 + i * 15} fontSize={11.5} fontFamily={FONT} fill={INK_2}>
              {line}
            </text>
          ))}
          <text y={182} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
            Nothing here is a neural network: it is a
          </text>
          <text y={196} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
            lookup and a sum, which is why it is fast
          </text>
          <text y={210} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
            enough to sit at a search leaf.
          </text>
        </g>
        <text y={7 * s + 88} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          Board from figure seed {scene.seed}; the window positions and codes are read off it
        </text>
        <text y={7 * s + 102} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          directly. Table layout as implemented in src/core/native/ntuple.hpp.
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 4. A value network: board in, one number out — and the NNUE shortcut.
 * ========================================================================= */

export function ValueNetShape({ caption }: { caption?: string }) {
  const scene = loadOracle();
  if (!scene) return <Missing what="The board used by the value-network figure" />;
  const s = 22;
  const W = 780;
  const H = 282;
  const planeX = 7 * s + 40;
  const planes = ["is this cell empty?", "is it a 3?", "is it covered?", "is it cracked?"];
  const chips = ["next disc", "drops to rise"];
  const hiddenX = planeX + 190;
  const outX = hiddenX + 140;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -16 ${W} ${H}`} role="img" aria-label="A value network turns a board into one number">
        <text y={-2} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
          A value network: one board in, one number out
        </text>
        <MiniBoard cells={scene.board} y={12} s={s} />
        <text y={7 * s + 30} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          the public position
        </text>
        {planes.map((plane, i) => (
          <g key={plane} transform={`translate(${planeX} ${16 + i * 26})`}>
            <rect width={150} height={20} rx={5} fill="#18181b" stroke={GRID} />
            <text x={9} y={14} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
              {plane}
            </text>
          </g>
        ))}
        {chips.map((chip, i) => (
          <g key={chip} transform={`translate(${planeX} ${128 + i * 26})`}>
            <rect width={150} height={20} rx={5} fill="rgba(57,135,229,0.14)" stroke={BLUE} strokeOpacity={0.5} />
            <text x={9} y={14} fontSize={10.5} fontFamily={FONT} fill={INK_2}>
              {chip}
            </text>
          </g>
        ))}
        <text x={planeX} y={196} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          one input per question per cell
        </text>
        {[0, 1].map((layer) =>
          Array.from({ length: 6 }, (_, node) => (
            <circle
              key={`${layer}-${node}`}
              cx={hiddenX + layer * 46}
              cy={30 + node * 26}
              r={7}
              fill="none"
              stroke={INK_3}
            />
          )),
        )}
        <text x={hiddenX - 6} y={196} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          two small hidden layers
        </text>
        <g transform={`translate(${outX} 62)`}>
          <rect width={128} height={56} rx={10} fill="#18181b" stroke={ACCENT} strokeOpacity={0.6} />
          <text x={64} y={24} textAnchor="middle" fontSize={11.5} fontFamily={FONT} fill={INK_2}>
            how promising
          </text>
          <text x={64} y={42} textAnchor="middle" fontSize={11.5} fontFamily={FONT} fontWeight={700} fill={ACCENT}>
            one number
          </text>
        </g>
        {[
          { from: 7 * s + 8, to: planeX - 6, y: 96 },
          { from: planeX + 158, to: hiddenX - 12, y: 96 },
          { from: hiddenX + 58, to: outX - 6, y: 96 },
        ].map((arrow) => (
          <g key={arrow.from}>
            <path d={`M${arrow.from},${arrow.y} H${arrow.to - 6}`} stroke={INK_3} strokeWidth={1.5} />
            <path d={`M${arrow.to - 6},${arrow.y - 4} L${arrow.to},${arrow.y} L${arrow.to - 6},${arrow.y + 4} Z`} fill={INK_3} />
          </g>
        ))}
        <g transform="translate(0 214)">
          <rect width={W - 12} height={54} rx={8} fill="rgba(57,135,229,0.10)" stroke={BLUE} strokeOpacity={0.45} />
          <text x={12} y={19} fontSize={11} fontFamily={FONT} fontWeight={700} fill={BLUE}>
            The NNUE idea
          </text>
          <text x={12} y={35} fontSize={11} fontFamily={FONT} fill={INK_2}>
            A move changes only a few cells, so the first layer is built to be patched — subtract the
          </text>
          <text x={12} y={48} fontSize={11} fontFamily={FONT} fill={INK_2}>
            weights that switched off, add the ones that switched on — instead of recomputing every leaf.
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 5. A policy network: board in, seven probabilities out.
 * ========================================================================= */

export function PolicyNetShape({ caption }: { caption?: string }) {
  const scene = loadOracle();
  if (!scene) return <Missing what="The board used by the policy figure" />;
  const s = 22;
  const W = 780;
  const H = 240;
  // Illustrative only: no retained record in this repository publishes a
  // trained policy's per-column probabilities, so the figure says so on its
  // face and the numbers below are a shape, not a measurement.
  const shape = [0.06, 0.04, 0.11, 0.09, 0.42, 0.18, 0.1];
  const barsX = 7 * s + 220;
  const barMaxH = 108;
  const best = shape.indexOf(Math.max(...shape));
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -16 ${W} ${H}`} role="img" aria-label="A policy network turns a board into seven probabilities">
        <text y={-2} fontSize={12.5} fontFamily={FONT} fontWeight={700} fill={INK}>
          A policy: one board in, seven numbers out
        </text>
        <MiniBoard cells={scene.board} y={12} s={s} />
        <text y={7 * s + 30} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
          the public position
        </text>
        <g transform={`translate(${7 * s + 26} 26)`}>
          {wrap(
            "Instead of scoring a board, the network scores the seven columns directly and normalises them into probabilities. Playing means sampling or taking the largest. Training pushes probability toward the columns that led to more score over the rest of the game.",
            30,
          ).map((line, i) => (
            <text key={i} y={i * 15} fontSize={11.5} fontFamily={FONT} fill={INK_2}>
              {line}
            </text>
          ))}
        </g>
        <g transform={`translate(${barsX} 0)`}>
          {shape.map((p, column) => {
            const h = p * barMaxH * 2;
            return (
              <g key={column} transform={`translate(${column * 34} 0)`}>
                <rect
                  x={2}
                  y={12 + barMaxH - h}
                  width={22}
                  height={h}
                  rx={4}
                  fill={column === best ? ACCENT : BLUE}
                />
                <text
                  x={13}
                  y={12 + barMaxH + 16}
                  textAnchor="middle"
                  fontSize={10.5}
                  fontFamily={FONT}
                  fill={column === best ? ACCENT : INK_3}
                  fontWeight={column === best ? 700 : 400}
                >
                  {column}
                </text>
              </g>
            );
          })}
          <text x={0} y={12 + barMaxH + 36} fontSize={10.5} fontFamily={FONT} fill={INK_3}>
            probability of each column · they sum to 1
          </text>
          <text x={0} y={12 + barMaxH + 52} fontSize={10.5} fontFamily={FONT} fill={ORANGE}>
            bar heights are illustrative, not a measured policy
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/* =========================================================================
 * 6. Monte Carlo tree search: the tree grows where the playouts look good.
 * ========================================================================= */

export function MctsTreeGrowth({ caption }: { caption?: string }) {
  const stages: { playouts: number; visits: number[]; depth: number }[] = [
    { playouts: 8, visits: [1, 1, 1, 1, 2, 1, 1], depth: 1 },
    { playouts: 32, visits: [3, 2, 4, 3, 12, 5, 3], depth: 2 },
    { playouts: 128, visits: [6, 5, 12, 8, 74, 15, 8], depth: 3 },
    { playouts: 512, visits: [12, 9, 30, 14, 392, 35, 20], depth: 4 },
  ];
  const panelW = 190;
  const W = 4 * panelW;
  const H = 260;
  const colW = 22;
  return (
    <figure className="engine-fig">
      <svg viewBox={`0 -16 ${W} ${H}`} role="img" aria-label="A search tree growing toward the promising branch">
        {stages.map((stage, si) => {
          const best = stage.visits.indexOf(Math.max(...stage.visits));
          const share = Math.max(...stage.visits) / stage.playouts;
          return (
            <g key={stage.playouts} transform={`translate(${si * panelW} 0)`}>
              <text y={-2} fontSize={11.5} fontFamily={FONT} fontWeight={700} fill={INK}>
                after {stage.playouts} playouts
              </text>
              <circle cx={80} cy={20} r={7} fill={INK_2} />
              <text x={94} y={24} fontSize={10} fontFamily={FONT} fill={INK_3}>
                the position now
              </text>
              {stage.visits.map((visits, column) => {
                const x = 12 + column * colW;
                const r = 3 + Math.sqrt(visits) * 1.5;
                return (
                  <g key={column}>
                    <path d={`M80,27 L${x},46`} stroke={GRID} strokeWidth={1} />
                    <circle cx={x} cy={50} r={r} fill={column === best ? ACCENT : BLUE} opacity={column === best ? 1 : 0.7} />
                    <text x={x} y={72} textAnchor="middle" fontSize={9} fontFamily={FONT} fill={INK_3}>
                      {visits}
                    </text>
                  </g>
                );
              })}
              {/* the favoured branch, expanded deeper each stage */}
              {Array.from({ length: stage.depth }, (_, level) => {
                const y = 92 + level * 26;
                const cx = 12 + best * colW;
                const spread = 8 + level * 6;
                return (
                  <g key={level}>
                    {[-1, 0, 1].map((offset) => (
                      <g key={offset}>
                        <path
                          d={`M${cx},${y - 22} L${cx + offset * spread},${y}`}
                          stroke={GRID}
                          strokeWidth={1}
                        />
                        <circle cx={cx + offset * spread} cy={y} r={3.5} fill={ACCENT} opacity={0.85 - level * 0.12} />
                      </g>
                    ))}
                  </g>
                );
              })}
              <g className="engine-motion">
                <circle cx={12 + best * colW} cy={50} r={12} fill="none" stroke={ACCENT} strokeWidth={1.5} opacity={0}>
                  <animate
                    attributeName="opacity"
                    values="0;0.9;0"
                    keyTimes="0;0.5;1"
                    dur="2.4s"
                    begin={`${si * 0.3}s`}
                    repeatCount="indefinite"
                  />
                </circle>
              </g>
              <text y={200} fontSize={10} fontFamily={FONT} fill={INK_3}>
                {Math.round(share * 100)}% of playouts went to column {best}
              </text>
            </g>
          );
        })}
        <text y={212} fontSize={10.5} fontFamily={FONT} fill={ORANGE}>
          Illustrative counts — this figure shows the mechanism, not a measured search. Each playout picks
        </text>
        <text y={226} fontSize={10.5} fontFamily={FONT} fill={ORANGE}>
          the child with the best average so far plus a bonus for being under-tried, plays it forward with a
        </text>
        <text y={240} fontSize={10.5} fontFamily={FONT} fill={ORANGE}>
          quick policy, and reports the result back up the tree.
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}
