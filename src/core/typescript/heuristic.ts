import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  SOLID,
  isNumbered,
  type Board,
  type GameState,
} from "./engine.ts";

export type HeuristicProfileName =
  | "legacy"
  | "survival"
  | "potential"
  | "anti-clog"
  | "combined";

export const DEFAULT_HEURISTIC_PROFILE: HeuristicProfileName = "combined";
export const HEURISTIC_GAME_OVER_UTILITY = -250_000;

export interface HeuristicFeatures {
  openColumns: number;
  heightLoad: number;
  solidCells: number;
  crackedCells: number;
  numberedCells: number;
  highLowNumbers: number;
  legacyNearMatches: number;
  directPotential: number;
  latentChainPotential: number;
  crackedExposure: number;
  solidExposure: number;
  adjacentOnes: number;
  tripleTwos: number;
  deadLowNumbers: number;
}

export type HeuristicWeights = Readonly<Record<keyof HeuristicFeatures, number>>;

const SURVIVAL_WEIGHTS = {
  openColumns: 180,
  heightLoad: -10,
  solidCells: -620,
  crackedCells: -220,
  numberedCells: -18,
  highLowNumbers: -90,
  legacyNearMatches: 0,
  directPotential: 0,
  latentChainPotential: 0,
  crackedExposure: 0,
  solidExposure: 0,
  adjacentOnes: 0,
  tripleTwos: 0,
  deadLowNumbers: 0,
} satisfies HeuristicWeights;

const POTENTIAL_WEIGHTS = {
  ...SURVIVAL_WEIGHTS,
  directPotential: 140,
  latentChainPotential: 360,
  crackedExposure: 100,
  solidExposure: 40,
} satisfies HeuristicWeights;

const ANTI_CLOG_WEIGHTS = {
  ...SURVIVAL_WEIGHTS,
  adjacentOnes: -550,
  tripleTwos: -750,
  deadLowNumbers: -120,
} satisfies HeuristicWeights;

export const HEURISTIC_PROFILES = {
  legacy: {
    ...SURVIVAL_WEIGHTS,
    legacyNearMatches: 55,
  },
  survival: SURVIVAL_WEIGHTS,
  potential: POTENTIAL_WEIGHTS,
  "anti-clog": ANTI_CLOG_WEIGHTS,
  combined: {
    ...POTENTIAL_WEIGHTS,
    adjacentOnes: ANTI_CLOG_WEIGHTS.adjacentOnes,
    tripleTwos: ANTI_CLOG_WEIGHTS.tripleTwos,
    deadLowNumbers: ANTI_CLOG_WEIGHTS.deadLowNumbers,
  },
} as const satisfies Readonly<Record<HeuristicProfileName, HeuristicWeights>>;

interface LineAnalysis {
  lengths: number[];
  starts: number[];
  ends: number[];
}

interface DiscAnalysis {
  index: number;
  row: number;
  column: number;
  value: number;
  horizontalLength: number;
  verticalLength: number;
  horizontalAddition: number;
  verticalAddition: number;
  addition: number;
  horizontalRelease: number;
  verticalRelease: number;
  release: number;
}

/**
 * Extract a small, mirror-invariant feature vector for horizon evaluation.
 *
 * Direct potential is rooted in additions that can actually grow a line to a
 * disc's number. Latent potential is deliberately allowed to depend only on
 * those addition-rooted discs. That prevents closed cycles such as adjacent
 * ones from claiming that each disc can somehow ignite the other.
 */
