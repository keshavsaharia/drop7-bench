/**
 * Allocation-free port of `evaluateHeuristic` (src/core/typescript/heuristic.ts)
 * for the browser solver.
 *
 * The reference leaf builds two line analyses (six 49-entry arrays), one
 * `DiscAnalysis` object and a `Map` entry per numbered disc, and several small
 * arrays per disc and per covered cell, on every one of the hundreds of
 * thousands of leaves a depth-4 search visits. Finding-13 measured the same
 * structure in C++ at ~80% of a decision. This port keeps every accumulation
 * in the reference's order and uses the same arithmetic expressions, so the
 * returned utility is the identical double; it only replaces the scans with
 * the 128-entry run tables and the per-leaf allocations with reused scratch.
 *
 * `fast-search.test.ts` asserts `Object.is(fast, reference)` on real leaves for
 * every built-in profile.
 */

import {
  CRACKED,
  EMPTY,
  SOLID,
} from "../../../src/core/typescript/engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  type HeuristicWeights,
} from "../../../src/core/typescript/heuristic.ts";
import {
  BOARD_SIZE,
  CELL_COUNT,
  POPCOUNT,
  RUN_END,
  RUN_LENGTH,
  RUN_START,
  readiness,
} from "./fast-tables.ts";

export { HEURISTIC_GAME_OVER_UTILITY };

function unionReadiness(first: number, second: number) {
  return 1 - (1 - first) * (1 - second);
}

/** Optimistic physical cost to grow a row run to exactly `value`; -1 when impossible. */
function minimumHorizontalAdditionCost(
  row: number,
  value: number,
  segmentStart: number,
  segmentEnd: number,
  segmentLength: number,
  columnHeights: Uint8Array,
) {
  if (segmentStart < 0 || value <= segmentLength) return -1;

  const elevation = BOARD_SIZE - row;
  let best = Number.POSITIVE_INFINITY;
  for (let start = 0; start + value <= BOARD_SIZE; start += 1) {
    const end = start + value - 1;
    if (start > segmentStart || end < segmentEnd) continue;
    if (start > 0 && columnHeights[start - 1] >= elevation) continue;
    if (end + 1 < BOARD_SIZE && columnHeights[end + 1] >= elevation) continue;

    let cost = 0;
    for (let column = start; column <= end; column += 1) {
      cost += Math.max(0, elevation - columnHeights[column]);
    }
    if (cost > 0 && cost < best) best = cost;
  }
  return Number.isFinite(best) ? best : -1;
}

/** Descending insertion sort of the first `count` entries, in place. */
function sortDescending(values: Float64Array, count: number) {
  for (let i = 1; i < count; i += 1) {
    const value = values[i];
    let j = i - 1;
    while (j >= 0 && values[j] < value) {
      values[j + 1] = values[j];
      j -= 1;
    }
    values[j + 1] = value;
  }
}

export class FastLeaf {
  private readonly weights: HeuristicWeights;

  private readonly rowMasks = new Uint8Array(BOARD_SIZE);
  private readonly colMasks = new Uint8Array(BOARD_SIZE);
  private readonly heights = new Uint8Array(BOARD_SIZE);
  private readonly isDisc = new Uint8Array(CELL_COUNT);
  private readonly discs = new Uint8Array(CELL_COUNT);
  private readonly hLength = new Uint8Array(CELL_COUNT);
  private readonly vLength = new Uint8Array(CELL_COUNT);
  private readonly hAddition = new Float64Array(CELL_COUNT);
  private readonly vAddition = new Float64Array(CELL_COUNT);
  private readonly addition = new Float64Array(CELL_COUNT);
  private readonly hRelease = new Float64Array(CELL_COUNT);
  private readonly vRelease = new Float64Array(CELL_COUNT);
  private readonly release = new Float64Array(CELL_COUNT);
  private readonly scratch = new Float64Array(BOARD_SIZE);

  constructor(weights: HeuristicWeights) {
    this.weights = weights;
  }

