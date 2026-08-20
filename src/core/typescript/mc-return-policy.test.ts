import assert from "node:assert/strict";
import test from "node:test";
import {
  BOARD_SIZE,
  EMPTY,
  MOVES_PER_LEVEL,
  SOLID,
  boardFromRows,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  CompiledMcReturnPolicy,
  MC_RETURN_FEATURE_SIZE,
  MC_RETURN_FORMAT,
  MC_RETURN_VERSION,
  TrainableMcReturnNetwork,
  chooseMcReturnMove,
  encodeMcReturnAction,
  type McReturnNetworkSnapshot,
} from "./mc-return-policy.ts";

const E = EMPTY;
const row = (...cells: Cell[]) => cells;
const blank = () => row(E, E, E, E, E, E, E);

function position(board: Board): GameState {
  return {
    board,
    nextDisc: 5,
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
}

function zeroSnapshot(): McReturnNetworkSnapshot {
  const hiddenOne = 4;
  const hiddenTwo = 2;
  return {
    inputSize: MC_RETURN_FEATURE_SIZE,
    hiddenOne,
    hiddenTwo,
    weightsOne: Array<number>(MC_RETURN_FEATURE_SIZE * hiddenOne).fill(0.01),
    biasesOne: Array<number>(hiddenOne).fill(0.01),
    weightsTwo: Array<number>(hiddenOne * hiddenTwo).fill(0.01),
    biasesTwo: Array<number>(hiddenTwo).fill(0.01),
    weightsThree: Array<number>(hiddenTwo).fill(0.01),
    biasThree: 0,
  };
}

test("MC-return action inputs are observable and mirror exact", () => {
  const board = boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    row(E, E, 2, E, E, E, E),
    row(4, E, 6, E, E, E, E),
    row(SOLID, E, 6, 7, E, E, E),
  ]);
  const forward = encodeMcReturnAction(position(board), 1, 2, 123);
  const mirrored = encodeMcReturnAction(
    position(mirrorBoard(board)),
    BOARD_SIZE - 1 - 1,
    2,
    123,
  );

  assert.equal(forward.length, MC_RETURN_FEATURE_SIZE);
  assert.deepEqual(mirrored, forward);
  assert.ok([...forward].every(Number.isFinite));
});

test("the compact network learns a complete-return target", () => {
  const state = position(boardFromRows([
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    blank(),
    row(SOLID, SOLID, SOLID, SOLID, SOLID, SOLID, SOLID),
  ]));
  const input = encodeMcReturnAction(state, 3, 1, 123);
  const network = new TrainableMcReturnNetwork(zeroSnapshot());
  const before = network.value(input);
  for (let step = 0; step < 100; step += 1) {
    network.trainBatch([{ input, target: 5 }], 0.003);
  }
  const after = network.value(input);

  assert.ok(Math.abs(after - 5) < Math.abs(before - 5));
  assert.ok(network.byteLength() > 0);
  assert.equal(network.snapshot().inputSize, MC_RETURN_FEATURE_SIZE);
});

test("compiled MC-return policies mask full columns and validate artifacts", () => {
  const board = boardFromRows([
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
    row(E, E, E, SOLID, E, E, E),
  ]);
  const state = position(board);
  const artifact = {
    format: MC_RETURN_FORMAT,
    version: MC_RETURN_VERSION,
    algorithm: "undiscounted-monte-carlo-return" as const,
    observableOnly: true as const,
    options: { samples: 1, policySeed: 123 },
    network: zeroSnapshot(),
  };
  const policy = new CompiledMcReturnPolicy(artifact);
  const column = policy.chooseMove(state);

  assert.notEqual(column, 3);
  assert.equal(policy.evaluateActions(state).length, BOARD_SIZE - 1);
  assert.notEqual(
    chooseMcReturnMove(state, new TrainableMcReturnNetwork(zeroSnapshot())),
    3,
  );
  assert.throws(
    () =>
      new CompiledMcReturnPolicy({
        ...artifact,
        network: { ...artifact.network, weightsThree: [Number.NaN, 0] },
      }),
    /weightsThree/,
  );
});

function mirrorBoard(board: Board): Board {
  const result: Cell[] = [];
  for (let rowIndex = 0; rowIndex < BOARD_SIZE; rowIndex += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      result.push(board[rowIndex * BOARD_SIZE + column]);
    }
  }
  return result;
}
