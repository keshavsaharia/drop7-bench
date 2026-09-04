import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { SNAPSHOT_FORMAT, snapshotStages, validateSnapshot, type EvolutionSnapshot } from "./evolution.ts";

const dir = new URL("../../content/figures/nnue-evolution/", import.meta.url);
const full = JSON.parse(readFileSync(new URL("RUN-20260902T035644Z-c1fd8987.json", dir), "utf8"));
const empty = JSON.parse(readFileSync(new URL("RUN-20260903T032832Z-a76a6cf7.json", dir), "utf8"));

function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

test("the first run's snapshot validates with every stage present", () => {
  const snapshot = validateSnapshot(full, "run 1");
  assert.equal(snapshot.format, SNAPSHOT_FORMAT);
  assert.deepEqual(snapshotStages(snapshot), ["corpus", "pretrain", "evolve", "select", "screen"]);
  assert.equal(snapshot.evolve?.generations.length, snapshot.evolve?.generationsCompleted);
  assert.equal(snapshot.derived.length, 4);
});

test("the continuation snapshot validates as a run with no stages yet", () => {
  const snapshot = validateSnapshot(empty, "run 2");
  assert.deepEqual(snapshotStages(snapshot), []);
  assert.equal(snapshot.runLifecycle, "running");
});

test("a truncated generations array is refused", () => {
  const broken = clone(full) as EvolutionSnapshot;
  broken.evolve!.generations.pop();
  assert.throws(() => validateSnapshot(broken), /evolve\.generations has 59 rows but generationsCompleted is 60/);
});

test("per-game rows must match the arm's game count; contrasts must name real arms", () => {
  const broken = clone(full) as EvolutionSnapshot;
  broken.screen!.arms.candidate.perGame.pop();
  assert.throws(() => validateSnapshot(broken), /perGame has 63 rows but games is 64/);
  const renamed = clone(full) as EvolutionSnapshot;
  renamed.screen!.paired["candidate-vs-fair-d3s7"].candidateArm = "ghost";
  assert.throws(() => validateSnapshot(renamed), /candidateArm ghost is not a screen arm/);
});

test("the format string and stage keys are required", () => {
  assert.throws(() => validateSnapshot({ ...clone(empty), format: "other" }), /format must be/);
  const missing = clone(empty) as Record<string, unknown>;
  delete missing.screen;
  assert.throws(() => validateSnapshot(missing), /missing stage screen/);
  assert.throws(() => validateSnapshot(null), /not an object/);
});
