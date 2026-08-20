import assert from "node:assert/strict";
import test from "node:test";
import {
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  type Board,
  type Cell,
} from "./engine.ts";
import { evaluateHeuristic } from "./heuristic.ts";
import {
  COLUMN_HEIGHT_TOKEN_OFFSET,
  LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
  LEARNED_EVALUATOR_ACTIVE_TOKEN_COUNT,
  LEARNED_EVALUATOR_CELL_KINDS,
  LEARNED_EVALUATOR_HIDDEN_SIZE,
  LEARNED_POLICY_ACCUMULATOR_SIZE,
  LEARNED_POLICY_ACTIVE_TOKEN_COUNT,
  LEARNED_POLICY_HIDDEN_SIZE,
  MOVES_REMAINING_TOKEN_OFFSET,
  NEXT_DISC_TOKEN_OFFSET,
  compileLearnedEvaluatorWeights,
  compileLearnedPolicyWeights,
  createRandomLearnedEvaluatorWeights,
  createZeroLearnedEvaluatorWeights,
  createZeroLearnedPolicyWeights,
  evaluateLearnedPolicy,
  evaluateLearnedPosition,
  extractLearnedEvaluatorTokens,
  extractLearnedPolicyTokens,
  type SerializedLearnedPolicyWeights,
  type SerializedLearnedEvaluatorWeights,
} from "./learned-evaluator.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);
const empty = () => boardFromRows(Array.from({ length: 7 }, blank));

test("token extraction includes empty squares and both context families", () => {
  const { tokenIds, mirrored } = extractLearnedEvaluatorTokens({
    board: empty(),
    movesRemaining: 3,
  });

  assert.equal(mirrored, false);
  assert.equal(tokenIds.length, LEARNED_EVALUATOR_ACTIVE_TOKEN_COUNT);
  assert.equal(tokenIds[0], 0);
  assert.equal(tokenIds[1], LEARNED_EVALUATOR_CELL_KINDS);
  assert.equal(
    tokenIds[48],
    48 * LEARNED_EVALUATOR_CELL_KINDS,
    "the empty token remains active for the final square",
  );
  assert.equal(tokenIds[49], MOVES_REMAINING_TOKEN_OFFSET + 3);
  for (let column = 0; column < 7; column += 1) {
    assert.equal(
      tokenIds[50 + column],
      COLUMN_HEIGHT_TOKEN_OFFSET + column * 8,
    );
  }
});

test("policy tokens add the visible next disc to the canonical position", () => {
  const first = extractLearnedPolicyTokens({
    board: empty(),
    movesRemaining: 3,
    nextDisc: 1,
  });
  const fifth = extractLearnedPolicyTokens({
    board: empty(),
    movesRemaining: 3,
    nextDisc: 5,
  });

  assert.equal(first.tokenIds.length, LEARNED_POLICY_ACTIVE_TOKEN_COUNT);
  assert.deepEqual(
    first.tokenIds.slice(0, -1),
    fifth.tokenIds.slice(0, -1),
  );
  assert.equal(first.tokenIds.at(-1), NEXT_DISC_TOKEN_OFFSET);
  assert.equal(fifth.tokenIds.at(-1), NEXT_DISC_TOKEN_OFFSET + 4);
});

test("the Float32 network matches a hand-computed golden vector", () => {
  const artifact = mutableZeroWeights();
  const positionToken = 0;
  const movesToken = MOVES_REMAINING_TOKEN_OFFSET + MOVES_PER_LEVEL;
  const firstColumnHeightToken = COLUMN_HEIGHT_TOKEN_OFFSET;

  artifact.accumulatorBias[0] = 1;
  artifact.accumulatorBias[3] = -2;
  artifact.embedding[
    positionToken * LEARNED_EVALUATOR_ACCUMULATOR_SIZE
  ] = 2;
  artifact.embedding[
    movesToken * LEARNED_EVALUATOR_ACCUMULATOR_SIZE + 1
  ] = 4;
  artifact.embedding[
    firstColumnHeightToken * LEARNED_EVALUATOR_ACCUMULATOR_SIZE + 2
  ] = 5;

  artifact.hiddenBias[0] = 1;
  artifact.hiddenWeights[0] = 2;
  artifact.hiddenWeights[1] = -1;
  artifact.hiddenWeights[2] = 1;
  artifact.hiddenWeights[3] = 100;
  artifact.hiddenBias[1] = -10;
  artifact.hiddenWeights[LEARNED_EVALUATOR_ACCUMULATOR_SIZE + 2] = 1;
  artifact.outputWeights[0] = 2;
  artifact.outputWeights[1] = 100;
  artifact.outputBias = 3;

  // accumulator = ReLU([3, 4, 5, -2]); hidden = ReLU([8, -5]);
  // output = 3 + 8 * 2 = 19.
  assert.equal(
    evaluateLearnedPosition(
      { board: empty(), movesRemaining: MOVES_PER_LEVEL },
      compileLearnedEvaluatorWeights(artifact),
    ),
    19,
  );
});