export function extractHeuristicFeatures(state: GameState): HeuristicFeatures {
  const { board } = state;
  const horizontal = analyzeLines(board, "horizontal");
  const vertical = analyzeLines(board, "vertical");
  const columnHeights = getColumnHeights(board);
  const discs: DiscAnalysis[] = [];
  const discByIndex = new Map<number, DiscAnalysis>();

  const features: HeuristicFeatures = {
    openColumns: 0,
    heightLoad: 0,
    solidCells: 0,
    crackedCells: 0,
    numberedCells: 0,
    highLowNumbers: 0,
    legacyNearMatches: 0,
    directPotential: 0,
    latentChainPotential: 0,
    crackedExposure: 0,
    solidExposure: 0,
    adjacentOnes: 0,
    tripleTwos: 0,
    deadLowNumbers: 0,
  };

  for (let column = 0; column < BOARD_SIZE; column += 1) {
    if (board[column] === EMPTY) features.openColumns += 1;
  }

  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const elevation = BOARD_SIZE - row;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = row * BOARD_SIZE + column;
      const cell = board[index];
      if (cell === EMPTY) continue;

      features.heightLoad += elevation * elevation;
      if (cell === SOLID) {
        features.solidCells += 1;
        continue;
      }
      if (cell === CRACKED) {
        features.crackedCells += 1;
        continue;
      }
      if (!isNumbered(cell)) continue;

      features.numberedCells += 1;
      if (cell <= 2 && elevation >= 5) features.highLowNumbers += 1;

      const horizontalLength = horizontal.lengths[index];
      const verticalLength = vertical.lengths[index];
      if (
        Math.min(
          Math.abs(cell - horizontalLength),
          Math.abs(cell - verticalLength),
        ) === 1
      ) {
        features.legacyNearMatches += 1;
      }

      const verticalAddition =
        cell > columnHeights[column]
          ? readiness(cell - columnHeights[column])
          : 0;
      const horizontalCost = minimumHorizontalAdditionCost(
        row,
        cell,
        horizontal.starts[index],
        horizontal.ends[index],
        horizontalLength,
        columnHeights,
      );
      const horizontalAddition =
        horizontalCost === null ? 0 : readiness(horizontalCost);
      const addition = unionReadiness(
        horizontalAddition,
        verticalAddition,
      );
      const analysis: DiscAnalysis = {
        index,
        row,
        column,
        value: cell,
        horizontalLength,
        verticalLength,
        horizontalAddition,
        verticalAddition,
        addition,
        horizontalRelease: 0,
        verticalRelease: 0,
        release: 0,
      };
      discs.push(analysis);
      discByIndex.set(index, analysis);
      features.directPotential += addition;
    }
  }

  for (const disc of discs) {
    const horizontalRelease = releaseReadiness(
      disc.horizontalLength - disc.value,
      numberedDiscReadinessInHorizontalLine(
        disc,
        horizontal,
        discByIndex,
      ),
    );
    const verticalRelease = releaseReadiness(
      disc.verticalLength - disc.value,
      numberedDiscReadinessInVerticalLine(disc, vertical, discByIndex),
    );
    disc.horizontalRelease = horizontalRelease;
    disc.verticalRelease = verticalRelease;
    disc.release = unionReadiness(horizontalRelease, verticalRelease);
    features.latentChainPotential += disc.release;

    if (
      disc.value <= 2 &&
      disc.horizontalLength > disc.value &&
      disc.verticalLength > disc.value
    ) {
      features.deadLowNumbers += 1 - unionReadiness(disc.addition, disc.release);
    }
  }

  features.adjacentOnes = countAdjacentOnes(board, discByIndex);
  features.tripleTwos = countLockedTwoRuns(board, discByIndex);

  for (let index = 0; index < board.length; index += 1) {
    const cell = board[index];
    if (cell !== SOLID && cell !== CRACKED) continue;

    const neighborReadiness: number[] = [];
    const row = Math.floor(index / BOARD_SIZE);
    const column = index % BOARD_SIZE;
    for (const [rowDelta, columnDelta] of ORTHOGONAL_DIRECTIONS) {
      const neighborRow = row + rowDelta;
      const neighborColumn = column + columnDelta;
      if (!isInside(neighborRow, neighborColumn)) continue;
      const neighbor = discByIndex.get(
        neighborRow * BOARD_SIZE + neighborColumn,
      );
      if (!neighbor) continue;
      neighborReadiness.push(
        unionReadiness(neighbor.addition, neighbor.release),
      );
    }

    neighborReadiness.sort((a, b) => b - a);
    if (cell === CRACKED) {
      features.crackedExposure += unionAll(neighborReadiness);
    } else {
      // A first hit still has value because it cracks the cover, while two
      // independently plausible hits are what can expose its hidden number.
      features.solidExposure +=
        (neighborReadiness[0] ?? 0) * 0.35 +
        (neighborReadiness[1] ?? 0) * 0.65;
    }
  }

  return features;
}

