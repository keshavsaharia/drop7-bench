import assert from "node:assert/strict";
import test from "node:test";
import { fillTimeBuckets } from "./timeline.ts";
const empty = (bucket: string) => ({ bucket, views: 0 });
test("missing days are zero-filled and recorded values stay unchanged", () => {
  const points = fillTimeBuckets(
    [{ bucket: "2026-08-21 00:00:00.000", views: 9 }],
    "7d",
    new Date("2026-08-23T12:00:00Z"),
    empty,
  );
  assert.equal(points.length, 8);
  assert.equal(points[0].bucket, "2026-08-16T00:00:00.000Z");
  assert.equal(points.at(-1)!.bucket, "2026-08-23T00:00:00.000Z");
  assert.equal(
    points.reduce((sum, p) => sum + p.views, 0),
    9,
  );
  assert.equal(points[5].views, 9);
});
test("UTC hour boundaries exclude the end and week boundaries start Monday", () => {
  assert.equal(
    fillTimeBuckets([], "24h", new Date("2026-08-23T12:00:00Z"), empty).length,
    24,
  );
  const weekly = fillTimeBuckets(
    [],
    "90d",
    new Date("2026-08-23T12:00:00Z"),
    empty,
  );
  assert.ok(weekly.every((p) => new Date(p.bucket).getUTCDay() === 1));
});
