import assert from "node:assert/strict";
import test from "node:test";
import { legalColumns, serializeBoard } from "../../../src/core/typescript/engine.ts";
import { fastEvaluateMoves } from "./fast-search.ts";
import { buildGameTree, formatProbability, positionFromSeed, realizeTransition } from "./game-tree.ts";

const SEEDS = [0x5eed1001, 0x5eed1002, 0x5eed1003];

test("the leaf-only tree reproduces the solver's one-ply values at the root", () => {
  for (const seed of SEEDS) {
    const root = positionFromSeed(seed, 12);
    const tree = buildGameTree(root, 0, 49);
    const solver = fastEvaluateMoves(root, { maxDepth: 1, maxWork: 50_000_000, timeLimitMs: 600_000 });
    assert.equal(solver.depth, 1, `seed ${seed}: the solver completed one ply`);
    for (const entry of solver.columns) {
      const choice = tree.choices[entry.column];
      assert.equal(choice.legal, true);
      const scale = Math.max(1, Math.abs(entry.value));
      assert.ok(Math.abs(choice.value - entry.value) <= 1e-6 * scale, `seed ${seed} column ${entry.column}: ${choice.value} vs ${entry.value}`);
      assert.ok(Math.abs(choice.expectedScore - entry.expectedScore) <= 1e-6 * Math.max(1, Math.abs(entry.expectedScore)));
    }
    assert.equal(tree.bestColumn, solver.bestColumn, `seed ${seed} best column`);
  }
});

test("each choice's outcome probabilities sum to one and truncation accounts for the rest", () => {
  const root = positionFromSeed(SEEDS[0], 15);
  const tree = buildGameTree(root, 0, 7);
  for (const choice of tree.choices) {
    if (!choice.legal) continue;
    const shown = choice.outcomes.reduce((sum, outcome) => sum + outcome.probability, 0);
    assert.ok(Math.abs(shown + choice.hiddenProbability - 1) < 1e-9, `column ${choice.column}`);
    assert.ok(choice.outcomes.length <= 7);
    assert.ok(choice.unlisted ? choice.outcomes.length === 0 : choice.mergedOutcomes >= choice.outcomes.length);
    assert.ok(choice.streamedOutcomes >= choice.mergedOutcomes);
  }
  assert.equal(legalColumns(root.board).length, tree.choices.filter((choice) => choice.legal).length);
});

test("a realized transition reproduces the chosen outcome board with the engine's own frames", () => {
  let matched = 0;
  let total = 0;
  for (const seed of SEEDS) {
    const root = positionFromSeed(seed, 10);
    const tree = buildGameTree(root, 0, 7);
    for (const choice of tree.choices) {
      if (!choice.legal) continue;
      for (const outcome of choice.outcomes) {
        total += 1;
        const realized = realizeTransition(root, choice.column, outcome);
        if (!realized.matched) continue;
        matched += 1;
        assert.equal(serializeBoard(realized.state.board), serializeBoard(outcome.state.board));
        assert.equal(realized.state.nextDisc, outcome.state.nextDisc);
        assert.ok(realized.frames.length >= 1, "the engine produced animation frames");
        assert.equal(realized.frames[0].kind, "drop");
      }
    }
  }
  assert.ok(total > 0);
  assert.ok(matched / total >= 0.9, `matched ${matched} of ${total} outcomes`);
});

test("probabilities format as sevenths when exact", () => {
  assert.equal(formatProbability(1 / 7), "1/7");
  assert.equal(formatProbability(2 / 49), "2/49");
  assert.equal(formatProbability(0.123), "12.3%");
});
