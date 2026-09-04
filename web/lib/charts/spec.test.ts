import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import test from "node:test";
import { KINDS, formatCompact, formatSigned, formatValue, isSignedKind, resolveProvenance, sourceRecords, specTable, specWarnings, validateFigureSpec } from "./spec.ts";

const figuresDir = new URL("../../content/figures/", import.meta.url);
const RS = "RS-20260821T205102Z-d89df4b5";

function shipped(): { name: string; raw: unknown }[] {
  return readdirSync(figuresDir)
    .filter((f) => f.endsWith(".json"))
    .map((f) => ({ name: f, raw: JSON.parse(readFileSync(new URL(f, figuresDir), "utf8")) }));
}

test("every shipped spec still validates and comes back unchanged", () => {
  const specs = shipped();
  assert.ok(specs.length >= 30, `found ${specs.length} specs`);
  for (const { name, raw } of specs) {
    const validated = validateFigureSpec(raw, name);
    assert.deepEqual(validated, raw, `${name} is returned exactly as written`);
    assert.ok(KINDS.includes(validated.kind));
    assert.ok(validated.series.length > 0);
    assert.ok(validated.series.every((s) => s.points.every((p) => typeof p.sourceRecord === "string")));
  }
});

test("series-level provenance is written onto points that omit theirs, with the field", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "paired",
    series: [
      {
        name: "delta",
        sourceRecord: RS,
        sourceField: "perSeed",
        points: [
          { x: 1, y: 5 },
          { x: 2, y: -3, sourceRecord: "docs/methodology.md", sourceField: "table 2" },
        ],
      },
    ],
  });
  assert.equal(spec.series[0].points[0].sourceRecord, RS);
  assert.equal(spec.series[0].points[0].sourceField, "perSeed");
  assert.equal(spec.series[0].points[1].sourceRecord, "docs/methodology.md");
  assert.equal(spec.series[0].points[1].sourceField, "table 2");
  assert.deepEqual(
    resolveProvenance(spec).map((p) => p.record),
    [RS, "docs/methodology.md"],
  );
  assert.deepEqual(sourceRecords(spec), [RS, "docs/methodology.md"]);
});

test("a point that resolves to no record is refused with the provenance message", () => {
  assert.throws(
    () => validateFigureSpec({ title: "t", kind: "line", series: [{ name: "a", points: [{ x: 1, y: 2 }] }] }, "fig"),
    /fig: series a, point x=1: sourceRecord must be a research record ID .* refusing to render a point without provenance/,
  );
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", series: [{ name: "a", sourceRecord: "notes.txt", points: [{ x: 1, y: 2 }] }] }), /series a: sourceRecord must be/);
});

test("a histogram carries pre-binned counts or shares and refuses raw series", () => {
  assert.throws(
    () => validateFigureSpec({ title: "t", kind: "histogram", series: [{ name: "raw", sourceRecord: RS, points: [{ x: "a", y: 1 }] }] }),
    /the chart never bins/,
  );
  const spec = validateFigureSpec({
    title: "t",
    kind: "histogram",
    y: { label: "share of waves", unit: "%" },
    bins: [
      {
        series: "points maximiser",
        sourceRecord: RS,
        bins: [
          { label: "1-3", lo: 1, hi: 3, share: 42.36 },
          { label: "4-6", lo: 4, hi: 6, share: 30.1, sourceRecord: "docs/methodology.md" },
        ],
      },
    ],
  });
  assert.deepEqual(spec.series, []);
  assert.equal(spec.bins?.[0].bins[0].sourceRecord, RS);
  assert.equal(spec.bins?.[0].bins[1].sourceRecord, "docs/methodology.md");
  assert.throws(() => validateFigureSpec({ title: "t", kind: "histogram", bins: [{ series: "a", sourceRecord: RS, bins: [{ label: "x", count: 1 }, { label: "y", share: 0.5 }] }] }), /mixes count and share/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "histogram", bins: [{ series: "a", sourceRecord: RS, bins: [{ label: "x", count: 1, share: 1 }] }] }), /exactly one of count or share/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "histogram", bins: [{ series: "a", sourceRecord: RS, bins: [{ label: "x", count: 1 }, { label: "x", count: 2 }] }] }), /duplicate bin label/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "histogram", bins: [{ series: "a", bins: [{ label: "x", count: 1 }] }] }), /without provenance/);
  const table = specTable(spec);
  assert.deepEqual(
    table.columns.map((c) => c.label),
    ["Series", "Bin", "Range", "share of waves", "Source"],
  );
  assert.equal(table.rows[0][3].text, "42.36 %");
});

