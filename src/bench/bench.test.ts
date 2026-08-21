import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  type GameState,
} from "../core/typescript/engine.ts";
import { getPolicy } from "./policies.ts";
import { playScriptedGame } from "./runner.ts";
import { validateScriptedRound } from "./rounds.ts";
import { parsePosition, stateFromPosition } from "./d7p-server.ts";

const ROUNDS_DIR = join(dirname(fileURLToPath(import.meta.url)), "rounds");

function loadRound(id: string) {
  return validateScriptedRound(
    JSON.parse(readFileSync(join(ROUNDS_DIR, `${id}.json`), "utf8")),
  );
}

test("every checked-in scripted round is valid and self-consistent", () => {
  const files = readdirSync(ROUNDS_DIR).filter((file) => file.endsWith(".json"));
  assert.ok(files.length >= 8, "the standard gauntlet suite is present");
  for (const file of files) {
    const round = validateScriptedRound(
      JSON.parse(readFileSync(join(ROUNDS_DIR, file), "utf8")),
    );
    assert.equal(round.discs.length >= round.maximumMoves, true);
    assert.equal(
      round.latentRows.length >= Math.ceil(round.maximumMoves / 5) + 1,
      true,
    );
    const distinctDiscs = new Set(round.discs.slice(0, 200));
    assert.ok(distinctDiscs.size > 3, `${round.id} disc tape is not degenerate`);
  }
});

test("scripted games are exactly deterministic across repeated runs", () => {
  const round = loadRound("gauntlet-01");
  const policy = getPolicy("expectimax-d2");
  const first = playScriptedGame(policy, round);
  const second = playScriptedGame(policy, round);
  assert.equal(first.checksum, second.checksum);
  assert.equal(first.score, second.score);
  assert.deepEqual(
    first.frames.map((frame) => frame.board),
    second.frames.map((frame) => frame.board),
  );
  assert.ok(first.moves > 10, "the game should survive a while");
});

test("two different policies consume the same disc tape and latent rows", () => {
  const round = loadRound("gauntlet-02");
  const greedy = playScriptedGame(getPolicy("greedy"), round);
  const d2 = playScriptedGame(getPolicy("expectimax-d2"), round);
  // The visible disc at every shared move index is identical by construction.
  const shared = Math.min(greedy.frames.length, d2.frames.length, 30);
  for (let index = 0; index < shared; index += 1) {
    assert.equal(greedy.frames[index].disc, round.discs[index]);
    assert.equal(d2.frames[index].disc, round.discs[index]);
  }
});

test("an illegal policy choice falls back to a legal column and is counted", () => {
  const round = loadRound("gauntlet-01");
  const illegalPolicy = {
    id: "always-illegal",
    name: "Always illegal",
    family: "test",
    description: "test double",
    publicInformation: true,
    chooseColumn: () => 99,
  };
  const game = playScriptedGame(illegalPolicy, round);
  assert.ok(game.illegalMoves > 0);
  assert.equal(game.illegalMoves, game.moves);
});

test("d7p position parsing accepts startpos and explicit boards", () => {
  const fromStart = parsePosition(["startpos", "next", "3", "rise", "5"]);
  assert.deepEqual([...fromStart.board], [...createInitialBoard()]);
  assert.equal(fromStart.nextDisc, 3);
  assert.equal(fromStart.movesRemaining, MOVES_PER_LEVEL);

  const encoded = createInitialBoard().join("");
  const explicit = parsePosition(["board", encoded, "next", "7", "rise", "2"]);
  assert.equal(explicit.nextDisc, 7);
  assert.equal(explicit.movesRemaining, 2);

  assert.throws(() => parsePosition(["board", "000", "next", "3", "rise", "5"]));
  assert.throws(() => parsePosition(["startpos", "next", "9", "rise", "5"]));
  assert.throws(() => parsePosition(["startpos", "next", "3"]));
});

test("d7p positions become a public-only game state that policies can answer", () => {
  const position = parsePosition(["startpos", "next", "4", "rise", "5"]);
  const state: GameState = stateFromPosition(position);
  assert.equal(state.score, 0);
  assert.equal(state.level, 1);
  const column = getPolicy("greedy").chooseColumn(state);
  assert.ok(
    column !== null && column >= 0 && column < BOARD_SIZE,
    "policy answered with a column",
  );
});
