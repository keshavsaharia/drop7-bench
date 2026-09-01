import assert from "node:assert/strict";
import test from "node:test";

import {
  CLASSIC_FIRST_LEVEL_DROPS,
  CLASSIC_LEVEL_BONUS,
  classicDropsForLevel,
  createClassicGame,
  playClassicMove,
  randomClassicDisc,
} from "./classic-engine.ts";
import {
  EMPTY,
  SOLID,
  createInitialLatentValues,
  legalColumns,
  seededRandom,
  serializeBoard,
} from "./engine.ts";
import {
  CLASSIC_RULESET,
  RECORDED_GAME_FORMAT,
  evaluateRecordedGameTape,
  type RecordedGameTape,
} from "./recorded-game.ts";

test("Classic TypeScript matches the shared native conformance transition", () => {
  const state = { ...createClassicGame(() => 0), board: Array(49).fill(EMPTY), nextDisc: 1 as const };
  const board = [...state.board];
  board[44] = 3;
  board[45] = SOLID;
  board[46] = 3;
  const latent = Array<1 | 2 | 3 | 4 | 5 | 6 | 7 | null>(49).fill(null);
  latent[45] = 6;

  const result = playClassicMove({ ...state, board }, 0, () => 0, {
    captureAnimation: false,
    latent: { values: latent, nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7] },
  });
  assert.ok(result?.latentValues);
  assert.deepEqual(result.waves, [{ depth: 1, cleared: 3, revealed: 1, points: 21 }]);
  assert.equal(result.scoreDelta, 21);
  assert.equal(result.state.score, 21);
  assert.equal(result.state.movesRemaining, 29);
  assert.equal(result.state.board[45], 6);
  assert.equal(result.state.board.filter((cell) => cell !== EMPTY).length, 1);
  assert.equal(result.latentValues.every((value) => value === null), true);
});

test("Classic uses a decreasing 30-drop clock and 7,000-point rise bonus", () => {
  assert.equal(CLASSIC_FIRST_LEVEL_DROPS, 30);
  assert.equal(classicDropsForLevel(1), 30);
  assert.equal(classicDropsForLevel(2), 29);
  assert.equal(classicDropsForLevel(30), 1);
  assert.equal(classicDropsForLevel(80), 1);
  assert.equal(CLASSIC_LEVEL_BONUS, 7_000);
});

test("Classic incoming discs contain seven numbers and one gray outcome", () => {
  for (let value = 1; value <= 7; value += 1) {
    assert.equal(randomClassicDisc(() => (value - 0.5) / 8), value);
  }
  assert.equal(randomClassicDisc(() => 7 / 8), SOLID);
});

test("Classic accepts latent values for dropped gray discs", () => {
  const state = { ...createClassicGame(() => 0.999), board: Array(49).fill(0) };
  const result = playClassicMove(state, 3, () => 0, {
    captureAnimation: false,
    latent: {
      values: createInitialLatentValues([1, 2, 3, 4, 5, 6, 7]).map(() => null),
      droppedValue: 6,
      nextCoveredRow: () => [1, 2, 3, 4, 5, 6, 7],
    },
  });
  assert.ok(result?.latentValues);
  assert.equal(result.latentValues[45], 6);
});

test("an explicit Classic tape is independently replayed to completion", () => {
  const random = seededRandom(0xc1a5_2026);
  const policy = seededRandom(0x51b1_1e00);
  let state = createClassicGame(random);
  const columns: number[] = [];
  const discs = [state.nextDisc];
  const dropLatentValues: (1 | 2 | 3 | 4 | 5 | 6 | 7 | null)[] = [
    state.nextDisc === SOLID ? 4 : null,
  ];
  const coveredRows = [[1, 2, 3, 4, 5, 6, 7] as const];
  let latent = createInitialLatentValues(coveredRows[0]);
  for (let move = 0; move < 5_000 && !state.gameOver; move += 1) {
    const legal = legalColumns(state.board);
    const column = legal[Math.floor(policy() * legal.length)];
    columns.push(column);
    const result = playClassicMove(state, column, random, {
      captureAnimation: false,
      latent: {
        values: latent,
        droppedValue: dropLatentValues.at(-1),
        nextCoveredRow: () => {
          const row = Array.from({ length: 7 }, () =>
            (Math.floor(random() * 7) + 1) as 1 | 2 | 3 | 4 | 5 | 6 | 7
          );
          coveredRows.push(row);
          return row;
        },
      },
    });
    assert.ok(result?.latentValues);
    state = result.state;
    latent = [...result.latentValues];
    if (!state.gameOver) {
      discs.push(state.nextDisc);
      dropLatentValues.push(state.nextDisc === SOLID ? 5 : null);
    }
  }
  const tape: RecordedGameTape = {
    format: RECORDED_GAME_FORMAT,
    ruleset: CLASSIC_RULESET,
    columns,
    discs,
    dropLatentValues,
    coveredRows: coveredRows.map((row) => [...row]),
  };
  const replay = evaluateRecordedGameTape(tape);
  assert.equal(replay.valid, true);
  assert.equal(replay.score, state.score);
  assert.equal(serializeBoard(replay.finalState!.board), serializeBoard(state.board));
});
