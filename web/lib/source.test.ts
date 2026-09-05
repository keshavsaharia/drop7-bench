import assert from "node:assert/strict";
import test from "node:test";
import { getRepoSource, getRepoSourceTree, sourceLanguage } from "./repo.ts";

test("recognizes the repository's primary implementation languages", () => {
  assert.equal(sourceLanguage("src/core/typescript/engine.ts"), "typescript");
  assert.equal(sourceLanguage("approaches/example/policy/main.cpp"), "cpp");
  assert.equal(sourceLanguage("approaches/example/policy/engine.hpp"), "cpp");
  assert.equal(sourceLanguage("approaches/example/policy/search.rs"), "rust");
});

test("reads source files and source-only directory trees", () => {
  const source = getRepoSource("src/core/typescript/engine.ts");
  assert.equal(source?.kind, "file");
  if (source?.kind !== "file") return;
  assert.equal(source.language, "typescript");
  assert.ok(source.source.includes("export"));
  assert.ok(source.lines > 0);

  const tree = getRepoSourceTree("src");
  assert.equal(tree?.path, "src");
  assert.ok(tree?.children?.some((entry) => entry.name === "core"));
});

test("reads nested approach code through its natural repository path", () => {
  const source = getRepoSource(
    "approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp",
  );
  assert.equal(source?.kind, "file");
  if (source?.kind !== "file") return;
  assert.equal(source.language, "cpp");
  assert.equal(source.href, "/approach/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp");
});

test("rejects paths outside the two published source roots", () => {
  assert.equal(getRepoSource("src/../package.json"), null);
  assert.equal(getRepoSource("docs/methodology.md"), null);
  assert.equal(getRepoSource("/src/core/typescript/engine.ts"), null);
});
