import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  CLEAR_BONUS,
  CRACKED,
  EMPTY,
  LEVEL_BONUS,
  MOVES_PER_LEVEL,
  SOLID,
  applyGravity,
  boardFromRows,
  boardToRows,
  contiguousLineLength,
  createGame,
  emptyBoard,
  enumerateCascadeOutcomes,
  enumerateMoveOutcomes,
  findPoppers,
  forEachMoveOutcome,
  legalColumns,
  playMove,
  raiseCoveredRow,
  resolveCascade,
  scoreForWave,
  seededRandom,
  type Cell,
  type GameState,
} from "./engine.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function stateWith(
  board: readonly Cell[],
  overrides: Partial<GameState> = {},
): GameState {
  return {
    board,
    nextDisc: 4,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
    ...overrides,
  };
}

test("scoring constants match the original game", () => {
  assert.equal(LEVEL_BONUS, 17_000);
  assert.equal(CLEAR_BONUS, 70_000);
  assert.deepEqual(
    Array.from({ length: 8 }, (_, index) => scoreForWave(index + 1)),
    [7, 39, 109, 224, 391, 617, 907, 1_267],
  );
});

test("gravity preserves the order of discs in every column", () => {
  const board = boardFromRows([
    row(E, E, E, E, E, E, E),
    row(2, E, E, E, E, E, E),
    row(E, E, E, E, E, E, E),
    row(5, E, E, E, E, E, E),
    row(E, E, 7, E, E, E, E),
    row(CRACKED, E, E, E, E, E, E),
    row(E, E, SOLID, E, E, E, E),
  ]);

  const rows = boardToRows(applyGravity(board));
  assert.deepEqual(
    rows.map((cells) => cells[0]),
    [E, E, E, E, 2, 5, CRACKED],
  );
  assert.deepEqual(
    rows.map((cells) => cells[2]),
    [E, E, E, E, E, 7, SOLID],
  );
});

test("line counts stop at gaps and include covered discs", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(2, SOLID, 3, E, 4, CRACKED, E),
  ]);

  assert.equal(contiguousLineLength(board, 6, 0, "row"), 3);
  assert.equal(contiguousLineLength(board, 6, 2, "row"), 3);
  assert.equal(contiguousLineLength(board, 6, 4, "row"), 2);
  assert.equal(contiguousLineLength(board, 6, 3, "row"), 0);
});

test("a wave clears all matching discs simultaneously", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(2, 2, E, E, E, E, E),
  ]);
  const result = resolveCascade(board, () => 0);

  assert.equal(result.score, 14);
  assert.equal(result.waves.length, 1);
  assert.equal(result.waves[0].cleared, 2);
  assert.deepEqual(result.board, emptyBoard());
});

test("a move records its landing before any explosions", () => {
  const game = stateWith(emptyBoard(), {
    nextDisc: 4,
    movesRemaining: 2,
  });
  const result = playMove(game, 3, () => 0.5);

  assert.ok(result);
  assert.equal(result.animation[0].kind, "drop");
  assert.deepEqual(result.animation[0].indexes, [6 * BOARD_SIZE + 3]);
  assert.equal(result.animation[0].board[6 * BOARD_SIZE + 3], 4);
});

test("headless moves can skip presentation snapshots", () => {
  const game = stateWith(emptyBoard(), {
    nextDisc: 4,
    movesRemaining: 2,
  });
  const result = playMove(game, 3, () => 0.5, {
    captureAnimation: false,
  });

  assert.ok(result);
  assert.deepEqual(result.animation, []);
  assert.equal(result.state.board[6 * BOARD_SIZE + 3], 4);
});

test("matching discs get distinct sequential burst snapshots", () => {
  const game = stateWith(
    boardFromRows([
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      row(2, E, E, E, E, E, E),
    ]),
    { nextDisc: 2, movesRemaining: 2 },
  );
  const result = playMove(game, 1, () => 0.5);

  assert.ok(result);
  const bursts = result.animation.filter((frame) => frame.kind === "burst");
  assert.equal(bursts.length, 2);
  assert.deepEqual(
    bursts.map((frame) => frame.indexes[0]),
    [6 * BOARD_SIZE, 6 * BOARD_SIZE + 1],
  );
  assert.equal(bursts[0].board[6 * BOARD_SIZE], 2);
  assert.equal(bursts[1].board[6 * BOARD_SIZE], E);
  assert.equal(bursts[1].board[6 * BOARD_SIZE + 1], 2);
});

test("two hits in one wave fully reveal a solid disc", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(3, SOLID, 3, E, E, E, E),
  ]);
  const result = resolveCascade(board, () => 0.999);

  assert.equal(result.score, 14);
  assert.equal(result.waves[0].revealed, 1);
  assert.equal(result.board[6 * BOARD_SIZE + 1], 7);
});

test("gravity, cracks, reveals, and scoring compose across chain waves", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(2, E, E, E, E, E, E),
    row(3, E, E, E, E, E, E),
    row(SOLID, E, E, E, E, E, E),
  ]);
  const result = resolveCascade(board, () => 0.999);

  assert.equal(result.score, scoreForWave(1) + scoreForWave(2));
  assert.deepEqual(
    result.waves.map((wave) => [wave.cleared, wave.revealed]),
    [
      [1, 0],
      [1, 1],
    ],
  );
  assert.equal(result.board[6 * BOARD_SIZE], 7);
});

