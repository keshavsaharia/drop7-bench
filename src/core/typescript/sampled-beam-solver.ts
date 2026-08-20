import {
  BOARD_SIZE,
  EMPTY,
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

export type SampledBeamEvaluator = (state: GameState) => number;

export interface SampledBeamSolverOptions {
  /** Independent chance determinizations shared by every legal root move. */
  scenarios: number;
  /** Total simulated moves, including the candidate root move. */
  depth: number;
  /** Chance-blind beam lookahead used when selecting continuation moves. */
  policyDepth?: number;
  /** Independent planning scenarios used to choose each continuation move. */
  policyScenarios?: number;
  /** Maximum non-terminal states retained at a continuation ply. */
  beamWidth?: number;
  /** Deterministic transition/evaluation budget; checked before each unit. */
  maxWork?: number;
  /** Solver-local uint32 seed. It is unrelated to a headless game's seed. */
  seed: number;
  heuristicProfile?: HeuristicProfileName;
  /**
   * Overrides heuristicProfile. The state passed here always has score zero;
   * accumulated move rewards are tracked separately by the planner.
   */
  evaluator?: SampledBeamEvaluator;
  terminalUtility?: number;
}

export interface SampledBeamColumnEvaluation {
  column: number;
  mean: number;
  /** Population variance across sampled determinizations. */
  variance: number;
  scenarios: number;
}

export interface SampledBeamWorkStats {
  total: number;
  simulatedMoves: number;
  evaluatedStates: number;
  policySearches: number;
  deduplicatedStates: number;
  prunedStates: number;
  peakBeamSize: number;
  peakCandidateStates: number;
}

export interface SampledBeamEvaluationResult {
  bestColumn: number | null;
  columns: readonly SampledBeamColumnEvaluation[];
  scenarios: number;
  completedScenarios: number;
  depth: number;
  policyDepth: number;
  policyScenarios: number;
  beamWidth: number;
  maxWork: number;
  complete: boolean;
  seed: number;
  work: Readonly<SampledBeamWorkStats>;
}

export const MAX_SAMPLED_BEAM_DEPTH = 20;
export const MAX_SAMPLED_BEAM_POLICY_DEPTH = 6;
export const MAX_SAMPLED_BEAM_POLICY_SCENARIOS = 8;
export const MAX_SAMPLED_BEAM_WIDTH = 256;
export const MAX_SAMPLED_BEAM_SCENARIOS = 10_000;
export const MAX_SAMPLED_BEAM_WORK = 5_000_000;
export const DEFAULT_SAMPLED_BEAM_POLICY_DEPTH = 2;
export const DEFAULT_SAMPLED_BEAM_POLICY_SCENARIOS = 4;
export const DEFAULT_SAMPLED_BEAM_WIDTH = 8;

const COLUMN_ORDER = [3, 2, 4, 1, 5, 0, 6] as const;
const MIRRORED_COLUMN_ORDER = [3, 4, 2, 5, 1, 6, 0] as const;
const SCENARIO_DOMAIN = 0x5343454e;
const DISC_DOMAIN = 0x44495343;
const REVEAL_DOMAIN = 0x5245564c;
const POLICY_DOMAIN = 0x504f4c59;
const STRATUM_DOMAIN = 0x53545241;
const JITTER_DOMAIN = 0x4a495454;
const SCENARIO_MULTIPLIER = 0x9e3779b9;
const PLY_MULTIPLIER = 0x85ebca6b;
const EVENT_MULTIPLIER = 0xc2b2ae35;

interface RunningStats {
  count: number;
  mean: number;
  squaredDeviation: number;
}

interface RootPlan {
  column: number;
  canonicalColumn: number;
  canonicalState: GameState;
  stats: RunningStats;
}

interface BeamNode {
  state: GameState;
  reward: number;
  rank: number;
  key: string;
  firstColumn: number;
}

interface WorkTracker extends SampledBeamWorkStats {
  limit: number;
}

class WorkLimitReached extends Error {}

/**
 * Evaluate every legal move with solver-local sampled chance scenarios.
 *
 * An outer scenario supplies a random reveal tape at each ply and the next
 * visible disc at each subsequent ply. Before that outer tape advances, a
 * receding beam chooses the continuation action with its own domain-separated
 * planning tape. Thus the policy may react to chance already visible in its
 * GameState, but cannot choose with hindsight about the transition it is
 * about to receive. All samples derive solely from the observable position
 * and `options.seed`; no game RNG or hidden future is accepted by this API.
 * Every root receives every completed scenario batch, so neither beam pruning
 * nor an exhausted work budget can remove a root before comparison.
 */
export function evaluateSampledBeamMoves(
  state: GameState,
  options: SampledBeamSolverOptions,
): SampledBeamEvaluationResult {
  const scenarios = boundedPositiveInteger(
    options.scenarios,
    "scenarios",
    MAX_SAMPLED_BEAM_SCENARIOS,
  );
  const depth = boundedPositiveInteger(
    options.depth,
    "depth",
    MAX_SAMPLED_BEAM_DEPTH,
  );
  const policyDepth = boundedPositiveInteger(
    options.policyDepth ?? DEFAULT_SAMPLED_BEAM_POLICY_DEPTH,
    "policyDepth",
    MAX_SAMPLED_BEAM_POLICY_DEPTH,
  );
  const policyScenarios = boundedPositiveInteger(
    options.policyScenarios ?? DEFAULT_SAMPLED_BEAM_POLICY_SCENARIOS,
    "policyScenarios",
    MAX_SAMPLED_BEAM_POLICY_SCENARIOS,
  );
  const beamWidth = boundedPositiveInteger(
    options.beamWidth ?? DEFAULT_SAMPLED_BEAM_WIDTH,
    "beamWidth",
    MAX_SAMPLED_BEAM_WIDTH,
  );
  const seed = unsignedSeed(options.seed);
  const maxWork = boundedPositiveInteger(
    options.maxWork ?? MAX_SAMPLED_BEAM_WORK,
    "maxWork",
    MAX_SAMPLED_BEAM_WORK,
  );
  const terminalUtility =
    options.terminalUtility ?? HEURISTIC_GAME_OVER_UTILITY;
  if (!Number.isFinite(terminalUtility)) {
    throw new Error("terminalUtility must be finite");
  }

  const profile = options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const evaluator =
    options.evaluator ??
    ((position: GameState) => evaluateHeuristic(position, profile));
  const work = createWorkStats(maxWork);
  const observableScenarioSeed = scenarioSeedForState(state, seed);
  const roots: RootPlan[] = [];

  if (!state.gameOver) {
    for (const column of legalColumns(state.board)) {
      const canonical = canonicalizeRootAction(state, column);
      roots.push({
        column,
        canonicalColumn: canonical.column,
        canonicalState: canonical.state,
        stats: createRunningStats(),
      });
    }
  }

  // Scenario-major traversal makes the common-random-number comparison
  // explicit: no root is skipped or globally pruned within a scenario.
  let completedScenarios = 0;
  for (let scenario = 0; scenario < scenarios; scenario += 1) {
    const utilities: number[] = [];
    try {
      for (const root of roots) {
        utilities.push(
          evaluateRootScenario(
            root.canonicalState,
            root.canonicalColumn,
            scenario,
            scenarios,
            depth,
            policyDepth,
            policyScenarios,
            beamWidth,
            observableScenarioSeed,
            evaluator,
            terminalUtility,
            work,
          ),
        );
      }
    } catch (error) {
      if (error instanceof WorkLimitReached) break;
      throw error;
    }
    for (let rootIndex = 0; rootIndex < roots.length; rootIndex += 1) {
      updateRunningStats(roots[rootIndex].stats, utilities[rootIndex]);
    }
    completedScenarios += 1;
  }

  const columns = roots.map(({ column, stats }) => ({
    column,
    mean: stats.mean,
    variance:
      stats.count === 0
        ? 0
        : Math.max(0, stats.squaredDeviation / stats.count),
    scenarios: stats.count,
  }));

  return {
    bestColumn: chooseBestRootColumn(state.board, columns),
    columns,
    scenarios,
    completedScenarios,
    depth,
    policyDepth,
    policyScenarios,
    beamWidth,
    maxWork,
    complete: completedScenarios === scenarios,
    seed,
    work: publicWorkStats(work),
  };
}

function evaluateRootScenario(
  initial: GameState,
  rootColumn: number,
  scenario: number,
  scenarioCount: number,
  depth: number,
  policyDepth: number,
  policyScenarios: number,
  beamWidth: number,
  scenarioSeed: number,
  evaluator: SampledBeamEvaluator,
  terminalUtility: number,
  work: WorkTracker,
) {
  const rootMove = playScenarioMove(
    initial,
    rootColumn,
    scenarioSeed,
    scenario,
    scenarioCount,
    0,
    work,
  );
  if (!rootMove) return terminalUtility;

  const rootReward = rootMove.scoreDelta;
  if (rootMove.state.gameOver) return rootReward + terminalUtility;

  const rootState = withScenarioDisc(
    withoutScore(rootMove.state),
    scenarioDisc(scenarioSeed, scenario, scenarioCount, 1),
  );
  work.peakBeamSize = Math.max(work.peakBeamSize, 1);
  if (depth === 1) {
    return rootReward + evaluateLeaf(rootState, evaluator, work);
  }

  let state = rootState;
  let reward = rootReward;
  for (let ply = 1; ply < depth; ply += 1) {
    const column = chooseChanceBlindBeamColumn(
      state,
      scenarioSeed,
      Math.min(policyDepth, depth - ply),
      policyScenarios,
      beamWidth,
      evaluator,
      terminalUtility,
      work,
    );
    if (column === null) return reward + terminalUtility;
    const move = playScenarioMove(
      state,
      column,
      scenarioSeed,
      scenario,
      scenarioCount,
      ply,
      work,
    );
    if (!move) return reward + terminalUtility;
    reward += move.scoreDelta;
    if (move.state.gameOver) return reward + terminalUtility;
    state = withScenarioDisc(
      withoutScore(move.state),
      scenarioDisc(
        scenarioSeed,
        scenario,
        scenarioCount,
        ply + 1,
      ),
    );
  }

  return reward + evaluateLeaf(state, evaluator, work);
}

/**
 * Choose before the outer transition is sampled. The beam's artificial
 * future is keyed only by the currently observable state and a separate
 * policy domain, so reaching the same state in two outer scenarios produces
 * the same action and cannot leak either scenario's future chance tape.
 */
function chooseChanceBlindBeamColumn(
  observedState: GameState,
  solverSeed: number,
  depth: number,
  policyScenarios: number,
  beamWidth: number,
  evaluator: SampledBeamEvaluator,
  terminalUtility: number,
  work: WorkTracker,
) {
  work.policySearches += 1;
  const canonical = canonicalizeState(observedState);
  const policySeed = scenarioSeedForState(
    canonical.state,
    mix32(solverSeed ^ POLICY_DOMAIN),
  );
  const roots: Array<{ column: number; stats: RunningStats }> = [];
  for (const column of columnOrderForBoard(canonical.state.board)) {
    if (canonical.state.board[column] !== EMPTY) continue;
    roots.push({ column, stats: createRunningStats() });
  }

  for (let scenario = 0; scenario < policyScenarios; scenario += 1) {
    for (const root of roots) {
      updateRunningStats(
        root.stats,
        evaluatePolicyRootScenario(
          canonical.state,
          root.column,
          policySeed,
          scenario,
          policyScenarios,
          depth,
          beamWidth,
          evaluator,
          terminalUtility,
          work,
        ),
      );
    }
  }

  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const root of roots) {
    if (root.stats.mean > bestValue) {
      bestValue = root.stats.mean;
      bestColumn = root.column;
    }
  }
  if (bestColumn === null) return null;
  return canonical.reflected
    ? BOARD_SIZE - 1 - bestColumn
    : bestColumn;
}

