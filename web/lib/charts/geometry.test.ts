import assert from "node:assert/strict";
import test from "node:test";
import { barPath, bucketLevels, groupLayout, inCellInk, placeEndLabels, rowBands, sequentialStep, stackSegments, timeTicks } from "./geometry.ts";

test("barPath rounds only the data end and shrinks the radius on thin bars", () => {
  const up = barPath(10, 20, 20, 50, 4, "top");
  assert.ok(up.startsWith("M10,70 V24 Q10,20 14,20 H26 Q30,20 30,24 V70 Z"), up);
  const right = barPath(0, 0, 50, 20, 4, "right");
  assert.match(right, /Q50,0 50,4/);
  assert.match(right, /H0 Z$/, "the baseline end is square");
  const thin = barPath(0, 0, 4, 40, 4, "top");
  assert.match(thin, /Q0,0 2,0/, "a 4 px bar uses a 2 px radius");
  assert.equal(barPath(0, 0, 0, 40, 4, "top"), "");
  const flat = barPath(0, 0, 20, 2, 4, "top");
  assert.ok(!flat.includes("-"), "the radius never exceeds the bar's length, so no coordinate leaves the bar");
});

test("groupLayout centres capped bars with a 2 px gap", () => {
  const one = groupLayout(1, 100);
  assert.equal(one.thick, 24, "capped at 24 px");
  assert.deepEqual(one.offsets, [-12]);
  const three = groupLayout(3, 60, 24, 2);
  assert.ok(three.thick < 24);
  assert.equal(three.offsets.length, 3);
  const span = three.offsets[2] + three.thick - three.offsets[0];
  assert.ok(Math.abs(span - (3 * three.thick + 4)) < 0.05, "bars plus two gaps fill the group");
  assert.ok(Math.abs(three.offsets[0] + span / 2) < 0.05, "the group is centred on zero");
  assert.deepEqual(groupLayout(0, 100), { thick: 0, offsets: [] });
});

test("stackSegments returns cumulative fractions of the whole, as recorded", () => {
  assert.deepEqual(stackSegments([60, 40], 100), [
    [0, 0.6],
    [0.6, 1],
  ]);
  const short = stackSegments([50, 40], 100);
  assert.equal(short[1][1], 0.9, "a row that does not sum to the whole is left short, not scaled");
});

test("bucketLevels stacks marks that share a pixel bucket in input order", () => {
  assert.deepEqual(bucketLevels([3, 5, 12, 4, 30], 7), [0, 1, 0, 2, 0]);
  assert.deepEqual(bucketLevels([], 7), []);
});

test("placeEndLabels keeps the first label and drops colliders instead of stacking them", () => {
  assert.deepEqual(placeEndLabels([100, 106, 130, 128], 12), [true, false, true, false]);
  assert.deepEqual(placeEndLabels([10, 40], 12), [true, true]);
});

test("timeTicks steps by day, then 2 or 7 days, then months, always inside the domain", () => {
  const d = (s: string) => Date.UTC(Number(s.slice(0, 4)), Number(s.slice(5, 7)) - 1, Number(s.slice(8, 10)));
  const four = timeTicks(d("2026-08-20"), d("2026-08-23"), 6);
  assert.equal(four.length, 4);
  assert.equal(four[0], d("2026-08-20"));
  assert.equal(four[3], d("2026-08-23"));
  const month = timeTicks(d("2026-08-01"), d("2026-08-31"), 6);
  assert.ok(month.length <= 7);
  assert.ok(month.every((t) => t >= d("2026-08-01") && t <= d("2026-08-31")));
  const year = timeTicks(d("2026-01-15"), d("2026-12-15"), 6);
  assert.ok(year.every((t) => new Date(t).getUTCDate() === 1), "month starts");
  assert.ok(year.length >= 4 && year.length <= 7);
  assert.deepEqual(timeTicks(5, 5, 4), [5]);
});

test("rowBands accumulates tops and edges; sequentialStep clamps; inCellInk is light on the darkest two steps", () => {
  assert.deepEqual(rowBands(10, [20, 30]), { tops: [10, 30], edges: [10, 30, 60] });
  assert.equal(sequentialStep(0, 0, 10, 5), 0);
  assert.equal(sequentialStep(10, 0, 10, 5), 4);
  assert.equal(sequentialStep(5, 0, 10, 5), 2);
  assert.equal(sequentialStep(3, 3, 3, 5), 0);
  assert.equal(inCellInk(0), "light");
  assert.equal(inCellInk(1), "light");
  assert.equal(inCellInk(2), "dark");
});