test("a heatmap refuses cells outside its declared rows and columns and duplicate cells", () => {
  const base = { title: "t", kind: "heatmap", y: { label: "occupancy" }, rows: ["r1", "r2"], cols: ["c1", "c2"] };
  const ok = validateFigureSpec({ ...base, cells: [{ row: "r1", col: "c2", value: 0.5, sourceRecord: RS }] });
  assert.deepEqual(ok.series, []);
  assert.equal(ok.cells?.length, 1);
  assert.throws(() => validateFigureSpec({ ...base, cells: [{ row: "r9", col: "c1", value: 1, sourceRecord: RS }] }), /row r9 is not in rows/);
  assert.throws(() => validateFigureSpec({ ...base, cells: [{ row: "r1", col: "c1", value: 1, sourceRecord: RS }, { row: "r1", col: "c1", value: 2, sourceRecord: RS }] }), /duplicate cell/);
  assert.throws(() => validateFigureSpec({ ...base, cells: [{ row: "r1", col: "c1", value: 1 }] }), /without provenance/);
  assert.deepEqual(
    specTable(ok).columns.map((c) => c.label),
    ["Row", "Column", "occupancy", "n", "Source"],
  );
});

test("stacked rows whose recorded shares do not sum to the whole are reported, not fixed", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "stacked",
    y: { label: "share", unit: "%" },
    series: [
      { name: "clears", sourceRecord: RS, points: [{ x: "policy A", y: 60 }, { x: "policy B", y: 50 }] },
      { name: "reveals", sourceRecord: RS, points: [{ x: "policy A", y: 40 }, { x: "policy B", y: 40 }] },
    ],
  });
  const warnings = specWarnings(spec);
  assert.equal(warnings.length, 1);
  assert.match(warnings[0], /policy B.*sums to 90/);
  assert.equal(spec.series[1].points[1].y, 40, "the recorded share is untouched");
});

test("a time axis needs parseable ISO dates", () => {
  const ok = validateFigureSpec({
    title: "t",
    kind: "bar",
    x: { label: "day", scale: "time" },
    series: [{ name: "negative", sourceRecord: "web/content/log/2026-08-20.mdx", points: [{ x: "2026-08-20", y: 5 }, { x: "2026-08-21", y: 7 }] }],
  });
  assert.equal(ok.x?.scale, "time");
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", x: { label: "day", scale: "time" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: "2026-02-30", y: 1 }] }] }), /ISO dates/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", x: { label: "day", scale: "time" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: 3, y: 1 }] }] }), /ISO dates/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "forest", x: { label: "day", scale: "time" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: "2026-08-20", y: 1 }] }] }), /time x axis is not supported/);
});

test("a log x axis needs positive numeric x on a line", () => {
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", x: { label: "w", scale: "log" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: 0, y: 1 }] }] }), /positive x/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "bar", x: { label: "w", scale: "log" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: "a", y: 1 }] }] }), /log x axis needs numeric x/);
});

test("delta and forest rows are categories with a non-negative floor; paired has one series", () => {
  const delta = validateFigureSpec({ title: "t", kind: "delta", series: [{ name: "d", sourceRecord: RS, points: [{ x: "f05", y: 101171, lo: 47447, floor: 30957, wtl: [41, 0, 23], n: 64 }] }] });
  assert.equal(delta.series[0].points[0].floor, 30957);
  assert.ok(isSignedKind(delta));
  assert.throws(() => validateFigureSpec({ title: "t", kind: "delta", series: [{ name: "d", sourceRecord: RS, points: [{ x: 3, y: 1 }] }] }), /string x category/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "forest", series: [{ name: "d", sourceRecord: RS, points: [{ x: "a", y: 1, floor: -1 }] }] }), /floor must be >= 0/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "forest", series: [{ name: "d", sourceRecord: RS, points: [{ x: "a", y: 1, wtl: [1, 2] }] }] }), /wtl/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "paired", series: [{ name: "a", sourceRecord: RS, points: [{ x: 1, y: 1 }] }, { name: "b", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] }), /exactly one series/);
  const table = specTable(delta);
  assert.deepEqual(
    table.columns.map((c) => c.label),
    ["Series", "x", "y", "Bounds", "Floor", "W-T-L", "n", "Source"],
  );
  assert.equal(table.rows[0][2].text, "+101,171");
  assert.equal(table.rows[0][5].text, "41-0-23");
});

