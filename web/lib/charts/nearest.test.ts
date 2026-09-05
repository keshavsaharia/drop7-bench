import assert from "node:assert/strict";
import test from "node:test";
import { bandIndex, gridStep, keyStep, nearestIndex, nearestPoint, rectIndex, stepIndex } from "./nearest.ts";

test("nearest-x snapping: nearest wins, ties go to the lower index, distance is capped", () => {
  const xs = [10, 50, 90];
  assert.equal(nearestIndex(xs, 12), 0);
  assert.equal(nearestIndex(xs, 71), 2);
  assert.equal(nearestIndex(xs, 30), 0, "a tie resolves to the lower index");
  assert.equal(nearestIndex(xs, 300, 40), null);
  assert.equal(nearestIndex(xs, 130, 40), 2);
  assert.equal(nearestIndex([], 5), null);
});

test("nearest mark in the plane within a radius", () => {
  const points = [
    { x: 0, y: 0 },
    { x: 100, y: 0 },
    { x: 100, y: 100 },
  ];
  assert.equal(nearestPoint(points, { x: 90, y: 95 }, 24), 2);
  assert.equal(nearestPoint(points, { x: 50, y: 50 }, 24), null);
  assert.equal(nearestPoint(points, { x: 50, y: 50 }), 0, "ties resolve to the lower index");
});

test("row lookup for horizontal kinds: band edges map y to a row", () => {
  const edges = [0, 28, 56, 84];
  assert.equal(bandIndex(edges, 0), 0);
  assert.equal(bandIndex(edges, 27.9), 0);
  assert.equal(bandIndex(edges, 28), 1);
  assert.equal(bandIndex(edges, 84), 2, "the bottom edge belongs to the last row");
  assert.equal(bandIndex(edges, -1), null);
  assert.equal(bandIndex(edges, 85), null);
  assert.equal(bandIndex([5], 5), null);
});

test("rectangle hit test includes the padding gap", () => {
  const rects = [
    { x: 10, y: 10, w: 20, h: 40 },
    { x: 40, y: 10, w: 20, h: 40 },
  ];
  assert.equal(rectIndex(rects, { x: 15, y: 20 }), 0);
  assert.equal(rectIndex(rects, { x: 32, y: 20 }), null);
  assert.equal(rectIndex(rects, { x: 32, y: 20 }, 2), 0);
  assert.equal(rectIndex(rects, { x: 38, y: 20 }, 2), 1);
});

test("stepping from no cursor lands on an end; steps clamp", () => {
  assert.equal(stepIndex(null, 1, 5), 0);
  assert.equal(stepIndex(null, -1, 5), 4);
  assert.equal(stepIndex(4, 1, 5), 4);
  assert.equal(stepIndex(0, -1, 5), 0);
  assert.equal(stepIndex(2, 1, 5), 3);
  assert.equal(stepIndex(2, 1, 0), null);
});

test("keyStep: arrows on the chosen axis, Home/End, Escape clears, other keys pass through", () => {
  assert.equal(keyStep("ArrowRight", null, 4), 0);
  assert.equal(keyStep("ArrowLeft", null, 4), 3);
  assert.equal(keyStep("ArrowRight", 3, 4), 3);
  assert.equal(keyStep("Home", 3, 4), 0);
  assert.equal(keyStep("End", 0, 4), 3);
  assert.equal(keyStep("Escape", 2, 4), null);
  assert.equal(keyStep("ArrowUp", 2, 4), undefined, "the other pair is not handled on the x axis");
  assert.equal(keyStep("ArrowDown", 1, 4, "y"), 2);
  assert.equal(keyStep("ArrowRight", 1, 4, "y"), undefined);
  assert.equal(keyStep("a", 1, 4), undefined);
});

test("2-D cell cursor: rows and columns clamp, Home/End jump to row ends, Escape clears", () => {
  assert.deepEqual(gridStep("ArrowDown", null, 3, 4), { row: 0, col: 0 }, "the first arrow lands on the first cell");
  assert.deepEqual(gridStep("ArrowDown", { row: 0, col: 1 }, 3, 4), { row: 1, col: 1 });
  assert.deepEqual(gridStep("ArrowUp", { row: 0, col: 1 }, 3, 4), { row: 0, col: 1 });
  assert.deepEqual(gridStep("ArrowRight", { row: 2, col: 3 }, 3, 4), { row: 2, col: 3 });
  assert.deepEqual(gridStep("ArrowLeft", { row: 2, col: 3 }, 3, 4), { row: 2, col: 2 });
  assert.deepEqual(gridStep("Home", { row: 2, col: 3 }, 3, 4), { row: 2, col: 0 });
  assert.deepEqual(gridStep("End", { row: 1, col: 0 }, 3, 4), { row: 1, col: 3 });
  assert.equal(gridStep("Escape", { row: 1, col: 0 }, 3, 4), null);
  assert.equal(gridStep("Tab", { row: 1, col: 0 }, 3, 4), undefined);
  assert.deepEqual(gridStep("ArrowRight", { row: 0, col: 0 }, 3, 4, "col"), { row: 1, col: 0 }, "transposed: Left/Right walk the rows");
  assert.deepEqual(gridStep("ArrowDown", { row: 0, col: 0 }, 3, 4, "col"), { row: 0, col: 1 });
});
