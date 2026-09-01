import assert from "node:assert/strict";
import test from "node:test";
import {
  createGame,
  createInitialLatentValues,
  legalColumns,
  playMove,
  randomDisc,
  seededRandom,
  serializeBoard,
  type DiscValue,
} from "./engine.ts";
import {
  HARDCORE_RULESET,
  RECORDED_GAME_FORMAT,
  evaluateRecordedGameTape,
  type RecordedGameTape,
} from "./recorded-game.ts";

test("an exact Hardcore tape validates its full random configuration", () => {
  const random = seededRandom(0x7a9e_2026);
  const policy = seededRandom(0x51b1_1e01);
  let state = createGame(random);
  const columns: number[] = [];
  const discs = [state.nextDisc];
  const dropLatentValues: null[] = [null];
  const coveredRows: DiscValue[][] = [randomRow(random)];
  let latent = createInitialLatentValues(coveredRows[0]);

  while (!state.gameOver) {
    const legal = legalColumns(state.board);
    const column = legal[Math.floor(policy() * legal.length)];
    columns.push(column);
    const move = playMove(state, column, random, {
      captureAnimation: false,
      latent: {
        values: latent,
        nextCoveredRow: () => {
          const row = randomRow(random);
          coveredRows.push(row);
          return row;
        },
      },
    });
    assert.ok(move?.latentValues);
    state = move.state;
    latent = [...move.latentValues];
    if (!state.gameOver) {
      discs.push(state.nextDisc);
      dropLatentValues.push(null);
    }
  }

  const tape: RecordedGameTape = {
    format: RECORDED_GAME_FORMAT,
    ruleset: HARDCORE_RULESET,
    columns,
    discs,
    dropLatentValues,
    coveredRows,
  };
  const replay = evaluateRecordedGameTape(tape);
  assert.equal(replay.valid, true);
  assert.equal(replay.score, state.score);
  assert.equal(replay.level, state.level);
  assert.equal(replay.moves, state.movesPlayed);
  assert.equal(serializeBoard(replay.finalState!.board), serializeBoard(state.board));

  assert.equal(
    evaluateRecordedGameTape({ ...tape, coveredRows: [...coveredRows, randomRow(random)] }).failure,
    "invalid-configuration",
  );
  assert.equal(
    evaluateRecordedGameTape({ ...tape, columns: columns.slice(0, -1), discs: discs.slice(0, -1), dropLatentValues: dropLatentValues.slice(0, -1) }).failure,
    "incomplete-game",
  );
});

function randomRow(random: ReturnType<typeof seededRandom>): DiscValue[] {
  return Array.from({ length: 7 }, () => randomDisc(random));
}
