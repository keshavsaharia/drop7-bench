import {
  BOARD_SIZE,
  legalColumns,
  playMove,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  HEURISTIC_GAME_OVER_UTILITY,
  evaluateHeuristic,
  type HeuristicProfileName,
} from "./heuristic.ts";

export type OpenLoopEvaluator = (state: GameState) => number;

export interface RobustOpenLoopBeamOptions {
  /** Common-random-number determinizations evaluated by every prefix. */
  scenarios: number;
  /** Number of columns in each complete open-loop plan. */
  depth: number;
  /** Maximum prefixes retained after a complete beam layer. */
  beamWidth: number;
  /** Lower-confidence score: mean - riskAversion * population stddev. */
  riskAversion?: number;
  /** Deterministic transition/evaluation cap. */
  maxWork?: number;
  /** Wall-clock cap for one replanning call. */
  timeLimitMs?: number;
  /** Solver-local seed; never a hidden game-future seed. */
  seed: number;
  heuristicProfile?: HeuristicProfileName;
  /** Overrides heuristicProfile and always receives state.score === 0. */
  evaluator?: OpenLoopEvaluator;
  terminalUtility?: number;
}

export interface OpenLoopColumnEvaluation {
  column: number;
  mean: number;
  variance: number;
  robustValue: number;
  scenarios: number;
  /** Prefix length represented by this column's best surviving plan. */
  depth: number;
  prefix: readonly number[];
}

export interface RobustOpenLoopBeamWork {
  total: number;
  simulatedMoves: number;
  evaluatedStates: number;
  generatedNodes: number;
  prunedNodes: number;
  peakBeamNodes: number;
  /** Current beam + incrementally retained next beam + one candidate. */
  peakRetainedScenarioStates: number;
}

export type OpenLoopStopReason = "complete" | "work" | "time";

export interface RobustOpenLoopBeamResult {
  bestColumn: number | null;
  bestPrefix: readonly number[];
  columns: readonly OpenLoopColumnEvaluation[];
  requestedDepth: number;
  completedDepth: number;
  scenarios: number;
  beamWidth: number;
  riskAversion: number;
  maxWork: number;
  timeLimitMs: number;
  complete: boolean;
  stopReason: OpenLoopStopReason;
  seed: number;
  elapsedMs: number;
  work: Readonly<RobustOpenLoopBeamWork>;
}

export const MAX_OPEN_LOOP_DEPTH = 8;
export const MAX_OPEN_LOOP_SCENARIOS = 256;
export const MAX_OPEN_LOOP_BEAM_WIDTH = 256;
export const MAX_OPEN_LOOP_WORK = 10_000_000;
export const MAX_OPEN_LOOP_TIME_MS = 60_000;
export const DEFAULT_OPEN_LOOP_WORK = 2_000_000;
export const DEFAULT_OPEN_LOOP_TIME_MS = 1_000;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;
const SCENARIO_DOMAIN = 0x5343454e;
const DISC_DOMAIN = 0x44495343;
const REVEAL_DOMAIN = 0x5245564c;
const STRATUM_DOMAIN = 0x53545241;
const JITTER_DOMAIN = 0x4a495454;
const SCENARIO_MULTIPLIER = 0x9e3779b9;
const PLY_MULTIPLIER = 0x85ebca6b;
const EVENT_MULTIPLIER = 0xc2b2ae35;

interface BeamNode {
  /** Columns in one canonical frame, shared by every scenario. */
  prefix: number[];
  firstColumn: number;
  reflected: boolean;
  rootPriority: number;
  states: Array<GameState | null>;
  rewards: Float64Array;
  mean: number;
  variance: number;
  robustValue: number;
}

interface MutableWork extends RobustOpenLoopBeamWork {
  maxWork: number;
  deadline: number;
  scenarios: number;
  stopReason: OpenLoopStopReason;
}

interface CanonicalRoot {
  state: GameState;
  column: number;
  reflected: boolean;
}

