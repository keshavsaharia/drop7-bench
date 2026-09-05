/** Add animation to the existing teaching position, without searching for a new one.
 * Run: node --experimental-strip-types web/scripts/generate-choice-lesson.ts
 * Verify retained output: append --check. No research cohorts are accessed.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import assert from "node:assert/strict";
import { playMove, serializeBoard, type GameState, type DiscValue } from "../../src/core/typescript/engine.ts";
import { gravityTravel, type BoardClip, type BoardFrame, type BoardRun, type ChoiceLessonData } from "../lib/board-animation.ts";

const original = JSON.parse(readFileSync(new URL("../content/learn/concept-scenarios.json", import.meta.url), "utf8")).tree;
const root: GameState = {
  board: [...original.board].map(Number), nextDisc: original.nextDisc,
  score: 0, level: 1, movesRemaining: 4, movesPlayed: 0, gameOver: false,
};

function matchingRuns(board: string, indexes: number[]): BoardRun[] {
  const runs = new Map<string, BoardRun>();
  for (const index of indexes) {
    for (const step of [1, 7]) {
      let start = index;
      let end = index;
      const valid = (cell: number) => cell >= 0 && cell < 49 && board[cell] !== "0" &&
        (step === 7 || Math.floor(cell / 7) === Math.floor(index / 7));
      while (valid(start - step)) start -= step;
      while (valid(end + step)) end += step;
      const length = (end - start) / step + 1;
      if (length === Number(board[index])) runs.set(`${start}-${end}`, { start, end, length });
    }
  }
  return [...runs.values()];
}

function clip(state: GameState, column: number): { move: BoardClip; state: GameState } {
  const result = playMove(state, column, () => 0.5, { captureAnimation: true });
  assert(result, "Every illustrated move must be legal");
  assert(result.waves.every((wave) => wave.revealed === 0), "This lesson assumes no hidden reveals");
  assert(!result.levelAdvanced && !result.clearedBoard, "This lesson isolates clear points from bonuses");
  const initial = serializeBoard(state.board);
  const frames: BoardFrame[] = [];
  let score = 0;
  let previous = initial;
  const add = (kind: BoardFrame["kind"], board: string, indexes: number[], label: string,
    depth?: number, points = 0, travel: Record<number, number> = {}) => {
    frames.push({ kind, board, indexes, label, score, points, travel, ...(depth ? { depth } : {}),
      ...(kind === "match" ? { runs: matchingRuns(board, indexes) } : {}) });
    previous = board;
  };
  add("ready", initial, [], `Drop the ${state.nextDisc} in column ${column + 1}.`);
  for (let i = 0; i < result.animation.length; i += 1) {
    const frame = result.animation[i];
    const board = serializeBoard(frame.board);
    if (frame.kind === "burst") {
      const indexes = [...frame.indexes];
      while (result.animation[i + 1]?.kind === "burst" && result.animation[i + 1]?.chainDepth === frame.chainDepth) {
        indexes.push(...result.animation[++i].indexes);
      }
      const wave = result.waves.find((entry) => entry.depth === frame.chainDepth)!;
      const discs = indexes.map((index) => board[index]).join(" and ");
      add("match", board, indexes, `The ${discs} ${indexes.length === 1 ? "matches" : "match"}.`, frame.chainDepth);
      score += wave.points;
      add("burst", board, indexes, `Wave ${frame.chainDepth}: +${wave.points} points.`, frame.chainDepth, wave.points);
    } else if (frame.kind === "drop") {
      const index = frame.indexes[0];
      add("drop", board, [...frame.indexes], `The ${state.nextDisc} lands.`, undefined, 0, { [index]: -(Math.floor(index / 7) + 1) });
    } else if (frame.kind === "settle") {
      add("settle", board, [...frame.indexes], "The discs above fall into the gaps.", frame.chainDepth, 0, gravityTravel(previous, board));
    } else {
      add(frame.kind, board, [...frame.indexes], frame.indexes.length ? "Nearby gray discs take a hit." : "The cleared discs leave a gap.", frame.chainDepth);
    }
  }
  assert.equal(score, result.scoreDelta);
  add("done", serializeBoard(result.state.board), [], `The move earns ${score} points.`);
  // A generous reading pause before a repeated clip restarts.
  frames.push({ ...frames[frames.length - 1] });
  return { move: { initial, nextDisc: state.nextDisc, column, points: result.scoreDelta,
    waves: result.waves.map(({ depth, cleared, points }) => ({ depth, cleared, points })), frames }, state: result.state };
}

const data: ChoiceLessonData = {
  source: "web/scripts/generate-choice-lesson.ts (TypeScript engine; existing concept-scenarios.json teaching position)",
  board: original.board, nextDisc: original.nextDisc,
  columns: original.columns.filter((column: { legal: boolean }) => column.legal).map((column: {
    column: number; points: number; board: string; fair: number; optimistic: number; pessimistic: number;
    branches: { disc: number; best: { column: number; board: string; points: number } }[];
  }) => {
    const played = clip(root, column.column);
    assert.equal(played.move.points, column.points);
    assert.equal(serializeBoard(played.state.board), column.board);
    const replies = column.branches.map((branch) => {
      const reply = clip({ ...played.state, nextDisc: branch.disc as DiscValue, score: 0 }, branch.best.column);
      assert.equal(reply.move.points, branch.best.points);
      assert.equal(serializeBoard(reply.state.board), branch.best.board);
      return { disc: branch.disc, move: reply.move };
    });
    const replyAverage = replies.reduce((sum, reply) => sum + reply.move.points, 0) / replies.length;
    assert.equal(played.move.points + replyAverage, column.fair);
    return { column: column.column, move: played.move, replies, replyAverage,
      fair: column.fair, optimistic: column.optimistic, pessimistic: column.pessimistic };
  }),
};
const target = new URL("../content/learn/choice-lesson.json", import.meta.url);
const output = JSON.stringify(data, null, 2) + "\n";
if (process.argv.includes("--check")) {
  assert.equal(readFileSync(target, "utf8"), output, "Regenerate the lesson animation data");
  console.log("Lesson verified: all 7 drops and 49 best replies match the retained scenario and engine.");
} else {
  writeFileSync(target, output);
  console.log(`Wrote ${fileURLToPath(target)}`);
}
