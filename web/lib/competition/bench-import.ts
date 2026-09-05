import {
  trajectoryChecksum,
  type BenchGameResult,
} from "../../../src/bench/runner.ts";
import type { CompetitionGameDefinition } from "./game.ts";
import {
  replayCompetitionColumns,
  type CompetitionReplayResult,
} from "./replay.ts";

/** One policy game verified against the competition's immutable round. */
export interface VerifiedSeedGame {
  policyId: string;
  columns: number[];
  replay: CompetitionReplayResult;
  /** Trajectory checksum of the verified game, in the bench's sixteen-hex form. */
  checksum: string;
  /** The score reported by the harness that produced the game. */
  clientScore: number;
}

type BenchReplayFile = Pick<
  BenchGameResult,
  "policyId" | "roundId" | "score" | "moves" | "censored" | "illegalMoves" | "checksum"
> & { frames: { column: number; score: number; board: string }[] };

const SLUG = /^[a-z0-9][a-z0-9-]*$/;

function invalid(reason: string): never {
  throw new Error(`Not a drop7 leaderboard replay: ${reason}`);
}

function nonNegativeInteger(value: unknown, field: string): number {
  if (!Number.isInteger(value) || (value as number) < 0) invalid(field);
  return value as number;
}

/** Structural check of a replay file written by `npm run bench`; nothing here is trusted yet. */
function asBenchReplay(value: unknown): BenchReplayFile {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    invalid("expected a JSON object");
  }
  const record = value as Record<string, unknown>;
  if (typeof record.policyId !== "string" || !SLUG.test(record.policyId)) invalid("policyId");
  if (typeof record.roundId !== "string" || !SLUG.test(record.roundId)) invalid("roundId");
  if (typeof record.censored !== "boolean") invalid("censored");
  if (typeof record.checksum !== "string" || !/^[a-f0-9]{16}$/.test(record.checksum)) {
    invalid("checksum");
  }
  if (!Array.isArray(record.frames)) invalid("frames");
  const frames = record.frames.map((frame, index) => {
    if (!frame || typeof frame !== "object") invalid(`frame ${index + 1}`);
    const { column, score, board } = frame as Record<string, unknown>;
    if (!Number.isInteger(column) || (column as number) < 0 || (column as number) > 6) {
      invalid(`frame ${index + 1} column`);
    }
    if (typeof board !== "string" || board.length !== 49) invalid(`frame ${index + 1} board`);
    return {
      column: column as number,
      score: nonNegativeInteger(score, `frame ${index + 1} score`),
      board,
    };
  });
  return {
    policyId: record.policyId,
    roundId: record.roundId,
    score: nonNegativeInteger(record.score, "score"),
    moves: nonNegativeInteger(record.moves, "moves"),
    censored: record.censored,
    illegalMoves: nonNegativeInteger(record.illegalMoves, "illegalMoves"),
    checksum: record.checksum,
    frames,
  };
}

/**
 * Verifies a replay written by `npm run bench` (or `just play-round`) on any
 * machine against the competition's immutable round. Only the recorded column
 * choices are taken as input: they are replayed here from the opening position,
 * and that replay must reproduce the recorded score, move count, censor flag,
 * every per-move board, and the trajectory checksum. The policy's own search is
 * never re-run, so a multi-hour game imports in seconds. Nothing here attests
 * that the registered policy would choose these columns again; that provenance
 * is the source revision the caller records beside the result.
 */
export function importBenchReplay(
  game: CompetitionGameDefinition,
  value: unknown,
): VerifiedSeedGame {
  const recorded = asBenchReplay(value);
  const { policyId } = recorded;
  if (recorded.roundId !== game.manifest.roundId) {
    throw new Error(
      `${policyId} replay is a ${recorded.roundId} game, but ${game.gameKey} plays ${game.manifest.roundId}`,
    );
  }
  if (recorded.illegalMoves !== 0) {
    throw new Error(`${policyId} made ${recorded.illegalMoves} illegal choices`);
  }
  if (recorded.frames.length !== recorded.moves) {
    throw new Error(
      `${policyId} replay records ${recorded.frames.length} frames for ${recorded.moves} moves`,
    );
  }
  const columns = recorded.frames.map((frame) => frame.column);
  const replay = replayCompetitionColumns(game.round, columns);
  if (
    !replay.valid ||
    replay.score !== recorded.score ||
    replay.moves !== recorded.moves ||
    replay.censored !== recorded.censored
  ) {
    throw new Error(`${policyId} failed the independent competition replay`);
  }
  for (let index = 0; index < recorded.frames.length; index += 1) {
    const expected = recorded.frames[index];
    const actual = replay.frames[index];
    if (actual.board !== expected.board || actual.score !== expected.score) {
      throw new Error(
        `${policyId} replay diverges from its recording at move ${index + 1}`,
      );
    }
  }
  const checksum = trajectoryChecksum(replay.frames);
  if (checksum !== recorded.checksum) {
    throw new Error(
      `${policyId} replay checksum ${checksum} does not match the recorded ${recorded.checksum}`,
    );
  }
  return { policyId, columns, replay, checksum, clientScore: recorded.score };
}
