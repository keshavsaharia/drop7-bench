import {
  BOARD_SIZE,
  EMPTY,
  playMove,
  seededRandom,
  serializeBoard,
  type Board,
  type Cell,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "./heuristic.ts";

export type MctsEvaluator = (state: GameState) => number;

export interface MctsSolverOptions {
  simulations: number;
  /** Maximum simulated moves, including the candidate root move. */
  horizon: number;
  /** Greedy rollout moves used when a simulation reaches a new state. */
  rolloutDepth?: number;
  /** UCB exploration bonus in evaluator/score units. */
  exploration?: number;
  /** Hard bound on retained decision states. */
  maxNodes?: number;
  /** Solver-local uint32 seed; it must not be a headless game seed. */
  seed: number;
  heuristicProfile?: HeuristicProfileName;
  /** Overrides heuristicProfile. It must not depend on state.score. */
  evaluator?: MctsEvaluator;
  terminalUtility?: number;
}

export interface MctsColumnEvaluation {
  column: number;
  mean: number;
  visits: number;
}

export interface MctsWorkStats {
  simulatedMoves: number;
  rolloutMoves: number;
  evaluatedStates: number;
  createdNodes: number;
  reusedNodes: number;
  peakPathDepth: number;
}

export interface MctsEvaluationResult {
  bestColumn: number | null;
  columns: readonly MctsColumnEvaluation[];
  simulations: number;
  horizon: number;
  rolloutDepth: number;
  exploration: number;
  maxNodes: number;
  seed: number;
  work: Readonly<MctsWorkStats>;
}

export const MAX_MCTS_SIMULATIONS = 1_000_000;
export const MAX_MCTS_HORIZON = 100;
export const MAX_MCTS_ROLLOUT_DEPTH = 20;
export const MAX_MCTS_NODES = 1_000_000;
export const DEFAULT_MCTS_EXPLORATION = 40_000;
export const DEFAULT_MCTS_MAX_NODES = 100_000;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const CHANCE_DOMAIN = 0x4348_414e;
const ROLLOUT_CHANCE_DOMAIN = 0x524f_4c4c;
const PROBE_DOMAIN = 0x5052_4f42;
const SIMULATION_MULTIPLIER = 0x9e37_79b9;
const PLY_MULTIPLIER = 0x85eb_ca6b;

interface ActionStats {
  column: number;
  visits: number;
  valueSum: number;
}

interface SearchNode {
  state: GameState;
  visits: number;
  actions: ActionStats[];
}

interface PathEntry {
  node: SearchNode;
  action: ActionStats;
  reward: number;
}

/**
 * Chance-sampled Monte Carlo tree search for Drop7.
 *
 * Chance is sampled from a solver-local stream and decision nodes are keyed by
 * the complete observable position. Unlike a determinization beam, later
 * actions are therefore conditioned on the disc and board that were actually
 * observed in that sampled branch. The table has a hard entry bound; once it
 * is full, simulations fall back to the bounded rollout evaluator.
 */
export function evaluateMctsMoves(
  input: GameState,
  options: MctsSolverOptions,
): MctsEvaluationResult {
  const simulations = boundedPositiveInteger(
    options.simulations,
    "simulations",
    MAX_MCTS_SIMULATIONS,
  );
  const horizon = boundedPositiveInteger(
    options.horizon,
    "horizon",
    MAX_MCTS_HORIZON,
  );
  const rolloutDepth = boundedNonNegativeInteger(
    options.rolloutDepth ?? 2,
    "rolloutDepth",
    MAX_MCTS_ROLLOUT_DEPTH,
  );
  const maxNodes = boundedPositiveInteger(
    options.maxNodes ?? DEFAULT_MCTS_MAX_NODES,
    "maxNodes",
    MAX_MCTS_NODES,
  );
  const exploration = options.exploration ?? DEFAULT_MCTS_EXPLORATION;
  if (!Number.isFinite(exploration) || exploration < 0) {
    throw new RangeError("exploration must be a non-negative finite number");
  }
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new TypeError("terminalUtility must be finite");
  }
  const seed = unsignedSeed(options.seed);
  const profile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const evaluator =
    options.evaluator ??
    ((state: GameState) => evaluateHeuristic(state, profile));
  const work: MctsWorkStats = {
    simulatedMoves: 0,
    rolloutMoves: 0,
    evaluatedStates: 0,
    createdNodes: 0,
    reusedNodes: 0,
    peakPathDepth: 0,
  };

  if (input.gameOver) {
    return {
      bestColumn: null,
      columns: [],
      simulations,
      horizon,
      rolloutDepth,
      exploration,
      maxNodes,
      seed,
      work,
    };
  }

  const canonicalRoot = canonicalizeState(withoutScore(input));
  const table = new Map<string, SearchNode>();
  const root = createNode(canonicalRoot.state);
  table.set(dynamicStateKey(root.state), root);
  work.createdNodes = 1;

  for (let simulation = 0; simulation < simulations; simulation += 1) {
    runSimulation(
      root,
      table,
      simulation,
      {
        horizon,
        rolloutDepth,
        exploration,
        maxNodes,
        seed,
        evaluator,
        terminalUtility,
      },
      work,
    );
  }

  const columns = root.actions
    .map((action) => ({
      column: canonicalRoot.mirrored
        ? BOARD_SIZE - 1 - action.column
        : action.column,
      mean:
        action.visits === 0
          ? Number.NEGATIVE_INFINITY
          : action.valueSum / action.visits,
      visits: action.visits,
    }))
    .sort((first, second) => first.column - second.column);

  return {
    bestColumn: chooseBestColumn(input.board, columns),
    columns,
    simulations,
    horizon,
    rolloutDepth,
    exploration,
    maxNodes,
    seed,
    work,
  };
}

