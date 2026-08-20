import { pathToFileURL } from "node:url";
import {
  CLEAR_BONUS,
  LEVEL_BONUS,
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { evaluateHeuristic } from "../../../src/core/typescript/heuristic.ts";
import { headlessDisc } from "../../../src/core/typescript/headless.ts";

/**
 * UPPER-BOUND DIAGNOSTIC ONLY.
 *
 * This intentionally unfair oracle can see the headless game's future disc
 * and reveal streams. It is useful for testing whether long-horizon play can
 * reach a target score under this simulator, but it is not a deployable or
 * statistically fair Drop7 policy. Beam pruning also means it is not a proof
 * of the true perfect-information optimum.
 */

export interface OracleGameOptions {
  seed: number;
  depth: number;
  beamWidth: number;
  maxMoves: number;
}

export interface OracleGameResult {
  seed: number;
  score: number;
  moves: number;
  finalLevel: number;
  gameOver: boolean;
  censored: boolean;
  clears: number;
  maxChain: number;
  generatedStates: number;
  deduplicatedStates: number;
  peakCandidateStates: number;
  elapsedMs: number;
}

export interface OracleArguments {
  seeds: number[];
  depth: number;
  beamWidth: number;
  maxMoves: number;
  smoke: boolean;
}

interface BeamNode {
  state: GameState;
  firstColumn: number | null;
  dynamicKey: string;
  rank: number;
}

interface PlanResult {
  column: number | null;
  generatedStates: number;
  deduplicatedStates: number;
  peakCandidateStates: number;
}

interface OracleStep {
  move: MoveResult;
  state: GameState;
}

const DEFAULT_DEPTH = 12;
const DEFAULT_BEAM_WIDTH = 512;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_GAME_COUNT = 1;
const MAX_GAME_COUNT = 100_000;
const MAX_BEAM_WIDTH = 100_000;
const TERMINAL_PENALTY = -1_000_000_000;

// These constants reproduce the public simulator's per-move reveal stream so
// the diagnostic oracle can be compared against fair policies on paired games.
const REVEAL_DOMAIN = 0x5245564c;
const REVEAL_MOVE_MULTIPLIER = 0x85ebca6b;

const DIAGNOSTIC_BANNER =
  "Drop7 perfect-information oracle — UPPER-BOUND DIAGNOSTIC ONLY (not a fair policy)";

export function runOracleGame(options: OracleGameOptions): OracleGameResult {
  const startedAt = performance.now();
  const seed = normalizeSeed(options.seed);
  const depth = positiveInteger(options.depth, "depth");
  const beamWidth = positiveInteger(options.beamWidth, "beamWidth");
  const maxMoves = positiveInteger(options.maxMoves, "maxMoves");
  if (beamWidth > MAX_BEAM_WIDTH) {
    throw new Error(`beamWidth cannot exceed ${MAX_BEAM_WIDTH}`);
  }

  let state: GameState = {
    board: createInitialBoard(),
    nextDisc: headlessDisc(seed, 0),
    score: 0,
    level: 1,
    movesRemaining: MOVES_PER_LEVEL,
    movesPlayed: 0,
    gameOver: false,
  };
  let clears = 0;
  let maxChain = 0;
  let generatedStates = 0;
  let deduplicatedStates = 0;
  let peakCandidateStates = 0;

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    // Receding horizon: deliberately commit only the best path's first move,
    // then rebuild the beam from the actual successor on the next turn.
    const plan = planOracleMove(state, seed, depth, beamWidth);
    if (plan.column === null) {
      throw new Error("Oracle found no legal move for a live Drop7 game");
    }
    generatedStates += plan.generatedStates;
    deduplicatedStates += plan.deduplicatedStates;
    peakCandidateStates = Math.max(
      peakCandidateStates,
      plan.peakCandidateStates,
    );

    const step = playOracleMove(state, plan.column, seed);
    if (!step) {
      throw new Error(`Oracle chose illegal column ${plan.column}`);
    }
    clears += clearCount(step.move);
    maxChain = Math.max(maxChain, step.move.waves.length);
    state = step.state;
  }

  return {
    seed,
    score: state.score,
    moves: state.movesPlayed,
    finalLevel: state.level,
    gameOver: state.gameOver,
    censored: !state.gameOver,
    clears,
    maxChain,
    generatedStates,
    deduplicatedStates,
    peakCandidateStates,
    elapsedMs: Math.max(0, performance.now() - startedAt),
  };
}

