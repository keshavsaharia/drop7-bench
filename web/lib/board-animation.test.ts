import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { applyGravity, findPoppers, type Board } from "../../src/core/typescript/engine.ts";
import { gravityTravel, type ChoiceLessonData } from "./board-animation.ts";

const data = JSON.parse(readFileSync(new URL("../content/learn/choice-lesson.json", import.meta.url), "utf8")) as ChoiceLessonData;

test("all teaching clips show complete simultaneous waves and account for every point", () => {
  const clips = data.columns.flatMap((column) => [column.move, ...column.replies.map((reply) => reply.move)]);
  for (const clip of clips) {
    let earned = 0;
    assert.equal(clip.frames[0].board, clip.initial);
    for (const frame of clip.frames) {
      assert.equal(frame.board.length, 49);
      if (frame.kind === "burst" || frame.kind === "match") {
        assert.deepEqual(frame.indexes, findPoppers([...frame.board].map(Number) as Board));
      }
      for (const run of frame.runs ?? []) {
        const step = run.end - run.start >= 7 ? 7 : 1;
        assert.equal((run.end - run.start) / step + 1, run.length);
        for (let index = run.start; index <= run.end; index += step) assert.notEqual(frame.board[index], "0");
        assert(frame.indexes.some((index) => index >= run.start && index <= run.end &&
          (index - run.start) % step === 0 && Number(frame.board[index]) === run.length));
      }
      if (frame.kind === "burst") earned += frame.points;
      assert.equal(frame.score, earned);
      for (const [destination, rows] of Object.entries(frame.travel)) {
        assert.notEqual(frame.board[Number(destination)], "0");
        assert(Number.isInteger(rows));
        assert(rows <= 0);
      }
    }
    assert.equal(earned, clip.points);
    assert.equal(clip.frames.at(-1)!.score, clip.points);
    assert.deepEqual(findPoppers([...clip.frames.at(-1)!.board].map(Number) as Board), []);
  }
});

test("gravity tracks duplicate discs in order through multiple gaps", () => {
  const board = Array(49).fill(0);
  board[0] = 2;
  board[14] = 2;
  board[35] = 4;
  const before = board.join("");
  const after = applyGravity(board as Board).join("");
  assert.deepEqual(gravityTravel(before, after), { 28: -4, 35: -3, 42: -1 });
});

test("the lesson distinguishes the immediate winner from the average winner", () => {
  const pick = (metric: (column: ChoiceLessonData["columns"][number]) => number) =>
    data.columns.reduce((best, column) => metric(column) > metric(best) ? column : best).column;
  assert.equal(pick((column) => column.move.points), 5);
  assert.equal(pick((column) => column.fair), 0);
  assert.equal(pick((column) => column.optimistic), 3);
  assert.equal(pick((column) => column.pessimistic), 5);
  for (const column of data.columns) {
    assert.equal(column.replies.length, 7);
    assert.equal(column.replyAverage, column.replies.reduce((sum, reply) => sum + reply.move.points, 0) / 7);
    assert.equal(column.fair, column.move.points + column.replyAverage);
  }
});
