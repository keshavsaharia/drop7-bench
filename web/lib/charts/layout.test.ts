import assert from "node:assert/strict";
import test from "node:test";
import { scaleLinear, scaleLog } from "d3-scale";
import { estimateText, formatIsoDate, parseIsoDate, rounded, tickValues, valueExtent, wrapText } from "./layout.ts";

const measure = (text: string, size: number) => estimateText(text, size);

test("wrapText wraps greedily, reports a word that cannot fit, and honours maxLines", () => {
  const short = wrapText("depth 4 five strata", 200, 11, measure);
  assert.deepEqual(short.lines, ["depth 4 five strata"]);
  assert.ok(short.fits);
  const wrapped = wrapText("depth 4 five strata frozen reference", 80, 11, measure);
  assert.ok(wrapped.lines.length > 1);
  assert.ok(wrapped.fits);
  assert.ok(wrapped.lines.every((line) => measure(line, 11) <= 80));
  const longWord = wrapText("supercalifragilisticexpialidocious", 40, 11, measure);
  assert.deepEqual(longWord.lines, ["supercalifragilisticexpialidocious"]);
  assert.equal(longWord.fits, false);
  const capped = wrapText("one two three four five six", 30, 11, measure, 2);
  assert.equal(capped.fits, false);
});

test("tickValues: small integer domains tick every integer, large ones step", () => {
  const scale = scaleLinear().domain([2, 5]).range([0, 100]);
  assert.deepEqual(tickValues(scale, 5, true), [2, 3, 4, 5]);
  const wide = scaleLinear().domain([0, 12]).range([0, 100]);
  assert.deepEqual(tickValues(wide, 4, true), [0, 3, 6, 9, 12]);
  const plain = scaleLinear().domain([0, 100]).range([0, 100]);
  assert.deepEqual(tickValues(plain, 5), [0, 20, 40, 60, 80, 100]);
});

test("tickValues: a log axis spanning three or more decades ticks only the decades", () => {
  const scale = scaleLog().domain([10, 100000]).range([0, 100]);
  assert.deepEqual(tickValues(scale, 10), [10, 100, 1000, 10000, 100000]);
  const narrow = scaleLog().domain([10, 500]).range([0, 100]);
  const ticks = tickValues(narrow, 10);
  assert.ok(ticks.every((v) => [1, 2, 5].includes(Math.round(v / 10 ** Math.floor(Math.log10(v))))));
  assert.ok(ticks.includes(100));
});

test("rounded() emits 1/100 px coordinates and is idempotent", () => {
  const scale = rounded(scaleLog().domain([1, 1000]).range([0, 617]));
  const twice = rounded(scale);
  for (const v of [1, 2.5, 7, 33, 333, 1000]) {
    const once = scale(v);
    assert.equal(Math.round(once * 100) / 100, once, `${v} is rounded to 0.01`);
    assert.equal(twice(v), once, `${v} unchanged by a second wrap`);
  }
  assert.deepEqual(scale.domain(), [1, 1000], "domain passes through");
  assert.deepEqual(twice.range(), [0, 617], "range passes through");
});

test("valueExtent widens a flat series and can force zero in", () => {
  assert.deepEqual(valueExtent([3, 9, -2]), [-2, 9]);
  assert.deepEqual(valueExtent([3, 9]), [3, 9]);
  assert.deepEqual(valueExtent([3, 9], true), [0, 9]);
  const flat = valueExtent([5, 5]);
  assert.ok(flat[0] < 5 && flat[1] > 5);
  assert.deepEqual(valueExtent([]), [0, 1]);
});

test("parseIsoDate accepts real calendar dates only and round-trips", () => {
  const ms = parseIsoDate("2026-08-20");
  assert.ok(ms !== null);
  assert.equal(formatIsoDate(ms as number), "2026-08-20");
  assert.equal(parseIsoDate("2026-02-30"), null);
  assert.equal(parseIsoDate("20260820"), null);
  assert.equal(parseIsoDate(42), null);
});
