import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { embedRefs, findOrphans, looksLikeSignedDelta, resolveSource, sourceCandidates, sourceHref } from "./provenance.ts";
import { validateFigureSpec } from "./spec.ts";

const figuresDir = new URL("../../content/figures/", import.meta.url);

test("resolveSource finds each id shape in its directory and returns null for a missing record", () => {
  const tree = new Set([
    "research/results/RS-20260821T205102Z-d89df4b5.json",
    "research/experiments/EX-20260902-nnue-evolution-d3-v2-49c18bc2.json",
    "research/theories/TH-20260825-evolved-nnue-leaf-d3-0f47e46c.json",
    "research/runs/RUN-20260902T035644Z-c1fd8987.json",
    "docs/methodology.md",
    "web/content/log/2026-08-20.mdx",
  ]);
  const exists = (path: string) => tree.has(path);
  assert.equal(resolveSource("RS-20260821T205102Z-d89df4b5", exists), "research/results/RS-20260821T205102Z-d89df4b5.json");
  assert.equal(resolveSource("EX-20260902-nnue-evolution-d3-v2-49c18bc2", exists), "research/experiments/EX-20260902-nnue-evolution-d3-v2-49c18bc2.json");
  assert.equal(resolveSource("TH-20260825-evolved-nnue-leaf-d3-0f47e46c", exists), "research/theories/TH-20260825-evolved-nnue-leaf-d3-0f47e46c.json");
  assert.equal(resolveSource("RUN-20260902T035644Z-c1fd8987", exists), "research/runs/RUN-20260902T035644Z-c1fd8987.json");
  assert.equal(resolveSource("docs/methodology.md", exists), "docs/methodology.md");
  assert.equal(resolveSource("web/content/log/2026-08-20.mdx", exists), "web/content/log/2026-08-20.mdx");
  assert.equal(resolveSource("RS-20260101T000000Z-deadbeef", exists), null, "a well-formed id with no file does not resolve");
  assert.equal(resolveSource("notes.txt", exists), null);
  assert.deepEqual(sourceCandidates("RS-x"), ["research/results/RS-x.json"]);
  assert.deepEqual(sourceCandidates("garbage"), []);
});

test("sourceHref routes what the console can open and returns null for ledger-only records", () => {
  assert.equal(sourceHref("EX-abc"), "/experiments/EX-abc");
  assert.equal(sourceHref("TH-abc"), "/theories/TH-abc");
  assert.equal(sourceHref("docs/research/status.md"), "/docs/research/status");
  assert.equal(sourceHref("web/content/log/2026-08-20.mdx"), "/log/2026-08-20");
  assert.equal(sourceHref("RS-abc"), null);
  assert.equal(sourceHref("RUN-abc"), null);
});

test("embedRefs finds fences in Markdown and tags in MDX and TSX", () => {
  const md = "text\n\n```figure score-vs-depth\ncaption: x\n```\n\n```figure\nmoves-vs-depth\n```\n\n```diagram\nchance-strata\n```\n";
  assert.deepEqual(
    embedRefs(md, "md").map((r) => [r.kind, r.name, r.form]),
    [
      ["figure", "score-vs-depth", "fence"],
      ["figure", "moves-vs-depth", "fence"],
      ["diagram", "chance-strata", "fence"],
    ],
  );
  const mdx = '<Figure name="learned-leaf-arms" caption="c" />\n<Diagram name="leaf-blend" />\n<FigureGrid names={["a-b", "c-d"]} columns={2} />\n<StatTile label="x" value="1" trend={{ figure: "reading-history", series: "mean" }} />\n<Sparkline figure="spark-1" />';
  assert.deepEqual(
    embedRefs(mdx, "mdx").map((r) => [r.kind, r.name]),
    [
      ["figure", "learned-leaf-arms"],
      ["diagram", "leaf-blend"],
      ["figure", "spark-1"],
      ["figure", "reading-history"],
      ["figure", "a-b"],
      ["figure", "c-d"],
    ],
  );
  const tsx = '<Figure\n  name="evidence-timeline"\n  caption="…"\n/>\n```figure not-a-fence-in-tsx```';
  assert.deepEqual(
    embedRefs(tsx, "tsx").map((r) => [r.kind, r.name, r.line]),
    [["figure", "evidence-timeline", 1]],
  );
  assert.deepEqual(embedRefs("<Figure name='x'/>", "md"), [], "Markdown does not scan tags");
});

test("findOrphans lists specs no page embeds, sorted", () => {
  assert.deepEqual(findOrphans(["b", "a", "c"], new Set(["b"])), ["a", "c"]);
  assert.deepEqual(findOrphans(["a"], new Set(["a"])), []);
});

test("looksLikeSignedDelta flags the paired-delta bar specs and not magnitude bars or lines", () => {
  const load = (name: string) => validateFigureSpec(JSON.parse(readFileSync(new URL(`${name}.json`, figuresDir), "utf8")), name);
  assert.equal(looksLikeSignedDelta(load("screen-deltas-with-bounds")), true);
  assert.equal(looksLikeSignedDelta(load("strata-5-vs-7")), true);
  assert.equal(looksLikeSignedDelta(load("failure-mode-census")), false);
  assert.equal(looksLikeSignedDelta(load("score-vs-depth")), false);
});