/** Find one move with an unfair, perfect-information receding-horizon beam. */
export function planOracleMove(
  root: GameState,
  seed: number,
  depth: number,
  beamWidth: number,
): PlanResult {
  const normalizedSeed = normalizeSeed(seed);
  positiveInteger(depth, "depth");
  positiveInteger(beamWidth, "beamWidth");
  if (beamWidth > MAX_BEAM_WIDTH) {
    throw new Error(`beamWidth cannot exceed ${MAX_BEAM_WIDTH}`);
  }

  const rootKey = dynamicStateKey(root);
  let beam: BeamNode[] = [
    {
      state: root,
      firstColumn: null,
      dynamicKey: rootKey,
      rank: rankState(root),
    },
  ];
  let generatedStates = 0;
  let deduplicatedStates = 0;
  let peakCandidateStates = 1;

  for (let ply = 0; ply < depth; ply += 1) {
    // At most seven successors per retained state are live at once. The map
    // also merges dynamically identical positions, keeping retained memory at
    // O(7 * beamWidth), independent of the full game-tree size.
    const candidates = new Map<string, BeamNode>();

    for (const node of beam) {
      if (node.state.gameOver) {
        insertCandidate(candidates, node);
        continue;
      }

      for (const column of legalColumns(node.state.board)) {
        const step = playOracleMove(node.state, column, normalizedSeed);
        if (!step) continue;
        generatedStates += 1;
        const dynamicKey = dynamicStateKey(step.state);
        const candidate: BeamNode = {
          state: step.state,
          firstColumn: node.firstColumn ?? column,
          dynamicKey,
          // Ranking is filled after deduplication, avoiding repeated feature
          // extraction for paths that converge on the same position.
          rank: 0,
        };
        if (candidates.has(dynamicKey)) deduplicatedStates += 1;
        insertCandidate(candidates, candidate);
      }
    }

    if (candidates.size === 0) break;
    peakCandidateStates = Math.max(peakCandidateStates, candidates.size);
    const ranked = [...candidates.values()];
    for (const node of ranked) node.rank = rankState(node.state);
    ranked.sort(compareBeamNodes);
    beam = ranked.slice(0, beamWidth);
  }

  beam.sort(compareBeamNodes);
  return {
    column: beam.find((node) => node.firstColumn !== null)?.firstColumn ?? null,
    generatedStates,
    deduplicatedStates,
    peakCandidateStates,
  };
}

/** Apply exactly the same future-disc/reveal mapping as headless mode. */
function playOracleMove(
  state: GameState,
  column: number,
  seed: number,
): OracleStep | null {
  const moveNumber = state.movesPlayed;
  const revealSeed = mix32(
    seed ^
      Math.imul((moveNumber + 1) >>> 0, REVEAL_MOVE_MULTIPLIER) ^
      REVEAL_DOMAIN,
  );
  // Every candidate gets a fresh copy of this move's stream. Otherwise the
  // order in which beam branches are expanded would change reveal values.
  const move = playMove(state, column, seededRandom(revealSeed), {
    captureAnimation: false,
  });
  if (!move) return null;

  const nextState = move.state.gameOver
    ? move.state
    : {
        ...move.state,
        nextDisc: headlessDisc(seed, move.state.movesPlayed),
      };
  return { move, state: nextState };
}

function insertCandidate(
  candidates: Map<string, BeamNode>,
  candidate: BeamNode,
) {
  const previous = candidates.get(candidate.dynamicKey);
  if (!previous || dominatesEquivalentState(candidate, previous)) {
    candidates.set(candidate.dynamicKey, candidate);
  }
}

/** For an identical future position, more accumulated score always dominates. */
function dominatesEquivalentState(candidate: BeamNode, previous: BeamNode) {
  if (candidate.state.score !== previous.state.score) {
    return candidate.state.score > previous.state.score;
  }
  return firstColumnOrder(candidate.firstColumn, previous.firstColumn) < 0;
}

function rankState(state: GameState) {
  return (
    state.score +
    evaluateHeuristic(state, "combined") +
    (state.gameOver ? TERMINAL_PENALTY : 0)
  );
}

function compareBeamNodes(first: BeamNode, second: BeamNode) {
  return (
    second.rank - first.rank ||
    second.state.score - first.state.score ||
    firstColumnOrder(first.firstColumn, second.firstColumn) ||
    first.dynamicKey.localeCompare(second.dynamicKey)
  );
}

