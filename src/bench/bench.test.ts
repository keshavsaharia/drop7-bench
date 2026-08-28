import assert from "node:assert/strict";
import test from "node:test";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  type GameState,
} from "../core/typescript/engine.ts";
import {
  BENCH_POLICIES,
  COMPETITION_POLICY_IDS,
  getPolicy,
} from "./policies.ts";
import { playScriptedGame, type BenchCheckpointEntry } from "./runner.ts";
import { validateScriptedRound } from "./rounds.ts";
import { parsePosition, stateFromPosition } from "./d7p-server.ts";
import {
  NATIVE_DECIDE_BINARY,
  RUST_DECIDE_BINARY,
  nativeBinaryAvailable,
  nativeDecide,
} from "./native-policy.ts";

const BENCH_DIR = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = join(BENCH_DIR, "..", "..");
const ROUNDS_DIR = join(BENCH_DIR, "rounds");

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

test("competition policies are public and link to checked-in approach pages", () => {
  assert.ok(COMPETITION_POLICY_IDS.includes("expectimax-d4"));
  for (const policy of BENCH_POLICIES) {
    const relative = policy.researchPath.replace(/^\//, "");
    assert.ok(
      existsSync(join(REPO_ROOT, relative, "README.mdx")),
      `${policy.id} research page exists`,
    );
  }
  for (const policyId of COMPETITION_POLICY_IDS) {
    assert.equal(getPolicy(policyId).publicInformation, true);
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

test("replay capture serializes each complete move animation", () => {
  const round = loadRound("gauntlet-01");
  const game = playScriptedGame(getPolicy("greedy"), round, {
    captureAnimation: true,
  });

  for (const frame of game.frames) {
    assert.equal(frame.animation?.[0]?.kind, "drop");
    assert.equal(frame.animation?.[0]?.board, frame.placedBoard);
    assert.equal(frame.animation?.at(-1)?.board, frame.board);
  }
  const levelFrames = game.frames.filter((frame) => frame.levelAdvanced);
  assert.ok(levelFrames.length > 0);
  assert.equal(
    levelFrames.every((frame) =>
      frame.animation?.some((animationFrame) => animationFrame.kind === "rise"),
    ),
    true,
  );
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
    researchPath: "/approaches/heuristic-search/policy-comparison" as const,
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

test("a checkpointed game resumes into an identical final result", () => {
  const round = loadRound("gauntlet-01");
  const policy = getPolicy("greedy");
  const journal: BenchCheckpointEntry[] = [];
  const full = playScriptedGame(policy, round, {
    onFrame: (_frame, entry, resumed) => {
      assert.equal(resumed, false, "no move is resumed in a fresh game");
      journal.push(entry);
    },
  });
  assert.equal(journal.length, full.moves);

  const prefix = journal.slice(0, 10);
  let resumedMoves = 0;
  const resumed = playScriptedGame(policy, round, {
    resume: prefix,
    onFrame: (_frame, _entry, wasResumed) => {
      if (wasResumed) resumedMoves += 1;
    },
  });
  assert.equal(resumedMoves, prefix.length, "the journal prefix is replayed");
  assert.equal(resumed.checksum, full.checksum);
  assert.equal(resumed.score, full.score);
  assert.equal(resumed.moves, full.moves);
  assert.equal(resumed.illegalMoves, full.illegalMoves);
});

test("a checkpoint from different code or round is rejected, not continued", () => {
  const round = loadRound("gauntlet-01");
  const policy = getPolicy("greedy");
  const journal: BenchCheckpointEntry[] = [];
  playScriptedGame(policy, round, {
    onFrame: (_frame, entry) => journal.push(entry),
  });
  const corrupted = journal.slice(0, 10);
  corrupted[5] = {
    ...corrupted[5],
    board: `${corrupted[5].board.slice(0, -1)}${corrupted[5].board.endsWith("7") ? "6" : "7"}`,
  };
  assert.throws(
    () => playScriptedGame(policy, round, { resume: corrupted }),
    /checkpoint mismatch/,
  );
});

test(
  "the native decision binary, when built, answers a public position deterministically",
  { skip: !nativeBinaryAvailable() && `${NATIVE_DECIDE_BINARY} is not built` },
  () => {
    const state: GameState = stateFromPosition(
      parsePosition(["startpos", "next", "4", "rise", "5"]),
    );
    const policy = getPolicy("native-fair-d4-s7");
    const first = policy.chooseColumn(state);
    const second = policy.chooseColumn(state);
    assert.equal(first, second, "same public state, same column");
    assert.ok(
      first !== null && first >= 0 && first < BOARD_SIZE,
      "native policy answered with a column",
    );
    assert.equal(policy.publicInformation, true);
  },
);

test(
  "the Rust decision binary, when built, answers a public position deterministically",
  { skip: !nativeBinaryAvailable(RUST_DECIDE_BINARY) && `${RUST_DECIDE_BINARY} is not built` },
  () => {
    const state: GameState = stateFromPosition(
      parsePosition(["startpos", "next", "4", "rise", "5"]),
    );
    // Depth 2 keeps the suite fast; the registered policy's depth-7
    // configuration is the same binary and code path with a larger budget.
    const decide = (s: GameState) =>
      nativeDecide(s, { binary: RUST_DECIDE_BINARY, depth: 2, chanceSamples: 7 });
    const first = decide(state);
    const second = decide(state);
    assert.equal(first, second, "same public state, same column");
    assert.ok(
      first !== null && first >= 0 && first < BOARD_SIZE,
      "rust policy answered with a column",
    );
    for (const id of ["rust-fair-d6-s7", "rust-fair-d7-s7"]) {
      const policy = getPolicy(id);
      assert.equal(policy.publicInformation, true);
      assert.equal(policy.family, "fair-expectimax");
      // Terminal short-circuit still evaluates the policy closure, so a missing
      // RUST_DECIDE_BINARY import fails here instead of only in a live game.
      assert.equal(policy.chooseColumn({ ...state, gameOver: true }), null);
    }
    // A decision that overruns its wall-clock budget reports the budget and
    // position instead of surfacing a raw spawnSync ETIMEDOUT.
    assert.throws(
      () =>
        nativeDecide(state, {
          binary: RUST_DECIDE_BINARY,
          depth: 4,
          chanceSamples: 7,
          timeoutMs: 1,
        }),
      /exceeded its 0s budget at depth 4/,
    );
  },
);
