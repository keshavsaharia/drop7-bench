import assert from "node:assert/strict";
import { test } from "node:test";
import { legalColumns } from "../../../src/core/typescript/engine.ts";
import { playScriptedGame } from "../../../src/bench/runner.ts";
import {
  COMPETITION_GAMES,
  COMPETITION_GAME_KEY,
  COMPETITION_ROUND,
} from "./game.ts";
import { getCompetitionGame } from "./registry.ts";
import { packColumns, unpackColumns } from "./packing.ts";
import { replayCompetitionColumns } from "./replay.ts";

test("3-bit column packing round-trips across byte boundaries", () => {
  const columns = [0, 6, 3, 1, 5, 2, 4, 0, 6, 6, 1];
  const packed = packColumns(columns);
  assert.equal(packed.byteLength, Math.ceil((columns.length * 3) / 8));
  assert.deepEqual(unpackColumns(packed, columns.length), columns);
});

test("competition catalog resolves the current and historical game registry", () => {
  assert.ok(COMPETITION_GAMES.length >= 1);
  const current = getCompetitionGame(COMPETITION_GAME_KEY);
  assert.ok(current);
  assert.equal(current.gameKey, COMPETITION_GAME_KEY);
  assert.equal(current.manifest.roundId, current.round.id);
});

test("column packing rejects out-of-range values and non-zero padding", () => {
  assert.throws(() => packColumns([7]), /outside 0-6/);
  assert.throws(() => unpackColumns(Uint8Array.of(1), 1), /non-zero padding/);
});

test("competition replay exactly reproduces a legal scripted game", () => {
  const played = playScriptedGame(
    {
      id: "test-first-legal",
      name: "Test first legal",
      family: "test",
      description: "test",
      researchPath: "/approaches/heuristic-search/policy-comparison",
      publicInformation: true,
      slow: false,
      chooseColumn: (state) => legalColumns(state.board)[0] ?? null,
    },
    COMPETITION_ROUND,
  );
  const columns = played.frames.map((frame) => frame.column);
  const replayed = replayCompetitionColumns(COMPETITION_ROUND, columns);

  assert.equal(replayed.valid, true);
  assert.equal(replayed.score, played.score);
  assert.equal(replayed.moves, played.moves);
  assert.equal(replayed.censored, played.censored);
  assert.deepEqual(
    replayed.frames.map((frame) => [
      frame.column,
      frame.score,
      frame.placedBoard,
      frame.board,
    ]),
    played.frames.map((frame) => [
      frame.column,
      frame.score,
      frame.placedBoard,
      frame.board,
    ]),
  );
});

test("competition replay can capture serializable presentation frames", () => {
  const played = playScriptedGame(
    {
      id: "test-animation-capture",
      name: "Test animation capture",
      family: "test",
      description: "test",
      researchPath: "/approaches/heuristic-search/policy-comparison",
      publicInformation: true,
      slow: false,
      chooseColumn: (state) => legalColumns(state.board)[0] ?? null,
    },
    COMPETITION_ROUND,
  );
  const columns = played.frames.map((frame) => frame.column);
  const replayed = replayCompetitionColumns(COMPETITION_ROUND, columns, {
    captureAnimation: true,
  });

  assert.equal(replayed.valid, true);
  assert.equal(replayed.score, played.score);
  assert.equal(
    replayed.frames.every((frame) => (frame.animation?.length ?? 0) > 0),
    true,
  );
  assert.equal(
    replayed.frames.every((frame) =>
      frame.animation?.every(
        (presentationFrame) => presentationFrame.board.length === 49,
      ),
    ),
    true,
  );
  for (const frame of replayed.frames) {
    const drop = frame.animation?.[0];
    assert.ok(drop);
    assert.equal(drop.kind, "drop");
    assert.equal(drop.indexes.length, 1);
    assert.equal(drop.indexes[0] % 7, frame.column);
    assert.equal(drop.board[drop.indexes[0]], String(frame.disc));
    assert.equal(frame.placedBoard, drop.board);
    assert.equal(frame.animation?.at(-1)?.board, frame.board);
  }
  const levelFrames = replayed.frames.filter((frame) => frame.levelAdvanced);
  assert.ok(levelFrames.length > 0);
  for (const frame of levelFrames) {
    const riseIndex = frame.animation?.findIndex(
      (animationFrame) => animationFrame.kind === "rise",
    );
    assert.notEqual(riseIndex, undefined);
    assert.ok(riseIndex! > 0);
  }
  assert.doesNotThrow(() => JSON.stringify(replayed.frames));
});

test("competition replay rejects truncated, illegal, and trailing choices", () => {
  const played = playScriptedGame(
    {
      id: "test-terminal",
      name: "Test terminal",
      family: "test",
      description: "test",
      researchPath: "/approaches/heuristic-search/policy-comparison",
      publicInformation: true,
      slow: false,
      chooseColumn: (state) => legalColumns(state.board)[0] ?? null,
    },
    COMPETITION_ROUND,
  );
  const columns = played.frames.map((frame) => frame.column);

  assert.equal(
    replayCompetitionColumns(COMPETITION_ROUND, columns.slice(0, -1)).failure,
    "incomplete",
  );
  assert.equal(
    replayCompetitionColumns(COMPETITION_ROUND, [...columns, 0]).failure,
    "trailing-moves",
  );

  const fullFrameIndex = played.frames.findIndex(
    (frame, index) =>
      index < played.frames.length - 1 &&
      Array.from({ length: 7 }, (_, column) => frame.board[column]).some(
        (cell) => cell !== "0",
      ),
  );
  assert.notEqual(fullFrameIndex, -1);
  const fullColumn = Array.from(
    { length: 7 },
    (_, column) => column,
  ).find((column) => played.frames[fullFrameIndex].board[column] !== "0");
  assert.notEqual(fullColumn, undefined);
  const illegal = replayCompetitionColumns(COMPETITION_ROUND, [
    ...columns.slice(0, fullFrameIndex + 1),
    fullColumn!,
  ]);
  assert.equal(illegal.valid, false);
  assert.equal(illegal.failure, "illegal-column");
});
