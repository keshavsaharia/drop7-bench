import assert from "node:assert/strict";
import test from "node:test";
import { LEVEL_BONUS } from "../../../src/core/typescript/engine.ts";
import {
  buildExplosionScoreBars,
  explosionPointsForFrame,
  type ScoreChartFrame,
} from "./score-chart.ts";

const frames: ScoreChartFrame[] = [
  { move: 1, scoreDelta: 10, levelAdvanced: false },
  { move: 2, scoreDelta: 0, levelAdvanced: false },
  { move: 3, scoreDelta: 25, levelAdvanced: false },
  { move: 4, scoreDelta: 0, levelAdvanced: false },
  { move: 5, scoreDelta: LEVEL_BONUS + 40, levelAdvanced: true },
  { move: 6, scoreDelta: 4, levelAdvanced: false },
  { move: 7, scoreDelta: 0, levelAdvanced: false },
  { move: 8, scoreDelta: 0, levelAdvanced: false },
  { move: 9, scoreDelta: 0, levelAdvanced: false },
  { move: 10, scoreDelta: LEVEL_BONUS, levelAdvanced: true },
  { move: 11, scoreDelta: 9, levelAdvanced: false },
];

test("explosion points exclude only the level-rise bonus", () => {
  assert.equal(explosionPointsForFrame(frames[4]), 40);
  assert.equal(explosionPointsForFrame(frames[9]), 0);
  assert.equal(explosionPointsForFrame(frames[10]), 9);
});

test("score bars preserve per-move explosion points and round membership", () => {
  assert.deepEqual(buildExplosionScoreBars(frames, "move"), [
    { startMove: 1, endMove: 1, round: 1, points: 10 },
    { startMove: 2, endMove: 2, round: 1, points: 0 },
    { startMove: 3, endMove: 3, round: 1, points: 25 },
    { startMove: 4, endMove: 4, round: 1, points: 0 },
    { startMove: 5, endMove: 5, round: 1, points: 40 },
    { startMove: 6, endMove: 6, round: 2, points: 4 },
    { startMove: 7, endMove: 7, round: 2, points: 0 },
    { startMove: 8, endMove: 8, round: 2, points: 0 },
    { startMove: 9, endMove: 9, round: 2, points: 0 },
    { startMove: 10, endMove: 10, round: 2, points: 0 },
    { startMove: 11, endMove: 11, round: 3, points: 9 },
  ]);
});

test("round bars support per-round and running explosion totals", () => {
  assert.deepEqual(buildExplosionScoreBars(frames, "round"), [
    { startMove: 1, endMove: 5, round: 1, points: 75 },
    { startMove: 6, endMove: 10, round: 2, points: 4 },
    { startMove: 11, endMove: 11, round: 3, points: 9 },
  ]);
  assert.deepEqual(buildExplosionScoreBars(frames, "round", true), [
    { startMove: 1, endMove: 5, round: 1, points: 75 },
    { startMove: 6, endMove: 10, round: 2, points: 79 },
    { startMove: 11, endMove: 11, round: 3, points: 88 },
  ]);
});
