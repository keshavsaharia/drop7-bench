import assert from "node:assert/strict";
import test from "node:test";
import { approachRefsInRecord, approachRefsInText } from "./records.ts";

test("record entry points link their plural repository directory", () => {
  assert.deepEqual(
    approachRefsInRecord({
      candidate: {
        entryPoint: "approaches/fair-expectimax/rust-engine/src/search.rs",
      },
    }),
    [{ family: "fair-expectimax", slug: "rust-engine" }],
  );
});

test("canonical URLs and repository paths identify the same approach once", () => {
  assert.deepEqual(
    approachRefsInText([
      "/approach/fair-expectimax/rust-engine",
      "approaches/fair-expectimax/rust-engine/src/leaf.rs",
      "approaches/fair-expectimax/README.mdx",
      "approaches/fair-expectimax/reference/main.cpp",
    ].join(" ")),
    [
      { family: "fair-expectimax", slug: "rust-engine" },
      { family: "fair-expectimax", slug: "reference" },
    ],
  );
});

test("family README paths do not produce approach references", () => {
  for (const path of [
    "approaches/fair-expectimax/README.mdx",
    "/approach/fair-expectimax/README.mdx",
    "/checkout/approaches/fair-expectimax/README.mdx",
  ]) {
    assert.deepEqual(approachRefsInText(path), [], path);
    assert.deepEqual(approachRefsInRecord({ evidenceRefs: [path] }), [], path);
  }
});

test("an approach README still links its containing approach", () => {
  assert.deepEqual(
    approachRefsInText("approaches/fair-expectimax/rust-engine/README.mdx"),
    [{ family: "fair-expectimax", slug: "rust-engine" }],
  );
});