  /** Utility of a live position's board; callers handle `gameOver` themselves. */
  evaluate(board: ArrayLike<number>): number {
    const {
      rowMasks, colMasks, heights, isDisc, discs,
      hLength, vLength, hAddition, vAddition, addition,
      hRelease, vRelease, release, scratch, weights,
    } = this;

    colMasks.fill(0);
    isDisc.fill(0);
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      const offset = row * BOARD_SIZE;
      let mask = 0;
      for (let column = 0; column < BOARD_SIZE; column += 1) {
        if (board[offset + column] !== EMPTY) {
          mask |= 1 << column;
          colMasks[column] |= 1 << row;
        }
      }
      rowMasks[row] = mask;
    }
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      heights[column] = POPCOUNT[colMasks[column]];
    }

    let openColumns = 0;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      if (board[column] === EMPTY) openColumns += 1;
    }

    let heightLoad = 0;
    let solidCells = 0;
    let crackedCells = 0;
    let numberedCells = 0;
    let highLowNumbers = 0;
    let legacyNearMatches = 0;
    let directPotential = 0;
    let discCount = 0;

    for (let row = 0; row < BOARD_SIZE; row += 1) {
      const elevation = BOARD_SIZE - row;
      const rowMask = rowMasks[row];
      const rowBase = rowMask * BOARD_SIZE;
      for (let column = 0; column < BOARD_SIZE; column += 1) {
        const index = row * BOARD_SIZE + column;
        const cell = board[index];
        if (cell === EMPTY) continue;

        heightLoad += elevation * elevation;
        if (cell === SOLID) {
          solidCells += 1;
          continue;
        }
        if (cell === CRACKED) {
          crackedCells += 1;
          continue;
        }

        numberedCells += 1;
        if (cell <= 2 && elevation >= 5) highLowNumbers += 1;

        const horizontalLength = RUN_LENGTH[rowBase + column];
        const verticalLength = RUN_LENGTH[colMasks[column] * BOARD_SIZE + row];
        if (
          Math.min(
            Math.abs(cell - horizontalLength),
            Math.abs(cell - verticalLength),
          ) === 1
        ) {
          legacyNearMatches += 1;
        }

        const verticalAddition =
          cell > heights[column] ? readiness(cell - heights[column]) : 0;
        const horizontalCost = minimumHorizontalAdditionCost(
          row,
          cell,
          RUN_START[rowBase + column],
          RUN_END[rowBase + column],
          horizontalLength,
          heights,
        );
        const horizontalAddition =
          horizontalCost < 0 ? 0 : readiness(horizontalCost);
        const combined = unionReadiness(horizontalAddition, verticalAddition);

        isDisc[index] = 1;
        discs[discCount] = index;
        discCount += 1;
        hLength[index] = horizontalLength;
        vLength[index] = verticalLength;
        hAddition[index] = horizontalAddition;
        vAddition[index] = verticalAddition;
        addition[index] = combined;
        directPotential += combined;
      }
    }

    let latentChainPotential = 0;
    let deadLowNumbers = 0;
    for (let k = 0; k < discCount; k += 1) {
      const index = discs[k];
      const row = (index / BOARD_SIZE) | 0;
      const column = index - row * BOARD_SIZE;
      const value = board[index];

      // Horizontal release: supporters are the other numbered discs in this row run.
      let horizontalRelease = 0;
      const hExcess = hLength[index] - value;
      if (hExcess > 0) {
        const rowBase = rowMasks[row] * BOARD_SIZE;
        const start = RUN_START[rowBase + column];
        const end = RUN_END[rowBase + column];
        let count = 0;
        for (let c = start; c <= end; c += 1) {
          const other = row * BOARD_SIZE + c;
          if (other === index || !isDisc[other]) continue;
          scratch[count] = addition[other];
          count += 1;
        }
        if (count >= hExcess) {
          sortDescending(scratch, count);
          horizontalRelease = scratch[hExcess - 1] * readiness(hExcess);
        }
      }

      let verticalRelease = 0;
      const vExcess = vLength[index] - value;
      if (vExcess > 0) {
        const colBase = colMasks[column] * BOARD_SIZE;
        const start = RUN_START[colBase + row];
        const end = RUN_END[colBase + row];
        let count = 0;
        for (let r = start; r <= end; r += 1) {
          const other = r * BOARD_SIZE + column;
          if (other === index || !isDisc[other]) continue;
          scratch[count] = addition[other];
          count += 1;
        }
        if (count >= vExcess) {
          sortDescending(scratch, count);
          verticalRelease = scratch[vExcess - 1] * readiness(vExcess);
        }
      }

      hRelease[index] = horizontalRelease;
      vRelease[index] = verticalRelease;
      const combinedRelease = unionReadiness(horizontalRelease, verticalRelease);
      release[index] = combinedRelease;
      latentChainPotential += combinedRelease;

      if (value <= 2 && hLength[index] > value && vLength[index] > value) {
        deadLowNumbers += 1 - unionReadiness(addition[index], combinedRelease);
      }
    }

    // Adjacent ones: a pair is locked unless either disc can escape on the
    // perpendicular axis.
    let adjacentOnes = 0;
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      for (let column = 0; column < BOARD_SIZE; column += 1) {
        const index = row * BOARD_SIZE + column;
        if (board[index] !== 1) continue;
        if (column + 1 < BOARD_SIZE && board[index + 1] === 1) {
          const a = unionReadiness(vAddition[index], vRelease[index]);
          const b = unionReadiness(vAddition[index + 1], vRelease[index + 1]);
          adjacentOnes += 1 - Math.max(0, a, b);
        }
        if (row + 1 < BOARD_SIZE && board[index + BOARD_SIZE] === 1) {
          const a = unionReadiness(hAddition[index], hRelease[index]);
          const b = unionReadiness(
            hAddition[index + BOARD_SIZE],
            hRelease[index + BOARD_SIZE],
          );
          adjacentOnes += 1 - Math.max(0, a, b);
        }
      }
    }

    // Runs of twos longer than two, horizontal lines first, then vertical.
    let tripleTwos = 0;
    for (let axis = 0; axis < 2; axis += 1) {
      for (let fixed = 0; fixed < BOARD_SIZE; fixed += 1) {
        let cursor = 0;
        while (cursor < BOARD_SIZE) {
          const index =
            axis === 0
              ? fixed * BOARD_SIZE + cursor
              : cursor * BOARD_SIZE + fixed;
          if (board[index] !== 2) {
            cursor += 1;
            continue;
          }
          const start = cursor;
          let escape = 0;
          while (cursor < BOARD_SIZE) {
            const runIndex =
              axis === 0
                ? fixed * BOARD_SIZE + cursor
                : cursor * BOARD_SIZE + fixed;
            if (board[runIndex] !== 2) break;
            const perpendicular =
              axis === 0
                ? unionReadiness(vAddition[runIndex], vRelease[runIndex])
                : unionReadiness(hAddition[runIndex], hRelease[runIndex]);
            if (perpendicular > escape) escape = perpendicular;
            cursor += 1;
          }
          const excess = cursor - start - 2;
          if (excess <= 0) continue;
          tripleTwos += excess * excess * (1 - escape);
        }
      }
    }

    // Cover exposure: how plausibly the numbered neighbours of each gray disc
    // can fire, best neighbours first.
    let crackedExposure = 0;
    let solidExposure = 0;
    for (let index = 0; index < CELL_COUNT; index += 1) {
      const cell = board[index];
      if (cell !== SOLID && cell !== CRACKED) continue;
      const row = (index / BOARD_SIZE) | 0;
      const column = index - row * BOARD_SIZE;
      let count = 0;
      if (row > 0 && isDisc[index - BOARD_SIZE]) {
        scratch[count] = unionReadiness(addition[index - BOARD_SIZE], release[index - BOARD_SIZE]);
        count += 1;
      }
      if (row + 1 < BOARD_SIZE && isDisc[index + BOARD_SIZE]) {
        scratch[count] = unionReadiness(addition[index + BOARD_SIZE], release[index + BOARD_SIZE]);
        count += 1;
      }
      if (column > 0 && isDisc[index - 1]) {
        scratch[count] = unionReadiness(addition[index - 1], release[index - 1]);
        count += 1;
      }
      if (column + 1 < BOARD_SIZE && isDisc[index + 1]) {
        scratch[count] = unionReadiness(addition[index + 1], release[index + 1]);
        count += 1;
      }
      sortDescending(scratch, count);
      if (cell === CRACKED) {
        let inverse = 1;
        for (let k = 0; k < count; k += 1) inverse *= 1 - scratch[k];
        crackedExposure += 1 - inverse;
      } else {
        solidExposure +=
          (count > 0 ? scratch[0] : 0) * 0.35 +
          (count > 1 ? scratch[1] : 0) * 0.65;
      }
    }

    // Same key order as the reference's FEATURE_KEYS, so the sum is bit-identical.
    let utility = 0;
    utility += openColumns * weights.openColumns;
    utility += heightLoad * weights.heightLoad;
    utility += solidCells * weights.solidCells;
    utility += crackedCells * weights.crackedCells;
    utility += numberedCells * weights.numberedCells;
    utility += highLowNumbers * weights.highLowNumbers;
    utility += legacyNearMatches * weights.legacyNearMatches;
    utility += directPotential * weights.directPotential;
    utility += latentChainPotential * weights.latentChainPotential;
    utility += crackedExposure * weights.crackedExposure;
    utility += solidExposure * weights.solidExposure;
    utility += adjacentOnes * weights.adjacentOnes;
    utility += tripleTwos * weights.tripleTwos;
    utility += deadLowNumbers * weights.deadLowNumbers;
    return utility;
  }
}