test("strip points carry the game's value in y, a censored flag, and string or numeric x", () => {
  const spec = validateFigureSpec({
    title: "t",
    kind: "strip",
    x: { label: "game" },
    y: { label: "score", unit: "points" },
    markers: [{ value: 1000000, label: "one-million target", sourceRecord: "docs/methodology.md" }],
    series: [{ name: "d4", sourceRecord: RS, points: [{ x: "0xa52e1300", y: 123456 }, { x: 2, y: 654321, censored: true }] }],
  });
  assert.equal(spec.series[0].points[1].censored, true);
  assert.equal(resolveProvenance(spec).length, 3);
  const table = specTable(spec);
  assert.equal(table.rows[1][1].note, "censored");
  assert.equal(table.rows[2][0].text, "marker: one-million target");
  assert.throws(() => validateFigureSpec({ title: "t", kind: "strip", series: [{ name: "d4", sourceRecord: RS, points: [{ x: "a" }] }] }), /y must be a finite number/);
});

test("evidence words outside the closed vocabulary are refused; valid + fail is accepted", () => {
  const ok = validateFigureSpec({ title: "t", kind: "line", evidence: { validity: "valid", outcome: "fail", tier: "development", cohort: "64 paired games" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] });
  assert.equal(ok.evidence?.outcome, "fail");
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", evidence: { tier: "exploratory · engineering result" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] }), /outside the closed vocabulary/);
});

test("colorBy disc needs disc categories; more than eight coloured series is refused; unknown role refused", () => {
  assert.throws(() => validateFigureSpec({ title: "t", kind: "bar", colorBy: "disc", series: [{ name: "a", sourceRecord: RS, points: [{ x: "gray", y: 1 }] }] }), /colorBy "disc"/);
  const nine = Array.from({ length: 9 }, (_, i) => ({ name: `s${i}`, sourceRecord: RS, points: [{ x: 1, y: i }] }));
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", series: nine }), /eight fixed palette slots/);
  const eightPlusContext = [...nine.slice(0, 8), { ...nine[8], role: "context" }];
  assert.equal(validateFigureSpec({ title: "t", kind: "line", series: eightPlusContext }).series.length, 9);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", series: [{ name: "a", role: "hero", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] }), /unknown role/);
});

test("markers need a recorded source and a label", () => {
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", markers: [{ value: 1, label: "one" }], series: [{ name: "a", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] }), /marker 0 \(one\): sourceRecord/);
  assert.throws(() => validateFigureSpec({ title: "t", kind: "line", markers: [{ value: 1, sourceRecord: RS }], series: [{ name: "a", sourceRecord: RS, points: [{ x: 1, y: 1 }] }] }), /needs a label/);
});

test("scatter-like kinds with more than three series warn unless faceted", () => {
  const four = Array.from({ length: 4 }, (_, i) => ({ name: `s${i}`, sourceRecord: RS, points: [{ x: "row", y: i }] }));
  assert.equal(specWarnings(validateFigureSpec({ title: "t", kind: "dot", series: four })).length, 1);
  assert.equal(specWarnings(validateFigureSpec({ title: "t", kind: "dot", facet: "series", series: four })).length, 0);
});

test("specTable columns for the series kinds, and signed formatting for signed bars", () => {
  const line = validateFigureSpec({ title: "t", kind: "line", x: { label: "depth", unit: "plies" }, y: { label: "score", unit: "points" }, series: [{ name: "a", sourceRecord: RS, points: [{ x: 4, y: 297327, n: 64, label: "frozen" }] }] });
  const table = specTable(line);
  assert.deepEqual(
    table.columns.map((c) => c.label),
    ["Series", "depth", "score", "Bounds", "n", "Source"],
  );
  assert.deepEqual(table.rows[0].map((c) => c.text), ["a", "4 plies", "297,327 points", "—", "64", RS]);
  assert.equal(table.rows[0][1].note, "frozen");
  const bar = validateFigureSpec({ title: "t", kind: "bar", series: [{ name: "a", sourceRecord: RS, points: [{ x: "c", y: 5, lo: -2 }] }] });
  assert.ok(isSignedKind(bar));
  assert.equal(specTable(bar).rows[0][2].text, "+5");
  assert.equal(specTable(bar).rows[0][3].text, "lower -2");
});

test("formatters: full precision for quoted values, compact only for ticks", () => {
  assert.equal(formatValue(297327, "points"), "297,327 points");
  assert.equal(formatValue(0.3422), "0.34");
  assert.equal(formatSigned(101171), "+101,171");
  assert.equal(formatSigned(-41950, "points"), "-41,950 points");
  assert.equal(formatCompact(1200000), "1.2M");
  assert.equal(formatCompact(350000), "350k");
  assert.equal(formatCompact(0.75), "0.75");
  assert.equal(formatCompact(-41950), "-42k");
  assert.equal(formatCompact(12000000), "12M");
  assert.equal(formatCompact(250), "250");
  assert.equal(formatCompact(3.5), "3.5");
});
