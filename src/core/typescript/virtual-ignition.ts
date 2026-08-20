import {
  BOARD_SIZE,
  CRACKED,
  EMPTY,
  SOLID,
  isNumbered,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
} from "./heuristic.ts";

export interface VirtualIgnitionFeatures {
  ignitionReadiness: number;
  seedClearPotential: number;
  initialCoverCracks: number;
  initialCoverReveals: number;
  downstreamClears: number;
  downstreamCoverReveals: number;
  downstreamWaves: number;
  cascadeDepthEnergy: number;
  coverReduction: number;
}

export type VirtualIgnitionWeights = Readonly<
  Record<keyof VirtualIgnitionFeatures, number>
>;

export interface VirtualIgnitionSeedAnalysis {
  index: number;
  value: DiscValue;
  additionCost: number;
  readiness: number;
  initialCoverCracks: number;
  initialCoverReveals: number;
  downstreamClears: number;
  downstreamCoverReveals: number;
  downstreamWaves: number;
  cascadeDepthEnergy: number;
  coverReduction: number;
}

export interface VirtualIgnitionAnalysis {
  features: VirtualIgnitionFeatures;
  seeds: readonly VirtualIgnitionSeedAnalysis[];
  scenarios: number;
}

export interface VirtualIgnitionOptions {
  scenarios?: number;
}

export const VIRTUAL_IGNITION_WEIGHTS: VirtualIgnitionWeights = {
  ignitionReadiness: 40,
  seedClearPotential: 40,
  initialCoverCracks: 90,
  initialCoverReveals: 180,
  downstreamClears: 160,
  downstreamCoverReveals: 260,
  downstreamWaves: 240,
  cascadeDepthEnergy: 80,
  coverReduction: 160,
};

export const DEFAULT_VIRTUAL_IGNITION_SCENARIOS = 7;
export const MAX_VIRTUAL_ADDITIONS = 6;

const VIRTUAL = 10 as const;
const MAX_SCENARIOS = 14;
const MAX_CASCADE_WAVES = BOARD_SIZE * BOARD_SIZE;
const REVEAL_DOMAIN = 0x5649_5245;
const EVENT_MULTIPLIER = 0x9e37_79b9;

type VirtualCell = Cell | typeof VIRTUAL;
type VirtualBoard = readonly VirtualCell[];

interface AdditionPlan {
  columns: readonly number[];
  cost: number;
}

interface IgnitionOutcome {
  initialCoverCracks: number;
  initialCoverReveals: number;
  downstreamClears: number;
  downstreamCoverReveals: number;
  downstreamWaves: number;
  cascadeDepthEnergy: number;
  coverReduction: number;
}

/**
 * Estimate latent cascades by virtually completing a physically reachable
 * trigger, removing that seed, applying exact adjacent cover hits, and then
 * resolving every causally downstream wave.
 *
 * Added support discs use an inert internal marker. They occupy line length
 * and survive gravity, but cannot introduce an invented numbered trigger.
 * This isolates the stored chain already present on the board. Readiness
 * exponentially discounts the number of real additions needed to reach that
 * virtual trigger. Since analysis starts only from such a physical plan,
 * dependency cycles with no outside seed remain exactly dormant.
 */
