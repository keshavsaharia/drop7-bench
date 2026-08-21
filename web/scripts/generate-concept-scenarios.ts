/**
 * Generates web/content/learn/concept-scenarios.json: small, exact look-ahead
 * trees computed by the repository's TypeScript engine, used by the concept
 * pages to demonstrate choice nodes, chance nodes, expected value, and how
 * optimistic / fair / pessimistic chance handling can pick different columns.
 *
 * The "evaluator" here is deliberately the simplest possible one — points
 * scored by the move — so the reader can check every number by hand. It is a
 * teaching device, not a research policy.
 *
 *   cd web && npm run concept:scenarios
 */
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import {
  findPoppers,
  legalColumns,
  playMove,
  serializeBoard,
  type Board,
  type DiscValue,
  type GameState,
} from "../../src/core/typescript/engine.ts";

const DISCS: DiscValue[] = [1, 2, 3, 4, 5, 6, 7];

interface RootSpec { id: string; rows: string[]; nextDisc: DiscValue; movesRemaining?: number }

// Roots are found by a deterministic search (fixed PRNG seed) over small boards:
// stable (no popper), no reveal chance anywhere in the two-ply tree, and the
// three chance styles pick three different columns. The first few hits, sorted
// by disc count, are emitted; the page names the one it uses.
function mulberry32(seed: number) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
function randomRoot(rand: () => number, index: number): RootSpec {
  const cells = new Array(49).fill(0);
  for (let c = 0; c < 7; c += 1) {
    const height = Math.floor(rand() * 5); // 0..4 discs in the column
    for (let h = 0; h < height; h += 1) {
      const r = 6 - h;
      const v = rand() < 0.08 ? 8 : 1 + Math.floor(rand() * 7);
      cells[r * 7 + c] = v;
    }
  }
  const rows: string[] = [];
  for (let r = 0; r < 7; r += 1) rows.push(cells.slice(r * 7, r * 7 + 7).join(""));
  return { id: `auto-${index}`, rows, nextDisc: (1 + Math.floor(rand() * 7)) as DiscValue };
}
const ROOTS: RootSpec[] = [];
{
  const rand = mulberry32(0x5eed7001);
  for (let i = 0; i < 30000; i += 1) ROOTS.push(randomRoot(rand, i));
}

function boardOf(rows: string[]): Board {
  const b = rows.join("");
  if (b.length !== 49) throw new Error("bad board");
  return b.split("").map(Number) as unknown as Board;
}

function state(board: Board, nextDisc: DiscValue, movesRemaining = 4): GameState {
  return { board, nextDisc, score: 0, level: 1, movesRemaining, movesPlayed: 0, gameOver: false };
}

// The engine also draws the next visible disc from the random source, so a
// constant source is used; the next disc is then overridden by enumeration.
// Reveal chance is detected from the wave records: any wave with revealed > 0
// means the branch depended on a hidden value, and that root is skipped.
const constantRandom = () => 0.5;

function tryMove(s: GameState, column: number) {
  const r = playMove(s, column, constantRandom);
  if (!r) return null;
  if (r.waves.some((w) => w.revealed > 0)) return null;
  return r;
}

const out: Record<string, unknown> = {};
const hits: Array<{ id: string; discCount: number; grays: number; data: unknown }> = [];
const diag = { stable: 0, revealFree: 0, twoWay: 0, threeWay: 0 };
for (const root of ROOTS) {
  const board = boardOf(root.rows);
  if (findPoppers(board).length > 0) continue;
  diag.stable += 1;
  const s0 = state(board, root.nextDisc, root.movesRemaining);
  const legal = legalColumns(board);
  const columns = [] as Array<Record<string, unknown>>;
  let reveal = false;
  for (let c = 0; c < 7; c += 1) {
    if (!legal.includes(c)) { columns.push({ column: c, legal: false }); continue; }
    const r1 = tryMove(s0, c);
    if (!r1) { reveal = true; break; }
    const branches = [] as Array<Record<string, unknown>>;
    for (const d of DISCS) {
      const s1 = { ...r1.state, nextDisc: d, score: 0 };
      const replies = [] as Array<{ column: number; points: number; board: string; waves: number }>;
      for (const c2 of legalColumns(s1.board)) {
        const r2 = tryMove(s1, c2);
        if (!r2) { reveal = true; break; }
        replies.push({ column: c2, points: r2.scoreDelta, board: serializeBoard(r2.state.board), waves: r2.waves.length });
      }
      if (reveal) break;
      const best = replies.reduce((a, b) => (b.points > a.points ? b : a), replies[0]);
      branches.push({ disc: d, replies, best: best ? { column: best.column, points: best.points, board: best.board, waves: best.waves } : null });
    }
    if (reveal) break;
    const vals = branches.map((b) => (b.best as { points: number } | null)?.points ?? 0);
    const fair = r1.scoreDelta + vals.reduce((a, b) => a + b, 0) / vals.length;
    const optimistic = r1.scoreDelta + Math.max(...vals);
    const pessimistic = r1.scoreDelta + Math.min(...vals);
    columns.push({
      column: c, legal: true, points: r1.scoreDelta, board: serializeBoard(r1.state.board), waves: r1.waves.length,
      gameOver: r1.state.gameOver, branches, fair, optimistic, pessimistic,
    });
  }
  if (reveal) continue;
  diag.revealFree += 1;
  const legalCols = columns.filter((c) => c.legal) as Array<{ column: number; fair: number; optimistic: number; pessimistic: number; points: number }>;
  const pick = (k: "fair" | "optimistic" | "pessimistic") => legalCols.reduce((a, b) => (b[k] > a[k] ? b : a), legalCols[0]).column;
  const greedy = legalCols.reduce((a, b) => (b.points > a.points ? b : a), legalCols[0]).column;
  const choice = { greedy, fair: pick("fair"), optimistic: pick("optimistic"), pessimistic: pick("pessimistic") };
  const distinct = new Set([choice.fair, choice.optimistic, choice.pessimistic]).size;
  if (distinct >= 2) diag.twoWay += 1;
  if (distinct >= 3) diag.threeWay += 1;
  if (distinct < 3) continue;
  const discCount = root.rows.join("").split("").filter((ch) => ch !== "0").length;
  const grays = root.rows.join("").split("").filter((ch) => ch === "8").length;
  hits.push({ id: root.id, discCount, grays, data: { board: serializeBoard(board), nextDisc: root.nextDisc, columns, choice } });
  if (hits.length >= 400) break;
}
hits.sort((a, b) => (a.discCount - b.discCount) || (b.grays - a.grays));
for (const h of hits.slice(0, 6)) {
  const d = h.data as { choice: Record<string, number>; nextDisc: number; columns: Array<{ legal: boolean; column: number; fair?: number; optimistic?: number; pessimistic?: number; points?: number }> };
  console.log(h.id, "discs", h.discCount, "grays", h.grays, "next", d.nextDisc, JSON.stringify(d.choice), d.columns.filter((c) => c.legal).map((c) => `${c.column}:p${c.points} f${c.fair?.toFixed(1)} o${c.optimistic} p${c.pessimistic}`).join(" | "));
}
if (hits.length > 0) out.tree = (hits[0].data as object);
console.log("hits:", hits.length, "diag", JSON.stringify(diag));
const target = join(import.meta.dirname, "..", "content", "learn", "concept-scenarios.json");
writeFileSync(target, JSON.stringify(out, null, 1) + "\n");
console.log("wrote", target);