interface RootPlan {
  canonical: CanonicalRoot;
  firstColumn: number;
  priority: number;
}

class SearchStopped extends Error {}

/**
 * Search action sequences rather than scenario-contingent policies.
 *
 * Every BeamNode owns exactly one column prefix, and that same prefix is
 * replayed against all chance scenarios. Chance affects the sampled states
 * and utility distribution, never the next action in the prefix. Calling the
 * function again from the newly observed real state supplies replanning
 * without leaking any scenario's unobserved future into the current choice.
 *
 * Candidate nodes are generated and inserted into a top-k beam one at a time.
 * The implementation therefore retains O(beamWidth * scenarios) states; it
 * never materializes the full beamWidth * 7 candidate layer.
 */
export function evaluateRobustOpenLoopBeam(
  state: GameState,
  options: RobustOpenLoopBeamOptions,
): RobustOpenLoopBeamResult {
  const scenarios = boundedInteger(
    options.scenarios,
    "scenarios",
    1,
    MAX_OPEN_LOOP_SCENARIOS,
  );
  const requestedDepth = boundedInteger(
    options.depth,
    "depth",
    1,
    MAX_OPEN_LOOP_DEPTH,
  );
  const beamWidth = boundedInteger(
    options.beamWidth,
    "beamWidth",
    1,
    MAX_OPEN_LOOP_BEAM_WIDTH,
  );
  const maxWork = boundedInteger(
    options.maxWork ?? DEFAULT_OPEN_LOOP_WORK,
    "maxWork",
    1,
    MAX_OPEN_LOOP_WORK,
  );
  const timeLimitMs = boundedInteger(
    options.timeLimitMs ?? DEFAULT_OPEN_LOOP_TIME_MS,
    "timeLimitMs",
    1,
    MAX_OPEN_LOOP_TIME_MS,
  );
  const seed = unsignedSeed(options.seed);
  const riskAversion = options.riskAversion ?? 0;
  if (!Number.isFinite(riskAversion) || riskAversion < 0) {
    throw new Error("riskAversion must be a non-negative finite number");
  }
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new Error("terminalUtility must be finite");
  }
  const profile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const evaluator =
    options.evaluator ??
    ((position: GameState) => evaluateHeuristic(position, profile));

  const startedAt = performance.now();
  const work: MutableWork = {
    total: 0,
    simulatedMoves: 0,
    evaluatedStates: 0,
    generatedNodes: 0,
    prunedNodes: 0,
    peakBeamNodes: 0,
    peakRetainedScenarioStates: 0,
    maxWork,
    deadline: startedAt + timeLimitMs,
    scenarios,
    stopReason: "complete",
  };
  const localSeed = scenarioSeedForState(state, seed);
  const legal = state.gameOver ? [] : legalColumns(state.board);
  const rootOrder = columnOrderForBoard(state.board).filter((column) =>
    legal.includes(column),
  );
  const { plans: rootPlans, aliases: rootAliases } = planCanonicalRoots(
    state,
    rootOrder,
  );
  let beam: BeamNode[] = [];
  let completedDepth = 0;
  let columnSummaries = new Map<number, OpenLoopColumnEvaluation>();

  if (rootOrder.length > 0) {
    try {
      const rootBeam: BeamNode[] = [];
      const rootSummaries = new Map<number, OpenLoopColumnEvaluation>();
      for (const plan of rootPlans) {
        const node = buildRootNode(
          plan.canonical,
          plan.firstColumn,
          plan.priority,
          scenarios,
          localSeed,
          riskAversion,
          evaluator,
          terminalUtility,
          work,
        );
        updateColumnSummary(rootSummaries, node);
        insertTopNode(rootBeam, node, beamWidth, work);
        recordRetainedStates(work, 0, rootBeam.length, 1);
      }
      beam = rootBeam;
      columnSummaries = rootSummaries;
      completedDepth = 1;
      work.peakBeamNodes = Math.max(work.peakBeamNodes, beam.length);
    } catch (error) {
      if (!(error instanceof SearchStopped)) throw error;
    }
  }

  while (
    completedDepth > 0 &&
    completedDepth < requestedDepth &&
    work.stopReason === "complete"
  ) {
    const nextBeam: BeamNode[] = [];
    const nextSummaries = new Map<number, OpenLoopColumnEvaluation>();
    const ply = completedDepth;
    try {
      for (const parent of beam) {
        if (!parent.states.some((scenarioState) => scenarioState !== null)) {
          updateColumnSummary(nextSummaries, parent);
          insertTopNode(nextBeam, parent, beamWidth, work);
          recordRetainedStates(
            work,
            beam.length,
            nextBeam.length,
            0,
          );
          continue;
        }
        for (const column of COLUMN_ORDER) {
          const child = extendNode(
            parent,
            column,
            ply,
            scenarios,
            localSeed,
            riskAversion,
            evaluator,
            terminalUtility,
            work,
          );
          updateColumnSummary(nextSummaries, child);
          insertTopNode(nextBeam, child, beamWidth, work);
          recordRetainedStates(
            work,
            beam.length,
            nextBeam.length,
            1,
          );
        }
      }
    } catch (error) {
      if (!(error instanceof SearchStopped)) throw error;
      break;
    }
    if (nextBeam.length === 0) break;
    beam = nextBeam;
    for (const [column, summary] of nextSummaries) {
      columnSummaries.set(column, summary);
    }
    completedDepth += 1;
    work.peakBeamNodes = Math.max(work.peakBeamNodes, beam.length);
  }

  const best = beam[0];
  const columns = rootOrder
    .map((column) => {
      const representative = rootAliases.get(column) ?? column;
      const summary = columnSummaries.get(representative);
      if (!summary) return undefined;
      return representative === column
        ? summary
        : mirrorColumnSummary(summary, column);
    })
    .filter(
      (summary): summary is OpenLoopColumnEvaluation =>
        summary !== undefined,
    );
  const complete =
    work.stopReason === "complete" &&
    (legal.length === 0 || completedDepth === requestedDepth);

  return {
    bestColumn: best?.firstColumn ?? rootOrder[0] ?? null,
    bestPrefix: best
      ? reportPrefix(best)
      : rootOrder.length > 0
        ? [rootOrder[0]]
        : [],
    columns,
    requestedDepth,
    completedDepth,
    scenarios,
    beamWidth,
    riskAversion,
    maxWork,
    timeLimitMs,
    complete,
    stopReason: complete ? "complete" : work.stopReason,
    seed,
    elapsedMs: Math.max(0, performance.now() - startedAt),
    work: publicWork(work),
  };
}