test("moves remaining and column heights independently reach inference", () => {
  const artifact = mutableZeroWeights();
  for (let moves = 0; moves <= MOVES_PER_LEVEL; moves += 1) {
    artifact.embedding[
      (MOVES_REMAINING_TOKEN_OFFSET + moves) *
        LEARNED_EVALUATOR_ACCUMULATOR_SIZE
    ] = moves;
  }
  for (let column = 0; column < 7; column += 1) {
    for (let height = 0; height <= 7; height += 1) {
      artifact.embedding[
        (COLUMN_HEIGHT_TOKEN_OFFSET + column * 8 + height) *
          LEARNED_EVALUATOR_ACCUMULATOR_SIZE
      ] = height;
    }
  }
  artifact.hiddenWeights[0] = 1;
  artifact.outputWeights[0] = 1;
  const model = compileLearnedEvaluatorWeights(artifact);

  const occupied = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, E, E, E, E, 2),
    row(1, E, 3, E, E, E, SOLID),
  ]);

  assert.equal(
    evaluateLearnedPosition({ board: empty(), movesRemaining: 1 }, model),
    1,
  );
  assert.equal(
    evaluateLearnedPosition({ board: occupied, movesRemaining: 3 }, model),
    7,
    "three context moves plus four occupied cells",
  );
});

test("the direct policy routes features to logits and masks full columns", () => {
  const artifact = mutableZeroPolicyWeights();
  const nextDiscToken = NEXT_DISC_TOKEN_OFFSET + 3;
  artifact.accumulatorBias[0] = 1;
  artifact.embedding[
    nextDiscToken * LEARNED_POLICY_ACCUMULATOR_SIZE
  ] = 2;
  artifact.hiddenWeights[0] = 3;
  artifact.outputWeights[5 * LEARNED_POLICY_HIDDEN_SIZE] = 2;
  const model = compileLearnedPolicyWeights(artifact);

  const evaluation = evaluateLearnedPolicy(
    { board: empty(), movesRemaining: 5, nextDisc: 4 },
    model,
  );
  assert.equal(evaluation.logits[5], 18);
  assert.equal(evaluation.bestColumn, 5);

  const blocked: Cell[] = [...empty()];
  blocked[5] = SOLID;
  assert.equal(
    evaluateLearnedPolicy(
      { board: blocked, movesRemaining: 5, nextDisc: 4 },
      model,
    ).bestColumn,
    3,
    "the maximum-logit column is ignored when its top cell is occupied",
  );
});

test("direct-policy logits and choices reflect exactly with the board", () => {
  const board: Cell[] = [...empty()];
  board[42] = 1;
  const reflected = mirrorBoard(board);
  const artifact = mutableZeroPolicyWeights();
  for (let column = 0; column < 7; column += 1) {
    artifact.outputBias[column] = column;
  }
  const model = compileLearnedPolicyWeights(artifact);
  const forward = evaluateLearnedPolicy(
    { board, movesRemaining: 2, nextDisc: 7 },
    model,
  );
  const mirror = evaluateLearnedPolicy(
    { board: reflected, movesRemaining: 2, nextDisc: 7 },
    model,
  );

  assert.deepEqual([...forward.logits], [...mirror.logits].reverse());
  assert.equal(forward.bestColumn, 0);
  assert.equal(mirror.bestColumn, 6);
});

test("residual artifacts preserve the combined baseline and enforce safety bounds", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    row(E, E, E, 4, E, E, E),
    row(E, E, 2, SOLID, E, E, E),
    row(E, 3, 5, SOLID, E, E, E),
    row(1, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]);
  const state = {
    board,
    movesRemaining: 2,
    nextDisc: 3 as const,
    score: 91_000,
    level: 4,
    movesPlayed: 17,
    gameOver: false,
  };
  const expectedBaseline = evaluateHeuristic(state, "combined");
  const artifact = mutableZeroWeights();
  artifact.baseline = "combined";
  artifact.residualMinimum = -25;
  artifact.residualMaximum = 10;
  artifact.outputBias = -100;

  assert.equal(
    evaluateLearnedPosition(state, compileLearnedEvaluatorWeights(artifact)),
    expectedBaseline - 25,
  );

  artifact.outputBias = 100;
  assert.equal(
    evaluateLearnedPosition(state, compileLearnedEvaluatorWeights(artifact)),
    expectedBaseline + 10,
  );
});

test("horizontal mirrors have identical tokens and exact model values", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    row(E, E, E, 7, E, E, E),
    row(E, 4, E, 6, E, E, E),
    row(E, 2, E, SOLID, E, 5, E),
    row(1, 3, E, 2, E, 6, E),
    row(4, 5, 7, 3, E, 1, 2),
  ]);
  const reflected = mirrorBoard(board);
  const left = extractLearnedEvaluatorTokens({
    board,
    movesRemaining: 2,
  });
  const right = extractLearnedEvaluatorTokens({
    board: reflected,
    movesRemaining: 2,
  });

  assert.notEqual(left.mirrored, right.mirrored);
  assert.deepEqual(left.tokenIds, right.tokenIds);

  const artifact = {
    ...createRandomLearnedEvaluatorWeights(0x7a11_ce55),
    baseline: "combined" as const,
  };
  const model = compileLearnedEvaluatorWeights(
    JSON.parse(JSON.stringify(artifact)),
  );
  const forwardValue = evaluateLearnedPosition(
    { board, movesRemaining: 2 },
    model,
  );
  const reflectedValue = evaluateLearnedPosition(
    { board: reflected, movesRemaining: 2 },
    model,
  );
  assert.equal(forwardValue, reflectedValue);
});