function evaluatePolicyRootScenario(
  initial: GameState,
  rootColumn: number,
  policySeed: number,
  scenario: number,
  scenarioCount: number,
  depth: number,
  beamWidth: number,
  evaluator: SampledBeamEvaluator,
  terminalUtility: number,
  work: WorkTracker,
) {
  const rootMove = playScenarioMove(
    initial,
    rootColumn,
    policySeed,
    scenario,
    scenarioCount,
    0,
    work,
  );
  if (!rootMove) return terminalUtility;
  const rootReward = rootMove.scoreDelta;
  if (rootMove.state.gameOver) return rootReward + terminalUtility;
  const rootState = withScenarioDisc(
    withoutScore(rootMove.state),
    scenarioDisc(policySeed, scenario, scenarioCount, 1),
  );
  if (depth === 1) {
    return rootReward + evaluateLeaf(rootState, evaluator, work);
  }

  let beam: BeamNode[] = [
    {
      state: rootState,
      reward: rootReward,
      rank: Number.NEGATIVE_INFINITY,
      key: dynamicStateKey(rootState),
      firstColumn: rootColumn,
    },
  ];
  let bestCompleted = Number.NEGATIVE_INFINITY;

  for (let ply = 1; ply < depth; ply += 1) {
    const candidates: BeamNode[] = [];
    for (const node of beam) {
      for (const column of columnOrderForBoard(node.state.board)) {
        if (node.state.board[column] !== EMPTY) continue;
        const move = playScenarioMove(
          node.state,
          column,
          policySeed,
          scenario,
          scenarioCount,
          ply,
          work,
        );
        if (!move) continue;
        const reward = node.reward + move.scoreDelta;
        if (move.state.gameOver) {
          bestCompleted = Math.max(
            bestCompleted,
            reward + terminalUtility,
          );
          continue;
        }
        const state = withScenarioDisc(
          withoutScore(move.state),
          scenarioDisc(policySeed, scenario, scenarioCount, ply + 1),
        );
        candidates.push({
          state,
          reward,
          rank: 0,
          key: dynamicStateKey(state),
          firstColumn: rootColumn,
        });
      }
    }

    if (candidates.length === 0) {
      return Number.isFinite(bestCompleted)
        ? bestCompleted
        : terminalUtility;
    }
    beam = deduplicateRankAndPrune(
      candidates,
      beamWidth,
      evaluator,
      work,
    );
    if (ply + 1 === depth) {
      return Math.max(bestCompleted, beam[0].rank);
    }
  }

  return Number.isFinite(bestCompleted) ? bestCompleted : terminalUtility;
}

