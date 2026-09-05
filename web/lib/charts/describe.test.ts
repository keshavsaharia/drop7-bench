import assert from "node:assert/strict";
import test from "node:test";
import { ariaText, describeBin, describeCell, describeMarker, describePoint, seriesAt } from "./describe.ts";
import { validateFigureSpec } from "./spec.ts";

const RS = "RS-20260821T205102Z-d89df4b5";

test("a delta point reads value first, then bounds, floor, W-T-L, n and the note; the source is last", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "delta",
    y: { label: "paired delta", unit: "points" },
    series: [{ name: "d", sourceRecord: RS, sourceField: "metrics.pairedD4s7MinusD4s5", points: [{ x: "f05 confirm", y: 101171, lo: 47447, floor: 30957, wtl: [41, 0, 23], n: 64, label: "finding-05 confirmation" }] }],
  });
  const text = describePoint(spec, spec.series[0].points[0]);
  assert.equal(text.value, "+101,171 points");
  assert.equal(text.x, "f05 confirm");
  assert.deepEqual(text.details, ["95 % lower bound 47,447 points", "detection floor 30,957 points", "W-T-L 41-0-23", "n = 64 games", "finding-05 confirmation"]);
  assert.equal(text.source, `${RS} · metrics.pairedD4s7MinusD4s5`);
  assert.equal(ariaText("d", text), "+101,171 points, d, f05 confirm. 95 % lower bound 47,447 points. detection floor 30,957 points. W-T-L 41-0-23. n = 64 games. finding-05 confirmation");
});

test("a line point is unsigned with its x unit; two-sided bounds read as a range", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "line",
    x: { label: "depth", unit: "plies" },
    y: { label: "score", unit: "points" },
    series: [{ name: "a", sourceRecord: RS, points: [{ x: 4, y: 297327, lo: 250000, hi: 300000 }] }],
  });
  const text = describePoint(spec, spec.series[0].points[0]);
  assert.equal(text.value, "297,327 points");
  assert.equal(text.x, "4 plies");
  assert.deepEqual(text.details, ["95 % bounds 250,000 to 300,000 points"]);
  assert.equal(ariaText(null, text), "297,327 points, 4 plies. 95 % bounds 250,000 to 300,000 points");
});

test("bins, cells and markers describe themselves with the recorded unit", () => {
  const hist = validateFigureSpec({ title: "t", kind: "histogram", y: { label: "share of all waves", unit: "%" }, bins: [{ series: "fair D4", sourceRecord: RS, bins: [{ label: "depth 1", share: 42.36, note: "1,374 waves observed" }] }] });
  const bin = describeBin(hist, hist.bins![0].bins[0]);
  assert.equal(bin.value, "42.36 %");
  assert.deepEqual(bin.details, ["1,374 waves observed"]);
  const heat = validateFigureSpec({ title: "t", kind: "heatmap", y: { label: "occupancy" }, rows: ["r1"], cols: ["c1"], cells: [{ row: "r1", col: "c1", value: 0.5, n: 64, sourceRecord: RS }] });
  const cell = describeCell(heat, heat.cells![0]);
  assert.equal(cell.value, "0.5");
  assert.equal(cell.x, "r1, c1");
  assert.deepEqual(cell.details, ["n = 64"]);
  const withMarker = validateFigureSpec({ title: "t", kind: "strip", y: { label: "score", unit: "points" }, markers: [{ value: 1000000, label: "one-million target", sourceRecord: "docs/methodology.md" }], series: [{ name: "d4", sourceRecord: RS, points: [{ x: 1, y: 5 }] }] });
  const marker = describeMarker(withMarker, withMarker.markers![0]);
  assert.equal(marker.value, "1,000,000 points");
  assert.equal(marker.x, "one-million target");
  assert.equal(marker.source, "docs/methodology.md");
});

test("seriesAt lists every series that has a point at that x, in series order", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "line",
    series: [
      { name: "a", sourceRecord: RS, points: [{ x: 3, y: 1 }, { x: 4, y: 2 }] },
      { name: "b", sourceRecord: RS, points: [{ x: 4, y: 3 }] },
    ],
  });
  assert.deepEqual(
    seriesAt(spec, 4).map((s) => [s.series.name, s.point.y]),
    [
      ["a", 2],
      ["b", 3],
    ],
  );
  assert.deepEqual(seriesAt(spec, 3).map((s) => s.series.name), ["a"]);
});