function firstColumnOrder(first: number | null, second: number | null) {
  return (first ?? 7) - (second ?? 7);
}

/**
 * Score accounting recovers exact clears, including the rare possibility of
 * one clear before a level rise and another clear after the new covered row.
 */
function clearCount(move: MoveResult) {
  const wavePoints = move.waves.reduce((sum, wave) => sum + wave.points, 0);
  const clearPoints =
    move.scoreDelta - wavePoints - (move.levelAdvanced ? LEVEL_BONUS : 0);
  const clears = clearPoints / CLEAR_BONUS;
  if (!Number.isInteger(clears) || clears < 0) {
    throw new Error("Move score could not be decomposed into clear bonuses");
  }
  return clears;
}

/** Score is omitted so converged positions retain only their dominant path. */
function dynamicStateKey(state: GameState) {
  return `${state.board.join("")}|${state.nextDisc}|${state.level}|${state.movesRemaining}|${state.movesPlayed}|${state.gameOver ? 1 : 0}`;
}

export function runCli(arguments_: readonly string[]) {
  const parsed = parseArguments(arguments_);
  if (parsed === null) {
    process.stdout.write(helpText());
    return;
  }
  if (parsed.smoke) {
    runSmokeTest();
    return;
  }

  process.stdout.write(`${DIAGNOSTIC_BANNER}\n`);
  process.stdout.write(
    `depth ${parsed.depth} · beam ${formatInteger(parsed.beamWidth)} · cap ${formatInteger(parsed.maxMoves)} moves · ${parsed.seeds.length} seed(s)\n\n`,
  );

  const results: OracleGameResult[] = [];
  for (const seed of parsed.seeds) {
    const result = runOracleGame({
      seed,
      depth: parsed.depth,
      beamWidth: parsed.beamWidth,
      maxMoves: parsed.maxMoves,
    });
    results.push(result);
    process.stdout.write(`${formatGame(result)}\n`);
  }
  process.stdout.write(`\n${formatSummary(results)}\n`);
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runCli(process.argv.slice(2));
}

export function parseArguments(
  arguments_: readonly string[],
): OracleArguments | null {
  let seedStart = 1;
  let gameCount = DEFAULT_GAME_COUNT;
  let explicitSeeds: number[] | undefined;
  let depth = DEFAULT_DEPTH;
  let beamWidth = DEFAULT_BEAM_WIDTH;
  let maxMoves = DEFAULT_MAX_MOVES;
  let smoke = false;

  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--help" || flag === "-h") return null;
    if (flag === "--smoke") {
      smoke = true;
      continue;
    }
    const value = arguments_[index + 1];
    if (value === undefined) throw new Error(`Missing value after ${flag}`);
    index += 1;

    switch (flag) {
      case "--seed":
        seedStart = parseInteger(value, flag);
        break;
      case "--seeds":
        explicitSeeds = parseSeeds(value);
        break;
      case "--games":
        gameCount = parsePositiveInteger(value, flag);
        if (gameCount > MAX_GAME_COUNT) {
          throw new Error(`--games cannot exceed ${MAX_GAME_COUNT}`);
        }
        break;
      case "--depth":
        depth = parsePositiveInteger(value, flag);
        break;
      case "--beam":
        beamWidth = parsePositiveInteger(value, flag);
        if (beamWidth > MAX_BEAM_WIDTH) {
          throw new Error(`--beam cannot exceed ${MAX_BEAM_WIDTH}`);
        }
        break;
      case "--max-moves":
      case "--maxMoves":
        maxMoves = parsePositiveInteger(value, flag);
        break;
      default:
        throw new Error(`Unknown option ${flag}`);
    }
  }

  const seeds = [
    ...new Set(
      explicitSeeds ??
        Array.from({ length: gameCount }, (_, offset) => seedStart + offset),
    ),
  ];
  for (const seed of seeds) normalizeSeed(seed);
  return { seeds, depth, beamWidth, maxMoves, smoke };
}

function parseSeeds(value: string) {
  const range = /^(?<start>-?\d+)\.\.(?<end>-?\d+)$/.exec(value);
  if (!range?.groups) {
    const seeds = value.split(",").map((seed) => parseInteger(seed, "--seeds"));
    if (seeds.length > MAX_GAME_COUNT) {
      throw new Error(`--seeds cannot contain more than ${MAX_GAME_COUNT} games`);
    }
    return seeds;
  }

  const start = parseInteger(range.groups.start, "--seeds");
  const end = parseInteger(range.groups.end, "--seeds");
  const length = Math.abs(end - start) + 1;
  if (length > MAX_GAME_COUNT) {
    throw new Error(`--seeds cannot contain more than ${MAX_GAME_COUNT} games`);
  }
  const direction = start <= end ? 1 : -1;
  return Array.from(
    { length },
    (_, offset) => start + offset * direction,
  );
}