function playScenarioMove(
  state: GameState,
  column: number,
  scenarioSeed: number,
  scenario: number,
  scenarioCount: number,
  ply: number,
  work: WorkTracker,
) {
  spendWork(work, "simulatedMoves");
  return playMove(
    state,
    column,
    scenarioRandom(scenarioSeed, scenario, scenarioCount, ply),
    { captureAnimation: false },
  );
}

function evaluateLeaf(
  state: GameState,
  evaluator: SampledBeamEvaluator,
  work: WorkTracker,
) {
  spendWork(work, "evaluatedStates");
  // A fresh shell prevents an evaluator from changing the score carried by a
  // beam node. Board evaluation is intentionally independent of game score.
  const value = evaluator({ ...state, score: 0 });
  if (!Number.isFinite(value)) {
    throw new Error("Sampled beam evaluator must return a finite number");
  }
  return value;
}

function withoutScore(state: GameState): GameState {
  return state.score === 0 ? state : { ...state, score: 0 };
}

function withScenarioDisc(state: GameState, nextDisc: DiscValue): GameState {
  return state.nextDisc === nextDisc ? state : { ...state, nextDisc };
}

function dynamicStateKey(state: GameState) {
  // Score is deliberately absent: reward-to-date is stored on BeamNode, and
  // equivalent dynamic states retain only the path with the greatest reward.
  return `${state.board.join("")}|${state.nextDisc}|${state.level}|${state.movesRemaining}|${state.movesPlayed}|${state.gameOver ? 1 : 0}`;
}