export function scoreHeuristicFeatures(
  features: HeuristicFeatures,
  weights: HeuristicWeights,
) {
  let utility = 0;
  for (const key of FEATURE_KEYS) utility += features[key] * weights[key];
  return utility;
}

export function evaluateHeuristic(
  state: GameState,
  profile: HeuristicProfileName = DEFAULT_HEURISTIC_PROFILE,
) {
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;
  return scoreHeuristicFeatures(
    extractHeuristicFeatures(state),
    HEURISTIC_PROFILES[profile],
  );
}

const FEATURE_KEYS = [
  "openColumns",
  "heightLoad",
  "solidCells",
  "crackedCells",
  "numberedCells",
  "highLowNumbers",
  "legacyNearMatches",
  "directPotential",
  "latentChainPotential",
  "crackedExposure",
  "solidExposure",
  "adjacentOnes",
  "tripleTwos",
  "deadLowNumbers",
] as const satisfies readonly (keyof HeuristicFeatures)[];

const ORTHOGONAL_DIRECTIONS = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
] as const;

function analyzeLines(
  board: Board,
  axis: "horizontal" | "vertical",
): LineAnalysis {
  const lengths = Array<number>(board.length).fill(0);
  const starts = Array<number>(board.length).fill(-1);
  const ends = Array<number>(board.length).fill(-1);

  for (let fixed = 0; fixed < BOARD_SIZE; fixed += 1) {
    let cursor = 0;
    while (cursor < BOARD_SIZE) {
      const cursorIndex =
        axis === "horizontal"
          ? fixed * BOARD_SIZE + cursor
          : cursor * BOARD_SIZE + fixed;
      if (board[cursorIndex] === EMPTY) {
        cursor += 1;
        continue;
      }

      const start = cursor;
      while (cursor < BOARD_SIZE) {
        const index =
          axis === "horizontal"
            ? fixed * BOARD_SIZE + cursor
            : cursor * BOARD_SIZE + fixed;
        if (board[index] === EMPTY) break;
        cursor += 1;
      }
      const end = cursor - 1;
      const length = end - start + 1;
      for (let position = start; position <= end; position += 1) {
        const index =
          axis === "horizontal"
            ? fixed * BOARD_SIZE + position
            : position * BOARD_SIZE + fixed;
        lengths[index] = length;
        starts[index] = start;
        ends[index] = end;
      }
    }
  }

  return { lengths, starts, ends };
}

function getColumnHeights(board: Board) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[row * BOARD_SIZE + column] !== EMPTY) heights[column] += 1;
    }
  }
  return heights;
}

/** Optimistic physical cost to grow this row component to exactly `value`. */
function minimumHorizontalAdditionCost(
  row: number,
  value: number,
  segmentStart: number,
  segmentEnd: number,
  segmentLength: number,
  columnHeights: readonly number[],
) {
  if (segmentStart < 0 || value <= segmentLength) return null;

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
  return Number.isFinite(best) ? best : null;
}

function numberedDiscReadinessInHorizontalLine(
  target: DiscAnalysis,
  horizontal: LineAnalysis,
  discByIndex: ReadonlyMap<number, DiscAnalysis>,
) {
  const support: number[] = [];
  const start = horizontal.starts[target.index];
  const end = horizontal.ends[target.index];
  for (let column = start; column <= end; column += 1) {
    const index = target.row * BOARD_SIZE + column;
    if (index === target.index) continue;
    const disc = discByIndex.get(index);
    if (disc) support.push(disc.addition);
  }
  return support;
}