export function analyzeVirtualIgnition(
  state: GameState,
  options: VirtualIgnitionOptions = {},
): VirtualIgnitionAnalysis {
  const scenarios = options.scenarios ?? DEFAULT_VIRTUAL_IGNITION_SCENARIOS;
  if (!Number.isSafeInteger(scenarios) || scenarios < 1 || scenarios > MAX_SCENARIOS) {
    throw new Error(`virtual ignition scenarios must be from 1 to ${MAX_SCENARIOS}`);
  }
  const board = canonicalBoard(state.board);
  const features = zeroFeatures();
  const seeds: VirtualIgnitionSeedAnalysis[] = [];
  const boardHash = hashBoard(board);

  for (let index = 0; index < board.length; index += 1) {
    const value = board[index];
    if (!isNumbered(value)) continue;
    const plan = minimumAdditionPlan(board, index, value);
    if (!plan || plan.cost > MAX_VIRTUAL_ADDITIONS) continue;
    const prepared = prepareBoard(board, plan.columns);
    if (!prepared || !isTrigger(prepared, index, value)) continue;
    const readiness = additionReadiness(plan.cost);
    const totals = zeroOutcome();
    for (let scenario = 0; scenario < scenarios; scenario += 1) {
      addOutcome(
        totals,
        ignite(
          prepared,
          index,
          board,
          scenarioRandom(boardHash ^ Math.imul(index + 1, 0x85eb_ca6b), scenario, scenarios),
        ),
      );
    }
    scaleOutcome(totals, 1 / scenarios);
    const seed: VirtualIgnitionSeedAnalysis = {
      index,
      value,
      additionCost: plan.cost,
      readiness,
      ...totals,
    };
    seeds.push(seed);
    features.ignitionReadiness += readiness;
    features.seedClearPotential += readiness;
    features.initialCoverCracks += readiness * totals.initialCoverCracks;
    features.initialCoverReveals += readiness * totals.initialCoverReveals;
    features.downstreamClears += readiness * totals.downstreamClears;
    features.downstreamCoverReveals +=
      readiness * totals.downstreamCoverReveals;
    features.downstreamWaves += readiness * totals.downstreamWaves;
    features.cascadeDepthEnergy += readiness * totals.cascadeDepthEnergy;
    features.coverReduction += readiness * totals.coverReduction;
  }
  return { features, seeds, scenarios };
}

export function extractVirtualIgnitionFeatures(
  state: GameState,
  options: VirtualIgnitionOptions = {},
) {
  return analyzeVirtualIgnition(state, options).features;
}

export function scoreVirtualIgnitionFeatures(
  features: VirtualIgnitionFeatures,
  weights: VirtualIgnitionWeights = VIRTUAL_IGNITION_WEIGHTS,
) {
  let result = 0;
  for (const key of Object.keys(features) as (keyof VirtualIgnitionFeatures)[]) {
    result += features[key] * weights[key];
  }
  return result;
}

/** Combined heuristic plus an independently scalable virtual-cascade residual. */
export function evaluateVirtualIgnition(
  state: GameState,
  scale = 1,
  options: VirtualIgnitionOptions = {},
) {
  validateScale(scale);
  if (state.gameOver) return HEURISTIC_GAME_OVER_UTILITY;
  const board = canonicalBoard(state.board);
  const canonicalState = board === state.board ? state : { ...state, board };
  return (
    evaluateHeuristic(canonicalState, "combined") +
    scale *
      scoreVirtualIgnitionFeatures(
        extractVirtualIgnitionFeatures(canonicalState, options),
      )
  );
}

function minimumAdditionPlan(
  board: Board,
  index: number,
  value: DiscValue,
): AdditionPlan | null {
  const row = Math.floor(index / BOARD_SIZE);
  const column = index % BOARD_SIZE;
  const heights = columnHeights(board);
  const rowLength = lineLength(board, row, column, "row");
  const columnLength = lineLength(board, row, column, "column");
  if (rowLength === value || columnLength === value) {
    return { columns: [], cost: 0 };
  }
  const candidates: AdditionPlan[] = [];
  if (columnLength < value && heights[column] < value) {
    const cost = value - heights[column];
    candidates.push({ columns: Array<number>(cost).fill(column), cost });
  }
  if (rowLength < value) {
    const [start, end] = segmentBounds(board, row, column);
    const needed = value - rowLength;
    const elevation = BOARD_SIZE - row;
    for (let left = 0; left <= needed; left += 1) {
      const right = needed - left;
      if (start - left < 0 || end + right >= BOARD_SIZE) continue;
      const columns: number[] = [];
      for (let candidate = start - left; candidate < start; candidate += 1) {
        appendColumnFill(columns, board, candidate, row, elevation, heights);
      }
      for (let candidate = end + 1; candidate <= end + right; candidate += 1) {
        appendColumnFill(columns, board, candidate, row, elevation, heights);
      }
      candidates.push({ columns, cost: columns.length });
    }
  }
  candidates.sort(
    (first, second) =>
      first.cost - second.cost ||
      uniqueCount(first.columns) - uniqueCount(second.columns) ||
      compareColumns(first.columns, second.columns),
  );
  return candidates[0] ?? null;
}

function appendColumnFill(
  columns: number[],
  board: Board,
  column: number,
  row: number,
  elevation: number,
  heights: readonly number[],
) {
  if (board[indexOf(row, column)] !== EMPTY) return;
  const additions = Math.max(0, elevation - heights[column]);
  for (let count = 0; count < additions; count += 1) columns.push(column);
}

