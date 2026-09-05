import assert from "node:assert/strict";
import test from "node:test";
import { OUTCOMES, READS, STATUSES, TIERS, VALIDITY, evidenceChip, evidenceChips, tierSentence, unknownEvidenceWords } from "./evidence.ts";

test("valid + fail is a completed contribution: both chips neutral, never critical", () => {
  const chips = evidenceChips({ validity: "valid", outcome: "fail" });
  assert.deepEqual(
    chips.map((c) => [c.field, c.word, c.tone]),
    [
      ["validity", "valid", "neutral"],
      ["outcome", "fail", "neutral"],
    ],
  );
  assert.ok(chips.every((c) => c.tone !== "critical" && c.tone !== "serious"));
  assert.ok(chips.every((c) => c.known));
});

test("pass is good, partial and inconclusive warn, invalid is serious", () => {
  assert.equal(evidenceChip("outcome", "pass").tone, "good");
  assert.equal(evidenceChip("validity", "partial").tone, "warning");
  assert.equal(evidenceChip("outcome", "inconclusive").tone, "warning");
  assert.equal(evidenceChip("validity", "invalid").tone, "serious");
  assert.equal(evidenceChip("outcome", "not-applicable").tone, "neutral");
});

test("oracle and teacher both read as serious with the eye-slash icon; public and diagnostic are neutral", () => {
  for (const word of ["oracle", "teacher"]) {
    const chip = evidenceChip("reads", word);
    assert.equal(chip.tone, "serious");
    assert.equal(chip.icon, "eye-slash");
  }
  assert.equal(evidenceChip("reads", "public").tone, "neutral");
  assert.equal(evidenceChip("reads", "diagnostic").tone, "neutral");
});

test("unknown words render neutral with the raw word and are flagged, never relabelled", () => {
  const chip = evidenceChip("status", "retired");
  assert.equal(chip.tone, "neutral");
  assert.equal(chip.word, "retired");
  assert.equal(chip.known, false);
  assert.deepEqual(unknownEvidenceWords({ status: "retired", tier: "exploratory", validity: "valid" }), [
    { field: "status", word: "retired" },
    { field: "tier", word: "exploratory" },
  ]);
  assert.deepEqual(unknownEvidenceWords({ status: "completed", tier: "STANDARD", cohort: "anything goes" }), []);
});

test("tiers are never coloured; their sentence is the recorded wording", () => {
  for (const tier of TIERS) {
    const chip = evidenceChip("tier", tier);
    assert.equal(chip.tone, "neutral");
    assert.ok(chip.known);
  }
  assert.match(tierSentence("STANDARD"), /64-game/);
  assert.equal(tierSentence("made-up"), "made-up");
});

test("every closed-vocabulary word is known, and chips come out in the strip's order", () => {
  for (const word of STATUSES) assert.ok(evidenceChip("status", word).known, word);
  for (const word of READS) assert.ok(evidenceChip("reads", word).known, word);
  for (const word of VALIDITY) assert.ok(evidenceChip("validity", word).known, word);
  for (const word of OUTCOMES) assert.ok(evidenceChip("outcome", word).known, word);
  const chips = evidenceChips({ cohort: "64 paired games", reads: "public", status: "completed", tier: "STANDARD" });
  assert.deepEqual(
    chips.map((c) => c.field),
    ["status", "tier", "reads", "cohort"],
  );
  assert.deepEqual(evidenceChips({}), []);
  assert.deepEqual(evidenceChips({ status: "  " }), []);
});
