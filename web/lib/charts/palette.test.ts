import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { contrastRatio, oklch } from "./color.ts";
import { CHART_TOKENS, DEEMPHASIS_HEX, MAX_SERIES, SEQUENTIAL_HEX, SERIES_HEX, STATUS_HEX, SURFACE_HEX, parseCssTokens, seriesColor, seriesToken, sequentialToken } from "./palette.ts";

const css = readFileSync(new URL("../../components/charts/charts.css", import.meta.url), "utf8");

test("the :root block in charts.css and the TypeScript token table agree", () => {
  const rootBlock = /:root\s*\{([^}]*)\}/.exec(css);
  assert.ok(rootBlock, "charts.css has a :root block");
  const declared = parseCssTokens(rootBlock[1]);
  for (const [name, hex] of Object.entries(CHART_TOKENS)) {
    assert.equal(declared[name], hex.toLowerCase(), `${name} in charts.css matches palette.ts`);
  }
  for (const name of Object.keys(declared)) {
    assert.ok(name in CHART_TOKENS, `${name} in charts.css is listed in palette.ts`);
  }
});

test("charts.css carries no hex literal outside the :root token block", () => {
  const withoutRoot = css.replace(/:root\s*\{[^}]*\}/, "");
  const withoutComments = withoutRoot.replace(/\/\*[\s\S]*?\*\//g, "");
  assert.deepEqual(withoutComments.match(/#[0-9a-fA-F]{6}\b/g) ?? [], []);
});

test("colour math sanity: black on white is 21:1", () => {
  assert.ok(Math.abs(contrastRatio("#ffffff", "#000000") - 21) < 1e-9);
});

test("every series slot sits in the dark lightness band, clears the chroma floor and 3:1 on both surfaces", () => {
  assert.equal(SERIES_HEX.length, 8);
  for (const hex of SERIES_HEX) {
    const { l, c } = oklch(hex);
    assert.ok(l >= 0.48 && l <= 0.67, `${hex} OKLCH L ${l.toFixed(3)} inside 0.48..0.67`);
    assert.ok(c >= 0.1, `${hex} chroma ${c.toFixed(3)} >= 0.10`);
    for (const surface of Object.values(SURFACE_HEX)) {
      assert.ok(contrastRatio(hex, surface) >= 3, `${hex} on ${surface} >= 3:1`);
    }
  }
});

test("status steps are >= 3:1 on both surfaces", () => {
  for (const [name, hex] of Object.entries(STATUS_HEX)) {
    for (const surface of Object.values(SURFACE_HEX)) {
      assert.ok(contrastRatio(hex, surface) >= 3, `status ${name} ${hex} on ${surface} >= 3:1`);
    }
  }
});

test("the sequential ramp is monotone with visible steps and a darkest step above 2:1", () => {
  const ls = SEQUENTIAL_HEX.map((hex) => oklch(hex).l);
  for (let i = 1; i < ls.length; i += 1) {
    assert.ok(ls[i] > ls[i - 1], `step ${i} is lighter than step ${i - 1}`);
    assert.ok(ls[i] - ls[i - 1] >= 0.06, `step ${i} delta L ${(ls[i] - ls[i - 1]).toFixed(3)} >= 0.06`);
  }
  assert.ok(contrastRatio(SEQUENTIAL_HEX[0], SURFACE_HEX.surface) >= 2, "darkest step >= 2:1 on the card");
  const hues = SEQUENTIAL_HEX.map((hex) => oklch(hex).h);
  assert.ok(Math.max(...hues) - Math.min(...hues) <= 40, "one hue");
});

test("the de-emphasis grey recedes below 3:1 by design", () => {
  assert.ok(contrastRatio(DEEMPHASIS_HEX, SURFACE_HEX.surface) < 3);
});

test("seriesColor and seriesToken use fixed slots and throw past slot 8 instead of cycling", () => {
  assert.equal(seriesColor(0), "#3987e5");
  assert.equal(seriesColor(7), "#e66767");
  assert.equal(seriesToken(0), "var(--color-series-1)");
  assert.equal(seriesToken(7), "var(--color-series-8)");
  assert.throws(() => seriesColor(MAX_SERIES), /never cycles/);
  assert.throws(() => seriesToken(8), /never cycles/);
  assert.throws(() => seriesColor(-1));
  assert.throws(() => seriesColor(1.5));
  assert.equal(sequentialToken(0), "var(--chart-seq-1)");
  assert.throws(() => sequentialToken(5));
});