function prepareBoard(board: Board, columns: readonly number[]) {
  const prepared: VirtualCell[] = [...board];
  for (const column of columns) {
    let placed = false;
    for (let row = BOARD_SIZE - 1; row >= 0; row -= 1) {
      const index = indexOf(row, column);
      if (prepared[index] !== EMPTY) continue;
      prepared[index] = VIRTUAL;
      placed = true;
      break;
    }
    if (!placed) return null;
  }
  return prepared;
}

function ignite(
  prepared: VirtualBoard,
  seedIndex: number,
  original: Board,
  random: () => number,
): IgnitionOutcome {
  const initial = clearWave(prepared, [seedIndex], random);
  let board = applyVirtualGravity(initial.board);
  let downstreamClears = 0;
  let downstreamCoverReveals = 0;
  let downstreamWaves = 0;
  let cascadeDepthEnergy = 0;
  for (let depth = 1; depth <= MAX_CASCADE_WAVES; depth += 1) {
    const poppers = findVirtualPoppers(board);
    if (poppers.length === 0) break;
    const wave = clearWave(board, poppers, random);
    downstreamClears += poppers.length;
    downstreamCoverReveals += wave.reveals;
    downstreamWaves += 1;
    // The forced ignition is virtual wave one; natural waves begin at depth 2.
    cascadeDepthEnergy += poppers.length * (depth + 1) ** 2;
    board = applyVirtualGravity(wave.board);
  }
  return {
    initialCoverCracks: initial.cracks,
    initialCoverReveals: initial.reveals,
    downstreamClears,
    downstreamCoverReveals,
    downstreamWaves,
    cascadeDepthEnergy,
    coverReduction: Math.max(0, coverCount(original) - coverCount(board)),
  };
}

function clearWave(
  board: VirtualBoard,
  poppers: readonly number[],
  random: () => number,
) {
  const result: VirtualCell[] = [...board];
  const popping = new Set(poppers);
  for (const index of popping) result[index] = EMPTY;
  let cracks = 0;
  let reveals = 0;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = indexOf(row, column);
      const cell = board[index];
      if (cell !== SOLID && cell !== CRACKED) continue;
      let hits = 0;
      for (const [rowDelta, columnDelta] of DIRECTIONS) {
        const neighborRow = row + rowDelta;
        const neighborColumn = column + columnDelta;
        if (
          inside(neighborRow, neighborColumn) &&
          popping.has(indexOf(neighborRow, neighborColumn))
        ) {
          hits += 1;
        }
      }
      if (hits === 0) continue;
      if (cell === CRACKED || hits >= 2) {
        result[index] = randomDisc(random);
        reveals += 1;
      } else {
        result[index] = CRACKED;
        cracks += 1;
      }
    }
  }
  return { board: result, cracks, reveals };
}

function findVirtualPoppers(board: VirtualBoard) {
  const result: number[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const index = indexOf(row, column);
      const cell = board[index];
      if (!isNumbered(cell as Cell)) continue;
      if (
        lineLength(board, row, column, "row") === cell ||
        lineLength(board, row, column, "column") === cell
      ) {
        result.push(index);
      }
    }
  }
  return result;
}

function applyVirtualGravity(board: VirtualBoard) {
  const result = Array<VirtualCell>(board.length).fill(EMPTY);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    let destination = BOARD_SIZE - 1;
    for (let row = BOARD_SIZE - 1; row >= 0; row -= 1) {
      const cell = board[indexOf(row, column)];
      if (cell === EMPTY) continue;
      result[indexOf(destination, column)] = cell;
      destination -= 1;
    }
  }
  return result;
}

function isTrigger(
  board: VirtualBoard,
  index: number,
  value: DiscValue,
) {
  const row = Math.floor(index / BOARD_SIZE);
  const column = index % BOARD_SIZE;
  return (
    lineLength(board, row, column, "row") === value ||
    lineLength(board, row, column, "column") === value
  );
}