function numberedDiscReadinessInVerticalLine(
  target: DiscAnalysis,
  vertical: LineAnalysis,
  discByIndex: ReadonlyMap<number, DiscAnalysis>,
) {
  const support: number[] = [];
  const start = vertical.starts[target.index];
  const end = vertical.ends[target.index];
  for (let row = start; row <= end; row += 1) {
    const index = row * BOARD_SIZE + target.column;
    if (index === target.index) continue;
    const disc = discByIndex.get(index);
    if (disc) support.push(disc.addition);
  }
  return support;
}

function releaseReadiness(
  excessLength: number,
  supporterReadiness: readonly number[],
) {
  if (excessLength <= 0 || supporterReadiness.length < excessLength) return 0;
  const sorted = [...supporterReadiness].sort((a, b) => b - a);
  return sorted[excessLength - 1] * readiness(excessLength);
}

function countAdjacentOnes(
  board: Board,
  discByIndex: ReadonlyMap<number, DiscAnalysis>,
) {
  let count = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = row * BOARD_SIZE + column;
      if (board[index] !== 1) continue;
      const disc = discByIndex.get(index);
      if (!disc) continue;

      if (column + 1 < BOARD_SIZE && board[index + 1] === 1) {
        const neighbor = discByIndex.get(index + 1);
        if (neighbor) {
          count += lockedness(
            perpendicularReadiness(disc, "horizontal"),
            perpendicularReadiness(neighbor, "horizontal"),
          );
        }
      }
      if (row + 1 < BOARD_SIZE && board[index + BOARD_SIZE] === 1) {
        const neighbor = discByIndex.get(index + BOARD_SIZE);
        if (neighbor) {
          count += lockedness(
            perpendicularReadiness(disc, "vertical"),
            perpendicularReadiness(neighbor, "vertical"),
          );
        }
      }
    }
  }
  return count;
}

function countLockedTwoRuns(
  board: Board,
  discByIndex: ReadonlyMap<number, DiscAnalysis>,
) {
  let count = 0;
  for (const axis of ["horizontal", "vertical"] as const) {
    for (let fixed = 0; fixed < BOARD_SIZE; fixed += 1) {
      let cursor = 0;
      while (cursor < BOARD_SIZE) {
        const index =
          axis === "horizontal"
            ? fixed * BOARD_SIZE + cursor
            : cursor * BOARD_SIZE + fixed;
        if (board[index] !== 2) {
          cursor += 1;
          continue;
        }
        const start = cursor;
        while (cursor < BOARD_SIZE) {
          const nextIndex =
            axis === "horizontal"
              ? fixed * BOARD_SIZE + cursor
              : cursor * BOARD_SIZE + fixed;
          if (board[nextIndex] !== 2) break;
          cursor += 1;
        }
        const excess = cursor - start - 2;
        if (excess <= 0) continue;

        const escapeReadiness: number[] = [];
        for (let position = start; position < cursor; position += 1) {
          const runIndex =
            axis === "horizontal"
              ? fixed * BOARD_SIZE + position
              : position * BOARD_SIZE + fixed;
          const disc = discByIndex.get(runIndex);
          if (disc) {
            escapeReadiness.push(perpendicularReadiness(disc, axis));
          }
        }
        count += excess * excess * lockedness(...escapeReadiness);
      }
    }
  }
  return count;
}

function perpendicularReadiness(
  disc: DiscAnalysis,
  runAxis: "horizontal" | "vertical",
) {
  return runAxis === "horizontal"
    ? unionReadiness(disc.verticalAddition, disc.verticalRelease)
    : unionReadiness(disc.horizontalAddition, disc.horizontalRelease);
}

function lockedness(...escapeReadiness: readonly number[]) {
  return 1 - Math.max(0, ...escapeReadiness);
}

function readiness(cost: number) {
  return cost >= 1 ? 2 ** (1 - cost) : 0;
}

function unionReadiness(first: number, second: number) {
  return 1 - (1 - first) * (1 - second);
}

function unionAll(values: readonly number[]) {
  let inverse = 1;
  for (const value of values) inverse *= 1 - value;
  return 1 - inverse;
}

function isInside(row: number, column: number) {
  return (
    row >= 0 &&
    row < BOARD_SIZE &&
    column >= 0 &&
    column < BOARD_SIZE
  );
}
