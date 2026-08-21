/**
 * Generates web/content/learn/rules-scenarios.json by playing every "Learn the
 * rules" scenario through the repository's TypeScript engine in latent mode.
 * The page's animations are therefore engine output, not illustrations.
 *
 *   cd web && npm run figures:rules
 */
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import {
  playMove,
  serializeBoard,
  type Board,
  type DiscValue,
  type GameState,
  type LatentValues,
  type MoveAnimationFrame,
} from "../../src/core/typescript/engine.ts";

interface ScenarioSpec {
  id: string;
  rows: string[];
  nextDisc: DiscValue;
  column: number;
  movesRemaining?: number;
  latent?: Record<number, DiscValue>;
  nextCoveredRow?: DiscValue[];
}

interface Frame {
  kind: "start" | "drop" | "wave" | "covers" | "gravity" | "rise" | "end";
  board: string;
  indexes: number[];
  chainDepth: number | null;
}

const SPECS: ScenarioSpec[] = [
  { id: "drop", rows: ["0000000","0000000","0000000","0000000","0000000","0006000","0004000"], nextDisc: 5, column: 3 },
  { id: "row-clear", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0033006"], nextDisc: 3, column: 4 },
  { id: "column-clear", rows: ["0000000","0000000","0000000","0000000","0000000","0030000","0030006"], nextDisc: 3, column: 2 },
  { id: "mismatch", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0222000"], nextDisc: 2, column: 4 },
  { id: "gray-counts", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0033800"], nextDisc: 3, column: 1 },
  { id: "crack", rows: ["0000000","0000000","0000000","0000000","0000000","0008000","0033000"], nextDisc: 3, column: 4, latent: { 38: 5 } },
  { id: "reveal", rows: ["0000000","0000000","0000000","0000000","0000000","0009000","0033000"], nextDisc: 3, column: 4, latent: { 38: 5 } },
  { id: "both-sides", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0380000"], nextDisc: 3, column: 3, latent: { 44: 4 } },
  { id: "double-hit", rows: ["0000000","0000000","0000000","0000000","0000000","0330000","0882000"], nextDisc: 3, column: 3, latent: { 43: 6, 44: 4 } },
  { id: "chain", rows: ["0000000","0000000","0000000","0000000","0000000","0089000","0033080"], nextDisc: 3, column: 1, latent: { 37: 4, 38: 1, 47: 6 } },
  { id: "rise", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0040000"], nextDisc: 5, column: 5, movesRemaining: 1, nextCoveredRow: [3, 1, 7, 2, 6, 4, 5] },
  { id: "rise-blocked", rows: ["0005000","0006000","0005000","0006000","0005000","0006000","0005000"], nextDisc: 4, column: 0, movesRemaining: 1, nextCoveredRow: [1, 2, 3, 4, 5, 6, 7] },
  { id: "board-clear", rows: ["0000000","0000000","0000000","0000000","0000000","0000000","0330000"], nextDisc: 3, column: 3 },
];

function boardOf(rows: string[]): Board {
  return rows.join("").split("").map(Number) as unknown as Board;
}

function collapse(animation: readonly MoveAnimationFrame[], start: Board, placedIndex: number): Frame[] {
  const frames: Frame[] = [{ kind: "start", board: serializeBoard(start), indexes: [], chainDepth: null }];
  let pendingWave: Frame | null = null;
  const flush = () => { if (pendingWave) { frames.push(pendingWave); pendingWave = null; } };
  for (const frame of animation) {
    if (frame.kind === "burst") {
      if (pendingWave && pendingWave.chainDepth === (frame.chainDepth ?? null)) {
        pendingWave.indexes.push(...frame.indexes);
      } else {
        flush();
        pendingWave = { kind: "wave", board: serializeBoard(frame.board), indexes: [...frame.indexes], chainDepth: frame.chainDepth ?? null };
      }
      continue;
    }
    flush();
    const kind = frame.kind === "drop" ? "drop" : frame.kind === "impact" ? "covers" : frame.kind === "settle" ? "gravity" : "rise";
    frames.push({ kind, board: serializeBoard(frame.board), indexes: [...frame.indexes], chainDepth: frame.chainDepth ?? null });
  }
  flush();
  if (frames.length === 1) frames.push({ kind: "drop", board: serializeBoard(start), indexes: [placedIndex], chainDepth: null });
  return frames;
}

const out: Record<string, unknown> = {};
for (const spec of SPECS) {
  const board = boardOf(spec.rows);
  const latent: LatentValues = new Array(49).fill(null);
  for (const [index, value] of Object.entries(spec.latent ?? {})) latent[Number(index)] = value;
  const state: GameState = {
    board, nextDisc: spec.nextDisc, score: 0, level: 1,
    movesRemaining: spec.movesRemaining ?? 5, movesPlayed: 0, gameOver: false,
  };
  const result = playMove(state, spec.column, () => 0.5, {
    captureAnimation: true,
    latent: { values: latent, nextCoveredRow: () => spec.nextCoveredRow ?? [1, 2, 3, 4, 5, 6, 7] },
  });
  if (!result) throw new Error(`scenario ${spec.id}: illegal move`);
  const placed = result.animation.find((f) => f.kind === "drop");
  const frames = collapse(result.animation, board, placed?.indexes[0] ?? -1);
  frames.push({ kind: "end", board: serializeBoard(result.state.board), indexes: [], chainDepth: null });
  out[spec.id] = {
    initial: { board: serializeBoard(board), nextDisc: spec.nextDisc, movesRemaining: state.movesRemaining, level: 1 },
    column: spec.column,
    frames,
    waves: result.waves,
    scoreDelta: result.scoreDelta,
    clearedBoard: result.clearedBoard,
    levelAdvanced: result.levelAdvanced,
    gameOver: result.state.gameOver,
    final: { board: serializeBoard(result.state.board), movesRemaining: result.state.movesRemaining, level: result.state.level, score: result.state.score },
  };
  console.log(spec.id, frames.map((f) => f.kind + (f.chainDepth ? f.chainDepth : "")).join(" → "), "| Δ", result.scoreDelta, result.state.gameOver ? "| GAME OVER" : "", result.levelAdvanced ? "| RISE" : "", result.clearedBoard ? "| CLEAR" : "");
}
const target = join(import.meta.dirname, "..", "content", "learn", "rules-scenarios.json");
writeFileSync(target, JSON.stringify(out, null, 1) + "\n");
console.log("wrote", target);