function deduplicateRankAndPrune(
  candidates: readonly BeamNode[],
  beamWidth: number,
  evaluator: SampledBeamEvaluator,
  work: WorkTracker,
) {
  const unique = new Map<string, BeamNode>();
  for (const candidate of candidates) {
    const previous = unique.get(candidate.key);
    if (previous) {
      work.deduplicatedStates += 1;
      if (!dominatesEquivalentState(candidate, previous)) continue;
    }
    unique.set(candidate.key, candidate);
  }
  work.peakCandidateStates = Math.max(
    work.peakCandidateStates,
    unique.size,
  );
  const ranked = [...unique.values()];
  for (const candidate of ranked) {
    candidate.rank =
      candidate.reward + evaluateLeaf(candidate.state, evaluator, work);
  }
  ranked.sort(compareBeamNodes);
  work.prunedStates += Math.max(0, ranked.length - beamWidth);
  const beam = ranked.slice(0, beamWidth);
  work.peakBeamSize = Math.max(work.peakBeamSize, beam.length);
  return beam;
}

function dominatesEquivalentState(
  candidate: BeamNode,
  previous: BeamNode,
) {
  if (candidate.reward !== previous.reward) {
    return candidate.reward > previous.reward;
  }
  return (
    columnPriority(candidate.firstColumn) <
    columnPriority(previous.firstColumn)
  );
}