test("serialized artifacts and evaluator positions are validated", () => {
  const valid = createZeroLearnedEvaluatorWeights();
  assert.doesNotThrow(() => compileLearnedEvaluatorWeights(valid));

  assert.throws(
    () => compileLearnedEvaluatorWeights({ ...valid, version: 2 }),
    /version must be 1/,
  );
  assert.throws(
    () =>
      compileLearnedEvaluatorWeights({
        ...valid,
        hiddenWeights: valid.hiddenWeights.slice(1),
      }),
    /hiddenWeights must contain 128 values/,
  );
  const nonFinite = [...valid.outputWeights];
  nonFinite[LEARNED_EVALUATOR_HIDDEN_SIZE - 1] = Number.NaN;
  assert.throws(
    () =>
      compileLearnedEvaluatorWeights({
        ...valid,
        outputWeights: nonFinite,
      }),
    /outputWeights\[7\] must be a finite Float32/,
  );
  assert.throws(
    () =>
      compileLearnedEvaluatorWeights({
        ...valid,
        accumulatorBias: new Float32Array(
          LEARNED_EVALUATOR_ACCUMULATOR_SIZE,
        ),
      }),
    /accumulatorBias must be a JSON array/,
  );
  assert.throws(
    () => compileLearnedEvaluatorWeights({ ...valid, baseline: "legacy" }),
    /baseline must be combined/,
  );
  assert.throws(
    () =>
      compileLearnedEvaluatorWeights({
        ...valid,
        residualMinimum: 5,
        residualMaximum: -5,
      }),
    /residualMinimum cannot exceed residualMaximum/,
  );

  assert.throws(
    () =>
      extractLearnedEvaluatorTokens({
        board: empty(),
        movesRemaining: MOVES_PER_LEVEL + 1,
      }),
    /movesRemaining must be an integer from 0 through 5/,
  );
  const invalidBoard = [...empty()];
  invalidBoard[12] = 10 as Cell;
  assert.throws(
    () =>
      extractLearnedEvaluatorTokens({
        board: invalidBoard,
        movesRemaining: 1,
      }),
    /board cell 12 must be an integer from 0 through 9/,
  );

  const validPolicy = createZeroLearnedPolicyWeights();
  assert.doesNotThrow(() => compileLearnedPolicyWeights(validPolicy));
  assert.throws(
    () =>
      compileLearnedPolicyWeights({
        ...validPolicy,
        outputBias: validPolicy.outputBias.slice(1),
      }),
    /policy outputBias must contain 7 values/,
  );
  assert.throws(
    () =>
      extractLearnedPolicyTokens({
        board: empty(),
        movesRemaining: 2,
        nextDisc: 0 as 1,
      }),
    /nextDisc must be an integer from 1 through 7/,
  );
});

interface MutableSerializedWeights {
  format: SerializedLearnedEvaluatorWeights["format"];
  version: SerializedLearnedEvaluatorWeights["version"];
  baseline?: "combined";
  residualMinimum?: number;
  residualMaximum?: number;
  embedding: number[];
  accumulatorBias: number[];
  hiddenWeights: number[];
  hiddenBias: number[];
  outputWeights: number[];
  outputBias: number;
}

function mutableZeroWeights(): MutableSerializedWeights {
  const weights = createZeroLearnedEvaluatorWeights();
  return {
    ...weights,
    embedding: [...weights.embedding],
    accumulatorBias: [...weights.accumulatorBias],
    hiddenWeights: [...weights.hiddenWeights],
    hiddenBias: [...weights.hiddenBias],
    outputWeights: [...weights.outputWeights],
  };
}

interface MutableSerializedPolicyWeights {
  format: SerializedLearnedPolicyWeights["format"];
  version: SerializedLearnedPolicyWeights["version"];
  embedding: number[];
  accumulatorBias: number[];
  hiddenWeights: number[];
  hiddenBias: number[];
  outputWeights: number[];
  outputBias: number[];
}

function mutableZeroPolicyWeights(): MutableSerializedPolicyWeights {
  const weights = createZeroLearnedPolicyWeights();
  return {
    ...weights,
    embedding: [...weights.embedding],
    accumulatorBias: [...weights.accumulatorBias],
    hiddenWeights: [...weights.hiddenWeights],
    hiddenBias: [...weights.hiddenBias],
    outputWeights: [...weights.outputWeights],
    outputBias: [...weights.outputBias],
  };
}

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let rowIndex = 0; rowIndex < 7; rowIndex += 1) {
    const start = rowIndex * 7;
    mirrored.push(...board.slice(start, start + 7).reverse());
  }
  return mirrored;
}