interface ResolvedSearchOptions {
  horizon: number;
  rolloutDepth: number;
  exploration: number;
  maxNodes: number;
  seed: number;
  evaluator: MctsEvaluator;
  terminalUtility: number;
}

function runSimulation(
  root: SearchNode,
  table: Map<string, SearchNode>,
  simulation: number,
  options: ResolvedSearchOptions,
  work: MctsWorkStats,
) {
  const path: PathEntry[] = [];
  let node = root;
  let leafValue = 0;

  for (let ply = 0; ply < options.horizon; ply += 1) {
    const action = selectAction(node, options.exploration);
    const move = playMove(
      node.state,
      action.column,
      seededRandom(chanceSeed(options.seed, simulation, ply, CHANCE_DOMAIN)),
      { captureAnimation: false },
    );
    work.simulatedMoves += 1;
    if (!move) {
      leafValue = options.terminalUtility;
      break;
    }

    path.push({ node, action, reward: move.scoreDelta });
    work.peakPathDepth = Math.max(work.peakPathDepth, path.length);
    if (move.state.gameOver) {
      leafValue = options.terminalUtility;
      break;
    }

    const canonical = canonicalizeState(withoutScore(move.state));
    if (ply + 1 === options.horizon) {
      leafValue = evaluateLeaf(canonical.state, options.evaluator, work);
      break;
    }

    const key = dynamicStateKey(canonical.state);
    const existing = table.get(key);
    if (existing) {
      work.reusedNodes += 1;
      node = existing;
      continue;
    }

    if (table.size < options.maxNodes) {
      const child = createNode(canonical.state);
      table.set(key, child);
      work.createdNodes += 1;
      node = child;
    }
    leafValue = rolloutValue(
      canonical.state,
      simulation,
      ply + 1,
      Math.min(options.rolloutDepth, options.horizon - ply - 1),
      options,
      work,
    );
    break;
  }

  let value = leafValue;
  for (let index = path.length - 1; index >= 0; index -= 1) {
    const entry = path[index];
    value += entry.reward;
    entry.action.visits += 1;
    entry.action.valueSum += value;
    entry.node.visits += 1;
  }
}

function selectAction(node: SearchNode, exploration: number) {
  for (const action of node.actions) {
    if (action.visits === 0) return action;
  }

  let best = node.actions[0];
  let bestBound = Number.NEGATIVE_INFINITY;
  const logarithm = Math.log(node.visits + 1);
  for (const action of node.actions) {
    const mean = action.valueSum / action.visits;
    const bound =
      mean + exploration * Math.sqrt(logarithm / action.visits);
    if (bound > bestBound) {
      best = action;
      bestBound = bound;
    }
  }
  return best;
}