function buildRootNode(
  canonical: CanonicalRoot,
  firstColumn: number,
  rootPriority: number,
  scenarios: number,
  seed: number,
  riskAversion: number,
  evaluator: OpenLoopEvaluator,
  terminalUtility: number,
  work: MutableWork,
) {
  const states = Array<GameState | null>(scenarios);
  const rewards = new Float64Array(scenarios);
  const utilities = new Float64Array(scenarios);
  for (let scenario = 0; scenario < scenarios; scenario += 1) {
    spendWork(work, "simulatedMoves");
    const move = playMove(
      canonical.state,
      canonical.column,
      scenarioRandom(seed, scenario, scenarios, 0),
      { captureAnimation: false },
    );
    if (!move) {
      states[scenario] = null;
      utilities[scenario] = terminalUtility;
      continue;
    }
    rewards[scenario] = move.scoreDelta;
    if (move.state.gameOver) {
      states[scenario] = null;
      utilities[scenario] = move.scoreDelta + terminalUtility;
      continue;
    }
    const next = withScenarioDisc(
      withoutScore(move.state),
      scenarioDisc(seed, scenario, scenarios, 1),
    );
    states[scenario] = next;
    utilities[scenario] =
      move.scoreDelta + evaluateLeaf(next, evaluator, work);
  }
  work.generatedNodes += 1;
  const stats = distribution(utilities, riskAversion);
  return {
    prefix: [canonical.column],
    firstColumn,
    reflected: canonical.reflected,
    rootPriority,
    states,
    rewards,
    ...stats,
  } satisfies BeamNode;
}