function lineLength(
  board: VirtualBoard,
  row: number,
  column: number,
  axis: "row" | "column",
) {
  if (board[indexOf(row, column)] === EMPTY) return 0;
  const [rowStep, columnStep] = axis === "row" ? [0, 1] : [1, 0];
  let result = 1;
  for (const direction of [-1, 1]) {
    let nextRow = row + rowStep * direction;
    let nextColumn = column + columnStep * direction;
    while (
      inside(nextRow, nextColumn) &&
      board[indexOf(nextRow, nextColumn)] !== EMPTY
    ) {
      result += 1;
      nextRow += rowStep * direction;
      nextColumn += columnStep * direction;
    }
  }
  return result;
}

function segmentBounds(board: Board, row: number, column: number) {
  let start = column;
  let end = column;
  while (start > 0 && board[indexOf(row, start - 1)] !== EMPTY) start -= 1;
  while (
    end + 1 < BOARD_SIZE &&
    board[indexOf(row, end + 1)] !== EMPTY
  ) {
    end += 1;
  }
  return [start, end] as const;
}

function columnHeights(board: Board) {
  const result = Array<number>(BOARD_SIZE).fill(0);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    for (let row = 0; row < BOARD_SIZE; row += 1) {
      if (board[indexOf(row, column)] !== EMPTY) result[column] += 1;
    }
  }
  return result;
}

function scenarioRandom(hash: number, scenario: number, scenarios: number) {
  let event = 0;
  return () => {
    const rotation =
      mix32(hash ^ REVEAL_DOMAIN ^ Math.imul(event + 1, EVENT_MULTIPLIER)) %
      scenarios;
    const stratum = (scenario + rotation) % scenarios;
    event += 1;
    return (stratum + 0.5) / scenarios;
  };
}

function randomDisc(random: () => number): DiscValue {
  return (Math.floor(Math.max(0, Math.min(0.999999999999, random())) * 7) +
    1) as DiscValue;
}

function canonicalBoard(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[indexOf(row, column)];
      const reverse = board[indexOf(row, BOARD_SIZE - 1 - column)];
      if (forward < reverse) return board;
      if (forward > reverse) return mirrorBoard(board);
    }
  }
  return board;
}

function mirrorBoard(board: Board): Board {
  const result: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      result.push(board[indexOf(row, column)]);
    }
  }
  return result;
}

function hashBoard(board: Board) {
  let hash = 0x811c_9dc5;
  for (const cell of board) hash = Math.imul(hash ^ (cell + 1), 0x0100_0193);
  return mix32(hash);
}

function coverCount(board: VirtualBoard) {
  return board.filter((cell) => cell === SOLID || cell === CRACKED).length;
}

function additionReadiness(cost: number) {
  return cost === 0 ? 1 : 2 ** (1 - cost);
}

function zeroFeatures(): VirtualIgnitionFeatures {
  return {
    ignitionReadiness: 0,
    seedClearPotential: 0,
    initialCoverCracks: 0,
    initialCoverReveals: 0,
    downstreamClears: 0,
    downstreamCoverReveals: 0,
    downstreamWaves: 0,
    cascadeDepthEnergy: 0,
    coverReduction: 0,
  };
}

function zeroOutcome(): IgnitionOutcome {
  return {
    initialCoverCracks: 0,
    initialCoverReveals: 0,
    downstreamClears: 0,
    downstreamCoverReveals: 0,
    downstreamWaves: 0,
    cascadeDepthEnergy: 0,
    coverReduction: 0,
  };
}

function addOutcome(target: IgnitionOutcome, source: IgnitionOutcome) {
  for (const key of Object.keys(target) as (keyof IgnitionOutcome)[]) {
    target[key] += source[key];
  }
}

function scaleOutcome(target: IgnitionOutcome, scale: number) {
  for (const key of Object.keys(target) as (keyof IgnitionOutcome)[]) {
    target[key] *= scale;
  }
}

function uniqueCount(values: readonly number[]) {
  return new Set(values).size;
}

function compareColumns(first: readonly number[], second: readonly number[]) {
  for (let index = 0; index < Math.min(first.length, second.length); index += 1) {
    if (first[index] !== second[index]) return first[index] - second[index];
  }
  return first.length - second.length;
}

function validateScale(scale: number) {
  if (!Number.isFinite(scale) || scale < 0) {
    throw new Error("virtual ignition scale must be non-negative and finite");
  }
}

function indexOf(row: number, column: number) {
  return row * BOARD_SIZE + column;
}

function inside(row: number, column: number) {
  return row >= 0 && row < BOARD_SIZE && column >= 0 && column < BOARD_SIZE;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

const DIRECTIONS = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
] as const;
