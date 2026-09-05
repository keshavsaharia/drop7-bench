import assert from "node:assert/strict";
import test from "node:test";
import { loadLearnPage } from "./learn.ts";
import { VOCABULARY_TOPICS, vocabularyTerms, vocabularyTopic } from "./vocabulary.ts";

test("topic pages retain every existing definition without duplicating or dropping rows", () => {
  const original = loadLearnPage("glossary");
  assert.ok(original);
  const rows = original.content.split("\n").filter((line) => line.startsWith("| **"));
  const terms = VOCABULARY_TOPICS.flatMap((topic) => vocabularyTerms(topic.slug));
  assert.equal(terms.length, rows.length);
  for (const term of terms) assert.ok(rows.includes(`| **${term.title}** | ${term.meaning} |`));
  for (const topic of VOCABULARY_TOPICS) {
    const group = vocabularyTerms(topic.slug);
    assert.ok(group.length > 0);
    assert.equal(new Set(group.map((term) => term.id)).size, group.length);
    assert.ok(group.every((term) => term.id.length > 0));
  }
});

test("the two meanings of run have separate, stable topic links", () => {
  const game = vocabularyTerms("game").find((term) => term.id === "run");
  const evidence = vocabularyTerms("evidence").find((term) => term.id === "run");
  assert.match(game?.meaning ?? "", /occupied cells/);
  assert.match(evidence?.meaning ?? "", /execution/);
});

test("unknown vocabulary topics cannot select another file", () => {
  for (const slug of ["missing", "../rules", "Game"]) {
    assert.equal(vocabularyTopic(slug), null);
    assert.deepEqual(vocabularyTerms(slug), []);
  }
});