function extendNode(
  parent: BeamNode,
  column: number,
  ply: number,
  scenarios: number,
  seed: number,
  riskAversion: number,
  evaluator: OpenLoopEvaluator,
  terminalUtility: number,
  work: MutableWork,
) {
  const states = Array<GameState | null>(scenarios);
  const rewards = new Float64Array(scenarios);
  const utilities = new Float64Array(scenarios);
  for (let scenario = 0; scenario < scenarios; scenario += 1) {
    const parentReward = parent.rewards[scenario];
    const parentState = parent.states[scenario];
    if (!parentState) {
      states[scenario] = null;
      rewards[scenario] = parentReward;
      utilities[scenario] = parentReward + terminalUtility;
      continue;
    }
    spendWork(work, "simulatedMoves");
    const move = playMove(
      parentState,
      column,
      scenarioRandom(seed, scenario, scenarios, ply),
      { captureAnimation: false },
    );
    if (!move) {
      states[scenario] = null;
      rewards[scenario] = parentReward;
      utilities[scenario] = parentReward + terminalUtility;
      continue;
    }
    const reward = parentReward + move.scoreDelta;
    rewards[scenario] = reward;
    if (move.state.gameOver) {
      states[scenario] = null;
      utilities[scenario] = reward + terminalUtility;
      continue;
    }
    const next = withScenarioDisc(
      withoutScore(move.state),
      scenarioDisc(seed, scenario, scenarios, ply + 1),
    );
    states[scenario] = next;
    utilities[scenario] = reward + evaluateLeaf(next, evaluator, work);
  }
  work.generatedNodes += 1;
  const stats = distribution(utilities, riskAversion);
  return {
    prefix: [...parent.prefix, column],
    firstColumn: parent.firstColumn,
    reflected: parent.reflected,
    rootPriority: parent.rootPriority,
    states,
    rewards,
    ...stats,
  } satisfies BeamNode;
}

function evaluateLeaf(
  state: GameState,
  evaluator: OpenLoopEvaluator,
  work: MutableWork,
) {
  spendWork(work, "evaluatedStates");
  const value = evaluator(state.score === 0 ? state : { ...state, score: 0 });
  if (!Number.isFinite(value)) {
    throw new Error("open-loop evaluator must return a finite number");
  }
  return value;
}

function distribution(values: Float64Array, riskAversion: number) {
  let mean = 0;
  let squaredDeviation = 0;
  let count = 0;
  for (const value of values) {
    count += 1;
    const difference = value - mean;
    mean += difference / count;
    squaredDeviation += difference * (value - mean);
  }
  const variance = Math.max(0, squaredDeviation / count);
  return {
    mean,
    variance,
    robustValue: mean - riskAversion * Math.sqrt(variance),
  };
}

function insertTopNode(
  beam: BeamNode[],
  candidate: BeamNode,
  beamWidth: number,
  work: MutableWork,
) {
  let insertion = beam.length;
  while (
    insertion > 0 &&
    compareNodes(candidate, beam[insertion - 1]) < 0
  ) {
    insertion -= 1;
  }
  if (insertion >= beamWidth) {
    work.prunedNodes += 1;
    return;
  }
  beam.splice(insertion, 0, candidate);
  if (beam.length > beamWidth) {
    beam.pop();
    work.prunedNodes += 1;
  }
}

