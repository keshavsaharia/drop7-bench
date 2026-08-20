import assert from "node:assert/strict";
import test from "node:test";
import {
  headlessDisc,
  runHeadlessGame,
  runHeadlessTournament,
} from "./headless.ts";

const FIXED_SEARCH = {
  maxDepth: 3,
  maxWork: 1_500,
} as const;

test("the next-disc sequence is deterministic and keyed by move number", () => {
  const first = Array.from({ length: 20 }, (_, move) =>
    headlessDisc(42, move),
  );
  const second = Array.from({ length: 20 }, (_, move) =>
    headlessDisc(42, move),
  );
  const other = Array.from({ length: 20 }, (_, move) =>
    headlessDisc(43, move),
  );

  assert.deepEqual(first, second);
  assert.notDeepEqual(first, other);
  assert.ok(first.every((disc) => disc >= 1 && disc <= 7));
});

test("a fixed-work headless game reproduces every move and score", () => {
  const options = {
    seed: 17,
    heuristicProfile: "combined" as const,
    search: FIXED_SEARCH,
    maxMoves: 8,
    trace: true,
  };
  const first = runHeadlessGame(options);
  const second = runHeadlessGame(options);

  assert.deepEqual(first.trace, second.trace);
  assert.equal(first.score, second.score);
  assert.equal(first.moves, second.moves);
  assert.equal(first.searchWork, second.searchWork);
  assert.equal(first.censored, true);
});

test("different policies receive the same paired future-disc sequence", () => {
  const legacy = runHeadlessGame({
    seed: 91,
    heuristicProfile: "legacy",
    search: FIXED_SEARCH,
    maxMoves: 6,
    trace: true,
  });
  const combined = runHeadlessGame({
    seed: 91,
    heuristicProfile: "combined",
    search: FIXED_SEARCH,
    maxMoves: 6,
    trace: true,
  });

  assert.deepEqual(
    legacy.trace?.map((move) => move.disc),
    combined.trace?.map((move) => move.disc),
  );
});

test("tournaments report paired deltas and censored games", () => {
  const tournament = runHeadlessTournament({
    profiles: ["legacy", "combined"],
    seeds: [3, 4],
    search: FIXED_SEARCH,
    maxMoves: 3,
  });

  assert.equal(tournament.games.length, 4);
  assert.equal(tournament.summaries.length, 2);
  assert.equal(tournament.summaries[0].pairedDelta95, null);
  assert.equal(tournament.summaries[0].pairedGames, 0);
  assert.equal(tournament.summaries[0].ties, 0);
  assert.equal(tournament.summaries[0].censoredGames, 2);
  assert.equal(
    tournament.summaries[1].wins +
      tournament.summaries[1].ties +
      tournament.summaries[1].losses,
    0,
  );
});

test("tournaments validate and deduplicate uint32 seeds", () => {
  assert.throws(
    () =>
      runHeadlessGame({
        seed: -1,
        search: FIXED_SEARCH,
        maxMoves: 1,
      }),
    /uint32/,
  );

  const tournament = runHeadlessTournament({
    profiles: ["combined"],
    seeds: [12, 12],
    search: FIXED_SEARCH,
    maxMoves: 1,
  });
  assert.equal(tournament.games.length, 1);
});