test("exact gray-disc outcomes retain their full probability mass", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(3, SOLID, 3, E, E, E, E),
  ]);
  const outcomes = enumerateCascadeOutcomes(board);

  assert.equal(outcomes.length, 7);
  assert.ok(
    Math.abs(
      outcomes.reduce((sum, outcome) => sum + outcome.probability, 0) - 1,
    ) < 1e-12,
  );
  assert.ok(outcomes.some((outcome) => outcome.score === 14 + 39));
  assert.equal(
    outcomes.filter((outcome) => outcome.score === 14).length,
    6,
  );
});

test("the Hardcore game starts above a solid row and cracks it on a 1", () => {
  const game = createGame(() => 0);
  const result = playMove(game, 0, () => 0);

  assert.ok(result);
  assert.equal(result.scoreDelta, 7);
  assert.equal(result.state.board[6 * BOARD_SIZE], CRACKED);
  assert.equal(result.state.movesRemaining, MOVES_PER_LEVEL - 1);
  assert.equal(result.state.nextDisc, 1);
});

test("clearing the board awards the original screen-clear bonus", () => {
  const game = stateWith(emptyBoard(), {
    nextDisc: 1,
    movesRemaining: 2,
  });
  const result = playMove(game, 3, () => 0.5);

  assert.ok(result);
  assert.equal(result.scoreDelta, CLEAR_BONUS + 7);
  assert.equal(result.clearedBoard, true);
});

test("every fifth move raises a solid row and awards the level bonus", () => {
  const game = stateWith(
    boardFromRows([
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    ]),
    { nextDisc: 7, movesRemaining: 1 },
  );
  const result = playMove(game, 0, () => 0.5);

  assert.ok(result);
  assert.equal(result.levelAdvanced, true);
  assert.equal(result.scoreDelta, LEVEL_BONUS);
  assert.equal(result.state.level, 2);
  assert.equal(result.state.movesRemaining, MOVES_PER_LEVEL);
  assert.deepEqual(
    boardToRows(result.state.board)[6],
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  );
});

test("a level-up explosion continues the fifth move's chain depth", () => {
  const game = stateWith(
    boardFromRows([
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      row(3, E, E, E, E, E, E),
      row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
    ]),
    { nextDisc: 1, movesRemaining: 1 },
  );
  const result = playMove(game, 6, () => 0.999);

  assert.ok(result);
  assert.deepEqual(
    result.waves.map((wave) => wave.depth),
    [1, 2],
  );
  assert.equal(result.scoreDelta, LEVEL_BONUS + scoreForWave(1) + scoreForWave(2));
});

test("a rising row ends the game instead of discarding an occupied top cell", () => {
  const rows = Array.from({ length: BOARD_SIZE }, blank);
  rows[0][3] = 6;
  const raised = raiseCoveredRow(boardFromRows(rows));
  assert.equal(raised, null);
});

test("the exact move model includes all seven next discs", () => {
  const game = createGame(() => 0.5);
  const outcomes = enumerateMoveOutcomes(game, 3);

  assert.equal(new Set(outcomes.map((outcome) => outcome.state.nextDisc)).size, 7);
  assert.ok(
    Math.abs(
      outcomes.reduce((sum, outcome) => sum + outcome.probability, 0) - 1,
    ) < 1e-12,
  );
});

test("streamed move outcomes preserve exact probability and expected score", () => {
  const game = stateWith(
    boardFromRows([
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      blank(),
      row(CRACKED, E, E, E, E, E, E),
    ]),
    { nextDisc: 2 },
  );
  const retained = enumerateMoveOutcomes(game, 0);
  let streamedProbability = 0;
  let streamedExpectedScore = 0;

  forEachMoveOutcome(game, 0, (outcome) => {
    streamedProbability += outcome.probability;
    streamedExpectedScore += outcome.probability * outcome.scoreDelta;
  });

  const retainedProbability = retained.reduce(
    (sum, outcome) => sum + outcome.probability,
    0,
  );
  const retainedExpectedScore = retained.reduce(
    (sum, outcome) => sum + outcome.probability * outcome.scoreDelta,
    0,
  );
  assert.ok(Math.abs(streamedProbability - retainedProbability) < 1e-12);
  assert.ok(Math.abs(streamedExpectedScore - retainedExpectedScore) < 1e-9);
});

test("seeded games remain settled and gravity-packed through game over", () => {
  for (let seed = 1; seed <= 12; seed += 1) {
    const random = seededRandom(seed);
    let game = createGame(random);
    let previousScore = game.score;

    for (let move = 0; move < 200 && !game.gameOver; move += 1) {
      const columns = legalColumns(game.board);
      const column = columns[Math.floor(random() * columns.length)];
      const result = playMove(game, column, random);
      assert.ok(result);
      game = result.state;

      assert.ok(game.score >= previousScore);
      assert.equal(findPoppers(game.board).length, 0);
      for (let columnIndex = 0; columnIndex < BOARD_SIZE; columnIndex += 1) {
        let foundDisc = false;
        for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
          const cell = game.board[rowIndex * BOARD_SIZE + columnIndex];
          if (cell !== EMPTY) foundDisc = true;
          if (foundDisc) assert.notEqual(cell, EMPTY);
        }
      }
      previousScore = game.score;
    }
  }
});