function compareNodes(first: BeamNode, second: BeamNode) {
  if (first.robustValue !== second.robustValue) {
    return second.robustValue - first.robustValue;
  }
  if (first.mean !== second.mean) return second.mean - first.mean;
  if (first.variance !== second.variance) {
    return first.variance - second.variance;
  }
  if (first.rootPriority !== second.rootPriority) {
    return first.rootPriority - second.rootPriority;
  }
  const length = Math.min(first.prefix.length, second.prefix.length);
  for (let index = 0; index < length; index += 1) {
    const difference =
      columnPriority(first.prefix[index]) -
      columnPriority(second.prefix[index]);
    if (difference !== 0) return difference;
  }
  return first.prefix.length - second.prefix.length;
}

function updateColumnSummary(
  summaries: Map<number, OpenLoopColumnEvaluation>,
  node: BeamNode,
) {
  const candidate = nodeSummary(node);
  const previous = summaries.get(node.firstColumn);
  if (
    !previous ||
    candidate.robustValue > previous.robustValue ||
    (candidate.robustValue === previous.robustValue &&
      candidate.mean > previous.mean)
  ) {
    summaries.set(node.firstColumn, candidate);
  }
}

function nodeSummary(node: BeamNode): OpenLoopColumnEvaluation {
  return {
    column: node.firstColumn,
    mean: node.mean,
    variance: node.variance,
    robustValue: node.robustValue,
    scenarios: node.states.length,
    depth: node.prefix.length,
    prefix: reportPrefix(node),
  };
}

function reportPrefix(node: BeamNode) {
  return node.reflected
    ? node.prefix.map((column) => BOARD_SIZE - 1 - column)
    : node.prefix.slice();
}

function recordRetainedStates(
  work: MutableWork,
  currentBeam: number,
  nextBeam: number,
  candidate: number,
) {
  work.peakRetainedScenarioStates = Math.max(
    work.peakRetainedScenarioStates,
    (currentBeam + nextBeam + candidate) * work.scenarios,
  );
}

function spendWork(
  work: MutableWork,
  kind: "simulatedMoves" | "evaluatedStates",
) {
  if (work.total >= work.maxWork) {
    work.stopReason = "work";
    throw new SearchStopped();
  }
  if (performance.now() >= work.deadline) {
    work.stopReason = "time";
    throw new SearchStopped();
  }
  work.total += 1;
  work[kind] += 1;
}

function publicWork(work: MutableWork): RobustOpenLoopBeamWork {
  return {
    total: work.total,
    simulatedMoves: work.simulatedMoves,
    evaluatedStates: work.evaluatedStates,
    generatedNodes: work.generatedNodes,
    prunedNodes: work.prunedNodes,
    peakBeamNodes: work.peakBeamNodes,
    peakRetainedScenarioStates: work.peakRetainedScenarioStates,
  };
}

function planCanonicalRoots(
  state: GameState,
  rootOrder: readonly number[],
) {
  const plans: RootPlan[] = [];
  const aliases = new Map<number, number>();
  const representativeByCanonicalColumn = new Map<number, number>();
  const symmetric = compareBoardWithMirror(state.board) === 0;
  for (let priority = 0; priority < rootOrder.length; priority += 1) {
    const firstColumn = rootOrder[priority];
    const canonical = canonicalizeRootAction(state, firstColumn);
    const representative = symmetric
      ? representativeByCanonicalColumn.get(canonical.column)
      : undefined;
    if (representative !== undefined) {
      aliases.set(firstColumn, representative);
      continue;
    }
    if (symmetric) {
      representativeByCanonicalColumn.set(
        canonical.column,
        firstColumn,
      );
    }
    plans.push({ canonical, firstColumn, priority });
  }
  return { plans, aliases };
}

function mirrorColumnSummary(
  summary: OpenLoopColumnEvaluation,
  column: number,
): OpenLoopColumnEvaluation {
  return {
    ...summary,
    column,
    prefix: summary.prefix.map(
      (prefixColumn) => BOARD_SIZE - 1 - prefixColumn,
    ),
  };
}