function compareBeamNodes(first: BeamNode, second: BeamNode) {
  const valueDifference = second.rank - first.rank;
  if (valueDifference !== 0) return valueDifference;
  const rewardDifference = second.reward - first.reward;
  if (rewardDifference !== 0) return rewardDifference;
  const columnDifference =
    columnPriority(first.firstColumn) - columnPriority(second.firstColumn);
  if (columnDifference !== 0) return columnDifference;
  if (first.key < second.key) return -1;
  if (first.key > second.key) return 1;
  return 0;
}

function chooseBestRootColumn(
  board: Board,
  columns: readonly SampledBeamColumnEvaluation[],
) {
  let bestColumn: number | null = null;
  let bestValue = Number.NEGATIVE_INFINITY;
  for (const column of columnOrderForBoard(board)) {
    const evaluation = columns.find((entry) => entry.column === column);
    const value = evaluation?.scenarios ? evaluation.mean : undefined;
    if (value !== undefined && value > bestValue) {
      bestValue = value;
      bestColumn = column;
    }
  }
  return bestColumn;
}

function columnPriority(column: number) {
  return COLUMN_ORDER.indexOf(column as (typeof COLUMN_ORDER)[number]);
}

/**
 * Canonicalizing the observable board/action pair makes mirrored candidates
 * consume the exact same reveal tape, even when one cascade reveals several
 * covered cells in scan-order-dependent positions.
 */
function canonicalizeRootAction(state: GameState, column: number) {
  const boardComparison = compareBoardWithMirror(state.board);
  const reflect =
    boardComparison > 0 ||
    (boardComparison === 0 && column > Math.floor(BOARD_SIZE / 2));
  if (!reflect) {
    return { state: withoutScore(state), column };
  }
  return {
    state: { ...state, board: mirrorBoard(state.board), score: 0 },
    column: BOARD_SIZE - 1 - column,
  };
}

