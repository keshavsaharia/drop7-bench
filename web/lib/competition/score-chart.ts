import {
  LEVEL_BONUS,
  MOVES_PER_LEVEL,
} from "../../../src/core/typescript/engine.ts";

export type ScoreChartAggregation = "move" | "round";

export interface ScoreChartFrame {
  move: number;
  scoreDelta: number;
  levelAdvanced: boolean;
}

export interface ExplosionScoreBar {
  startMove: number;
  endMove: number;
  round: number;
  points: number;
}

/** Keep cascade and board-clear points while removing the flat survival bonus. */
export function explosionPointsForFrame(frame: ScoreChartFrame) {
  return frame.scoreDelta - (frame.levelAdvanced ? LEVEL_BONUS : 0);
}

export function buildExplosionScoreBars(
  frames: readonly ScoreChartFrame[],
  aggregation: ScoreChartAggregation,
  cumulative = false,
): ExplosionScoreBar[] {
  const bars =
    aggregation === "move"
      ? frames.map((frame) => ({
          startMove: frame.move,
          endMove: frame.move,
          round: roundForMove(frame.move),
          points: explosionPointsForFrame(frame),
        }))
      : aggregateRounds(frames);

  if (!cumulative) return bars;

  let total = 0;
  return bars.map((bar) => {
    total += bar.points;
    return { ...bar, points: total };
  });
}

function aggregateRounds(frames: readonly ScoreChartFrame[]) {
  const rounds = new Map<number, ExplosionScoreBar>();

  for (const frame of frames) {
    const round = roundForMove(frame.move);
    const current = rounds.get(round);
    if (current) {
      current.endMove = frame.move;
      current.points += explosionPointsForFrame(frame);
      continue;
    }

    rounds.set(round, {
      startMove: frame.move,
      endMove: frame.move,
      round,
      points: explosionPointsForFrame(frame),
    });
  }

  return [...rounds.values()];
}

function roundForMove(move: number) {
  return Math.floor((move - 1) / MOVES_PER_LEVEL) + 1;
}
