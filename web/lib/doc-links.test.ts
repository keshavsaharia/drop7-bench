import assert from "node:assert/strict";
import test from "node:test";
import { resolveRepoRelative, rewriteRepoDocHref } from "./doc-links.ts";

const STATUS = "docs/research/status.md";

test("rewrites sibling and parent docs links from a rendered status page", () => {
  assert.equal(
    rewriteRepoDocHref("experiment-index.md", STATUS),
    "/docs/research/experiment-index",
  );
  assert.equal(rewriteRepoDocHref("history.md", STATUS), "/docs/research/history");
  assert.equal(rewriteRepoDocHref("../strategies.md", STATUS), "/docs/strategies");
  assert.equal(rewriteRepoDocHref("../benchmarks.md", STATUS), "/docs/benchmarks");
  assert.equal(rewriteRepoDocHref("roadmap.md", STATUS), "/docs/research/roadmap");
});

test("rewrites repo-root docs/foo.md hrefs without a from-path", () => {
  assert.equal(rewriteRepoDocHref("docs/methodology.md"), "/docs/methodology");
  assert.equal(
    rewriteRepoDocHref("docs/exploratory/finding-01-score-is-survival.md#s3"),
    "/docs/exploratory/finding-01-score-is-survival#s3",
  );
});

test("strips a leftover .md on an already-routed /docs URL", () => {
  assert.equal(rewriteRepoDocHref("/docs/benchmarks.md"), "/docs/benchmarks");
  assert.equal(rewriteRepoDocHref("/docs/research/status.md#bottom"), "/docs/research/status#bottom");
  assert.equal(rewriteRepoDocHref("/docs/methodology"), "/docs/methodology");
});

test("resolves approach README links into /docs slugs", () => {
  assert.equal(
    rewriteRepoDocHref(
      "../../../docs/exploratory/finding-08-learned-leaf.md",
      "approaches/lifetime-objective/learned-leaf/README.md",
    ),
    "/docs/exploratory/finding-08-learned-leaf",
  );
});

test("leaves src/ links untouched", () => {
  assert.equal(rewriteRepoDocHref("src/core/typescript/engine.ts"), "src/core/typescript/engine.ts");
  assert.equal(
    rewriteRepoDocHref("../../src/core/typescript/engine.ts", STATUS),
    "../../src/core/typescript/engine.ts",
  );
});

test("rewrites approach directories and research records onto console routes", () => {
  assert.equal(
    rewriteRepoDocHref("../approaches/fair-expectimax", "docs/strategies.md"),
    "/approaches/fair-expectimax",
  );
  assert.equal(
    rewriteRepoDocHref("../approaches/ntuple-rl/torch-ppo", "docs/strategies.md"),
    "/approaches/ntuple-rl/torch-ppo",
  );
  assert.equal(
    rewriteRepoDocHref(
      "../../research/theories/TH-20260820-distributional-afterstate-ranker-7aba7fb3.json",
      "docs/research/experiment-index.md",
    ),
    "/theories/TH-20260820-distributional-afterstate-ranker-7aba7fb3",
  );
  assert.equal(
    rewriteRepoDocHref(
      "../../research/experiments/EX-20260820-d4-toptwo-override-gate-0bdb39a1.json",
      "docs/research/experiment-index.md",
    ),
    "/experiments/EX-20260820-d4-toptwo-override-gate-0bdb39a1",
  );
});

test("leaves non-docs repository and external hrefs untouched", () => {
  assert.equal(rewriteRepoDocHref("../../research/README.md", STATUS), "/research");
  assert.equal(
    rewriteRepoDocHref("../../research/results/RS-20260820T094500Z-5c1e9a04.json", "docs/research/experiment-index.md"),
    "../../research/results/RS-20260820T094500Z-5c1e9a04.json",
  );
  assert.equal(
    rewriteRepoDocHref(
      "../../approaches/lifetime-objective/learned-leaf/PREREGISTRATION.md",
      "docs/exploratory/finding-08-learned-leaf.md",
    ),
    "../../approaches/lifetime-objective/learned-leaf/PREREGISTRATION.md",
  );
  assert.equal(rewriteRepoDocHref("https://example.com/docs/foo.md"), "https://example.com/docs/foo.md");
  assert.equal(rewriteRepoDocHref("#bottom-line"), "#bottom-line");
  assert.equal(rewriteRepoDocHref("/learn/rules"), "/learn/rules");
});

test("resolveRepoRelative normalizes parent segments", () => {
  assert.equal(resolveRepoRelative(STATUS, "../strategies.md"), "docs/strategies.md");
  assert.equal(resolveRepoRelative(STATUS, "../../research/README.md"), "research/README.md");
});