function rolloutValue(
  initial: GameState,
  simulation: number,
  startingPly: number,
  depth: number,
  options: ResolvedSearchOptions,
  work: MctsWorkStats,
) {
  let state = initial;
  let reward = 0;

  for (let offset = 0; offset < depth; offset += 1) {
    const ply = startingPly + offset;
    const column = chooseGreedyColumn(
      state,
      simulation,
      ply,
      options,
      work,
    );
    if (column === null) return reward + options.terminalUtility;
    const move = playMove(
      state,
      column,
      seededRandom(
        chanceSeed(options.seed, simulation, ply, ROLLOUT_CHANCE_DOMAIN),
      ),
      { captureAnimation: false },
    );
    work.simulatedMoves += 1;
    work.rolloutMoves += 1;
    if (!move) return reward + options.terminalUtility;
    reward += move.scoreDelta;
    if (move.state.gameOver) return reward + options.terminalUtility;
    state = canonicalizeState(withoutScore(move.state)).state;
  }

  return reward + evaluateLeaf(state, options.evaluator, work);
}

function chooseGreedyColumn(
  state: GameState,
  simulation: number,
  ply: number,
  options: ResolvedSearchOptions,
  work: MctsWorkStats,
) {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of COLUMN_ORDER) {
    if (state.board[column] !== EMPTY) continue;
    const move = playMove(
      state,
      column,
      seededRandom(
        chanceSeed(
          options.seed ^ Math.imul(column + 1, 0xc2b2_ae35),
          simulation,
          ply,
          PROBE_DOMAIN,
        ),
      ),
      { captureAnimation: false },
    );
    work.simulatedMoves += 1;
    if (!move) continue;
    const value =
      move.scoreDelta +
      (move.state.gameOver
        ? options.terminalUtility
        : evaluateLeaf(move.state, options.evaluator, work));
    if (value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function evaluateLeaf(
  state: GameState,
  evaluator: MctsEvaluator,
  work: MctsWorkStats,
) {
  work.evaluatedStates += 1;
  const value = evaluator(withoutScore(state));
  if (!Number.isFinite(value)) {
    throw new TypeError("MCTS evaluator must return a finite number");
  }
  return value;
}

function createNode(state: GameState): SearchNode {
  return {
    state,
    visits: 0,
    actions: COLUMN_ORDER.filter((column) => state.board[column] === EMPTY).map(
      (column) => ({ column, visits: 0, valueSum: 0 }),
    ),
  };
}

function chooseBestColumn(
  board: Board,
  columns: readonly MctsColumnEvaluation[],
) {
  let bestColumn: number | null = null;
  let bestVisits = -1;
  let bestMean = Number.NEGATIVE_INFINITY;
  for (const column of tieOrderForBoard(board)) {
    const candidate = columns.find((item) => item.column === column);
    if (!candidate) continue;
    if (
      candidate.visits > bestVisits ||
      (candidate.visits === bestVisits && candidate.mean > bestMean)
    ) {
      bestColumn = column;
      bestVisits = candidate.visits;
      bestMean = candidate.mean;
    }
  }
  return bestColumn;
}

function canonicalizeState(state: GameState) {
  const mirrored = mirroredRepresentationIsSmaller(state.board);
  return {
    state: mirrored ? { ...state, board: mirrorBoard(state.board) } : state,
    mirrored,
  };
}

function mirroredRepresentationIsSmaller(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (mirrored < forward) return true;
      if (mirrored > forward) return false;
    }
  }
  return false;
}

function mirrorBoard(board: Board): Board {
  const mirrored: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      mirrored.push(board[offset + column]);
    }
  }
  return mirrored;
}

function tieOrderForBoard(board: Board) {
  return mirroredRepresentationIsSmaller(board)
    ? COLUMN_ORDER.map((column) => BOARD_SIZE - 1 - column)
    : COLUMN_ORDER;
}

function withoutScore(state: GameState): GameState {
  return state.score === 0 ? state : { ...state, score: 0 };
}

function dynamicStateKey(state: GameState) {
  return `${serializeBoard(state.board)}:${state.nextDisc}:${state.movesRemaining}:${state.gameOver ? 1 : 0}`;
}

function chanceSeed(
  seed: number,
  simulation: number,
  ply: number,
  domain: number,
) {
  return mix32(
    seed ^
      Math.imul((simulation + 1) >>> 0, SIMULATION_MULTIPLIER) ^
      Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
      domain,
  );
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb_352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846c_a68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function boundedPositiveInteger(value: number, name: string, maximum: number) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new RangeError(`${name} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function boundedNonNegativeInteger(
  value: number,
  name: string,
  maximum: number,
) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw new RangeError(`${name} must be an integer from 0 to ${maximum}`);
  }
  return value;
}

function unsignedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new RangeError("seed must be a uint32 integer");
  }
  return seed >>> 0;
}