function canonicalizeRootAction(
  state: GameState,
  column: number,
): CanonicalRoot {
  const comparison = compareBoardWithMirror(state.board);
  const reflected =
    comparison > 0 ||
    (comparison === 0 && column > Math.floor(BOARD_SIZE / 2));
  return reflected
    ? {
        state: { ...state, board: mirrorBoard(state.board), score: 0 },
        column: BOARD_SIZE - 1 - column,
        reflected,
      }
    : { state: withoutScore(state), column, reflected };
}

function columnOrderForBoard(board: Board) {
  return compareBoardWithMirror(board) <= 0
    ? COLUMN_ORDER
    : MIRRORED_COLUMN_ORDER;
}

function columnPriority(column: number) {
  return COLUMN_ORDER.indexOf(column as (typeof COLUMN_ORDER)[number]);
}

function compareBoardWithMirror(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const reflected = board[offset + BOARD_SIZE - 1 - column];
      if (forward < reflected) return -1;
      if (forward > reflected) return 1;
    }
  }
  return 0;
}

function mirrorBoard(board: Board): Board {
  const result: Cell[] = [];
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = BOARD_SIZE - 1; column >= 0; column -= 1) {
      result.push(board[row * BOARD_SIZE + column]);
    }
  }
  return result;
}

function scenarioSeedForState(state: GameState, seed: number) {
  const canonicalBoard =
    compareBoardWithMirror(state.board) <= 0
      ? state.board
      : mirrorBoard(state.board);
  let hash = (seed ^ SCENARIO_DOMAIN) >>> 0;
  for (const cell of canonicalBoard) {
    hash = Math.imul(hash ^ cell, 0x01000193) >>> 0;
  }
  for (const value of [
    state.nextDisc,
    state.level,
    state.movesRemaining,
    state.movesPlayed,
    state.gameOver ? 1 : 0,
  ]) {
    hash = Math.imul(hash ^ value, 0x01000193) >>> 0;
  }
  return mix32(hash);
}

function scenarioDisc(
  seed: number,
  scenario: number,
  scenarioCount: number,
  ply: number,
): DiscValue {
  return (Math.floor(
    stratifiedSample(seed, scenario, scenarioCount, ply, DISC_DOMAIN) *
      BOARD_SIZE,
  ) + 1) as DiscValue;
}

function scenarioRandom(
  seed: number,
  scenario: number,
  scenarioCount: number,
  ply: number,
) {
  let event = 0;
  return () => {
    const domain =
      REVEAL_DOMAIN ^ Math.imul((event + 1) >>> 0, EVENT_MULTIPLIER);
    event += 1;
    return stratifiedSample(seed, scenario, scenarioCount, ply, domain);
  };
}

function stratifiedSample(
  seed: number,
  scenario: number,
  scenarioCount: number,
  ply: number,
  domain: number,
) {
  const rotation =
    mix32(
      seed ^
        Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
        domain ^
        STRATUM_DOMAIN,
    ) % scenarioCount;
  const stratum = (scenario + rotation) % scenarioCount;
  const jitter =
    mix32(
      seed ^
        Math.imul((scenario + 1) >>> 0, SCENARIO_MULTIPLIER) ^
        Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
        domain ^
        JITTER_DOMAIN,
    ) / 4_294_967_296;
  return (stratum + jitter) / scenarioCount;
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846ca68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function withScenarioDisc(state: GameState, nextDisc: DiscValue): GameState {
  return state.nextDisc === nextDisc ? state : { ...state, nextDisc };
}

function withoutScore(state: GameState): GameState {
  return state.score === 0 ? state : { ...state, score: 0 };
}

function boundedInteger(
  value: number,
  name: string,
  minimum: number,
  maximum: number,
) {
  if (
    !Number.isSafeInteger(value) ||
    value < minimum ||
    value > maximum
  ) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`);
  }
  return value;
}

function unsignedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("seed must be a uint32 integer");
  }
  return seed >>> 0;
}