function canonicalizeState(state: GameState) {
  const reflected = compareBoardWithMirror(state.board) > 0;
  return {
    reflected,
    state: reflected
      ? { ...state, board: mirrorBoard(state.board), score: 0 }
      : withoutScore(state),
  };
}

function columnOrderForBoard(board: Board) {
  return compareBoardWithMirror(board) <= 0
    ? COLUMN_ORDER
    : MIRRORED_COLUMN_ORDER;
}

function compareBoardWithMirror(board: Board) {
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    const offset = row * BOARD_SIZE;
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const forward = board[offset + column];
      const mirrored = board[offset + BOARD_SIZE - 1 - column];
      if (forward < mirrored) return -1;
      if (forward > mirrored) return 1;
    }
  }
  return 0;
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
  const sample = stratifiedSample(
    seed,
    scenario,
    scenarioCount,
    ply,
    DISC_DOMAIN,
  );
  return (Math.floor(sample * BOARD_SIZE) + 1) as DiscValue;
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
    return stratifiedSample(
      seed,
      scenario,
      scenarioCount,
      ply,
      domain,
    );
  };
}

/**
 * Latin-hypercube style common samples. At every chance coordinate, each
 * scenario occupies a distinct equal-width stratum. A deterministic rotation
 * and within-stratum jitter avoid locking a scenario to one part of the
 * distribution across plies while retaining correct uniform marginals.
 */
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
  const jitter = mixedSample(
    seed,
    scenario,
    ply,
    domain ^ JITTER_DOMAIN,
  );
  return (stratum + jitter) / scenarioCount;
}

function mixedSample(
  seed: number,
  scenario: number,
  ply: number,
  domain: number,
) {
  return (
    mix32(
      seed ^
        Math.imul((scenario + 1) >>> 0, SCENARIO_MULTIPLIER) ^
        Math.imul((ply + 1) >>> 0, PLY_MULTIPLIER) ^
        domain,
    ) / 4_294_967_296
  );
}

function mix32(value: number) {
  let mixed = value >>> 0;
  mixed = Math.imul(mixed ^ (mixed >>> 16), 0x7feb352d);
  mixed = Math.imul(mixed ^ (mixed >>> 15), 0x846ca68b);
  return (mixed ^ (mixed >>> 16)) >>> 0;
}

function createRunningStats(): RunningStats {
  return { count: 0, mean: 0, squaredDeviation: 0 };
}

function updateRunningStats(stats: RunningStats, value: number) {
  stats.count += 1;
  const difference = value - stats.mean;
  stats.mean += difference / stats.count;
  stats.squaredDeviation += difference * (value - stats.mean);
}

function createWorkStats(limit: number): WorkTracker {
  return {
    total: 0,
    simulatedMoves: 0,
    evaluatedStates: 0,
    policySearches: 0,
    deduplicatedStates: 0,
    prunedStates: 0,
    peakBeamSize: 0,
    peakCandidateStates: 0,
    limit,
  };
}

function spendWork(
  work: WorkTracker,
  kind: "simulatedMoves" | "evaluatedStates",
) {
  if (work.total >= work.limit) throw new WorkLimitReached();
  work.total += 1;
  work[kind] += 1;
}

function publicWorkStats(work: WorkTracker): SampledBeamWorkStats {
  return {
    total: work.total,
    simulatedMoves: work.simulatedMoves,
    evaluatedStates: work.evaluatedStates,
    policySearches: work.policySearches,
    deduplicatedStates: work.deduplicatedStates,
    prunedStates: work.prunedStates,
    peakBeamSize: work.peakBeamSize,
    peakCandidateStates: work.peakCandidateStates,
  };
}

function boundedPositiveInteger(value: number, name: string, maximum: number) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    throw new Error(`${name} must be an integer from 1 to ${maximum}`);
  }
  return value;
}

function unsignedSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("seed must be a uint32 integer");
  }
  return seed >>> 0;
}
