import assert from "node:assert/strict";
import test from "node:test";
import {
  SOLID,
  createInitialLatentValues,
  legalColumns,
  seededRandom,
} from "../../../src/core/typescript/engine.ts";
import {
  CLASSIC_RULESET,
  createClassicGame,
  playClassicMove,
} from "../../../src/core/typescript/classic-engine.ts";
import { RECORDED_GAME_FORMAT } from "../../../src/core/typescript/recorded-game.ts";
import { validateGameSubmission } from "./validation.ts";

test("Classic submissions are replayed and score claims must match", () => {
  const random = seededRandom(2026);
  let state = createClassicGame(random);
  const columns: number[] = [];
  const discs = [state.nextDisc];
  const hidden: (1 | 2 | 3 | 4 | 5 | 6 | 7 | null)[] = [
    state.nextDisc === SOLID ? 3 : null,
  ];
  const rows: (1 | 2 | 3 | 4 | 5 | 6 | 7)[][] = [[1, 2, 3, 4, 5, 6, 7]];
  let latent = createInitialLatentValues(rows[0]);
  while (!state.gameOver) {
    const column = legalColumns(state.board)[0];
    columns.push(column);
    const move = playClassicMove(state, column, random, {
      captureAnimation: false,
      latent: {
        values: latent,
        droppedValue: hidden.at(-1),
        nextCoveredRow: () => {
          const row = [7, 6, 5, 4, 3, 2, 1] as const;
          rows.push([...row]);
          return row;
        },
      },
    });
    assert.ok(move?.latentValues);
    state = move.state;
    latent = [...move.latentValues];
    if (!state.gameOver) {
      discs.push(state.nextDisc);
      hidden.push(state.nextDisc === SOLID ? 4 : null);
    }
  }
  const body = {
    schemaVersion: 2,
    gameId: "game-1",
    startedAt: "2026-08-31T08:00:00.000Z",
    completedAt: "2026-08-31T08:05:00.000Z",
    source: { application: "drop7-mobile", platform: "ios", appVersion: "1.0.0" },
    mode: "classic",
    tape: {
      format: RECORDED_GAME_FORMAT,
      ruleset: CLASSIC_RULESET,
      columns,
      discs,
      dropLatentValues: hidden,
      coveredRows: rows,
    },
    claimedScore: state.score,
    claimedLevel: state.level,
    claimedMoves: state.movesPlayed,
  };
  const valid = validateGameSubmission(body, "classic", new Date("2026-08-31T09:00:00Z"));
  assert.equal(valid.ok, true);
  if (valid.ok) {
    assert.equal(valid.submission.verified_score, state.score);
    assert.equal(valid.submission.source_platform, "ios");
    assert.equal(JSON.parse(valid.submission.tape_json).columns.length, columns.length);
  }
  assert.deepEqual(validateGameSubmission({ ...body, claimedScore: state.score + 1 }, "classic"), {
    ok: false,
    error: "result-mismatch",
    status: 422,
  });
  assert.deepEqual(validateGameSubmission(body, "hardcore"), {
    ok: false,
    error: "invalid-submission",
    status: 400,
  });
  assert.deepEqual(
    validateGameSubmission({ ...body, mode: "hardcore" }, "hardcore"),
    { ok: false, error: "ruleset-mismatch", status: 400 },
  );
  assert.deepEqual(validateGameSubmission({ ...body, completedAt: "08/31/2026 08:05" }, "classic"), {
    ok: false,
    error: "invalid-submission",
    status: 400,
  });
});