function runSmokeTest() {
  const options: OracleGameOptions = {
    seed: 7,
    depth: 3,
    beamWidth: 16,
    maxMoves: 7,
  };
  const first = runOracleGame(options);
  const second = runOracleGame(options);
  const deterministicFields = (result: OracleGameResult) => ({
    score: result.score,
    moves: result.moves,
    finalLevel: result.finalLevel,
    gameOver: result.gameOver,
    clears: result.clears,
    maxChain: result.maxChain,
    generatedStates: result.generatedStates,
    deduplicatedStates: result.deduplicatedStates,
    peakCandidateStates: result.peakCandidateStates,
  });
  if (
    JSON.stringify(deterministicFields(first)) !==
    JSON.stringify(deterministicFields(second))
  ) {
    throw new Error("Oracle smoke test was not deterministic");
  }
  if (first.moves === 0 || first.score < 0) {
    throw new Error("Oracle smoke test did not produce a valid game prefix");
  }
  process.stdout.write(
    `${DIAGNOSTIC_BANNER}\nsmoke ok · seed ${first.seed} · ${formatInteger(first.score)} points · ${first.moves} moves\n`,
  );
}

function formatGame(result: OracleGameResult) {
  const status = result.censored ? "censored" : "game over";
  return [
    `seed ${result.seed}`,
    `${formatInteger(result.score)} points`,
    `${result.moves} moves`,
    `${result.clears} clears`,
    `max chain ${result.maxChain}`,
    status,
    `${(result.elapsedMs / 1_000).toFixed(2)}s`,
  ].join(" · ");
}

function formatSummary(results: readonly OracleGameResult[]) {
  const mean = (values: readonly number[]) =>
    values.reduce((sum, value) => sum + value, 0) / values.length;
  return [
    `mean ${formatInteger(mean(results.map((result) => result.score)))} points`,
    `${mean(results.map((result) => result.moves)).toFixed(1)} moves`,
    `${mean(results.map((result) => result.clears)).toFixed(2)} clears`,
    `mean max chain ${mean(results.map((result) => result.maxChain)).toFixed(2)}`,
    `${results.filter((result) => result.censored).length}/${results.length} censored`,
  ].join(" · ");
}

function helpText() {
  return `${DIAGNOSTIC_BANNER}

Usage:
  node --experimental-strip-types approaches/oracle-curriculum/perfect-information-oracle/main.ts [options]

Options:
  --seed <n>           First seed when using --games (default: 1)
  --seeds <list>       Comma list or inclusive range, e.g. 1,7,9 or 1..8
  --games <n>          Consecutive seeds to run (default: ${DEFAULT_GAME_COUNT})
  --depth <ply>        Perfect-information lookahead (default: ${DEFAULT_DEPTH})
  --beam <states>      States retained per ply (default: ${DEFAULT_BEAM_WIDTH})
  --max-moves <n>      Per-game move cap (default: ${DEFAULT_MAX_MOVES})
  --smoke              Run a small deterministic self-check
  --help, -h           Show this help

This oracle sees future random streams and is only an optimistic diagnostic.
It is not a fair policy, and beam pruning does not guarantee the true optimum.
`;
}

function parsePositiveInteger(value: string, flag: string) {
  const parsed = parseInteger(value, flag);
  if (parsed < 1) throw new Error(`${flag} must be at least 1`);
  return parsed;
}

function parseInteger(value: string, flag: string) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(`${flag} must be an integer`);
  }
  return parsed;
}

function positiveInteger(value: number, label: string) {
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new Error(`${label} must be a positive integer`);
  }
  return value;
}

function normalizeSeed(seed: number) {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("Seeds must be uint32 integers");
  }
  return seed >>> 0;
}

function mix32(input: number) {
  let value = input >>> 0;
  value ^= value >>> 16;
  value = Math.imul(value, 0x7feb352d);
  value ^= value >>> 15;
  value = Math.imul(value, 0x846ca68b);
  value ^= value >>> 16;
  return value >>> 0;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}
