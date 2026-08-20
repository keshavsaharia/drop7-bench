import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  playMove,
  seededRandom,
  type DiscValue,
  type GameState,
} from "./engine.ts";
import {
  DEFAULT_HEURISTIC_PROFILE,
  type HeuristicProfileName,
} from "./heuristic.ts";
import { evaluateMoves } from "./solver.ts";

export interface HeadlessSearchOptions {
  maxDepth: number;
  /** Wall-clock guard. Omit it when using maxWork for reproducible trials. */
  timeLimitMs?: number;
  /** Deterministic chance/decision work budget for paired experiments. */
  maxWork?: number;
}

export interface HeadlessGameOptions {
  seed: number;
  heuristicProfile?: HeuristicProfileName;
  search: HeadlessSearchOptions;
  maxMoves?: number;
  trace?: boolean;
}

export interface HeadlessMoveTrace {
  move: number;
  disc: DiscValue;
  column: number;
  scoreDelta: number;
  score: number;
  chainDepth: number;
  completedDepth: number;
  searchWork: number;
}

export interface HeadlessGameResult {
  seed: number;
  heuristicProfile: HeuristicProfileName;
  score: number;
  moves: number;
  finalLevel: number;
  gameOver: boolean;
  censored: boolean;
  clears: number;
  chains: number;
  maxChain: number;
  discsCleared: number;
  coveredRevealed: number;
  searchNodes: number;
  searchWork: number;
  cacheHits: number;
  incompleteSearches: number;
  depthZeroSearches: number;
  completedDepths: Readonly<Record<number, number>>;
  elapsedMs: number;
  trace?: readonly HeadlessMoveTrace[];
}

export interface HeadlessTournamentOptions {
  profiles: readonly HeuristicProfileName[];
  seeds: readonly number[];
  search: HeadlessSearchOptions;
  maxMoves?: number;
  onGameComplete?: (
    game: HeadlessGameResult,
    completed: number,
    total: number,
  ) => void;
}

export interface HeadlessProfileSummary {
  heuristicProfile: HeuristicProfileName;
  games: number;
  completedGames: number;
  meanScore: number | null;
  medianScore: number | null;
  p10Score: number | null;
  p90Score: number | null;
  minimumScore: number | null;
  maximumScore: number | null;
  meanMoves: number;
  meanFinalLevel: number;
  meanCompletedDepth: number;
  censoredGames: number;
  incompleteSearches: number;
  depthZeroSearches: number;
  meanSearchWorkPerMove: number;
  meanCacheHitsPerMove: number;
  pairedGames: number;
  pairedMeanDelta: number | null;
  pairedMedianDelta: number | null;
  pairedDelta95: readonly [number, number] | null;
  wins: number;
  ties: number;
  losses: number;
}

export interface HeadlessTournamentResult {
  referenceProfile: HeuristicProfileName;
  games: readonly HeadlessGameResult[];
  summaries: readonly HeadlessProfileSummary[];
}

const DEFAULT_MAX_MOVES = 500;
const NEXT_DISC_DOMAIN = 0x4e455854;
const REVEAL_DOMAIN = 0x5245564c;

/**
 * The upcoming-disc stream is keyed only by game seed and move number. Gray
 * reveals use a separate per-move stream, so a policy that opens more covers
 * does not silently receive a different sequence of future discs. Reveal
 * values keep the correct uniform marginal, but are not treated as a shared
 * latent board once two policies reach different positions.
 */
export function headlessDisc(seed: number, move: number): DiscValue {
  const sample = mix32(
    (seed >>> 0) ^ Math.imul((move + 1) >>> 0, 0x9e3779b9) ^ NEXT_DISC_DOMAIN,
  );
  return (Math.floor((sample / 4_294_967_296) * 7) + 1) as DiscValue;
}

export function runHeadlessGame(
  options: HeadlessGameOptions,
): HeadlessGameResult {
  const startedAt = performance.now();
  const seed = normalizeGameSeed(options.seed);
  const heuristicProfile =
    options.heuristicProfile ?? DEFAULT_HEURISTIC_PROFILE;
  const maxMoves = positiveInteger(options.maxMoves ?? DEFAULT_MAX_MOVES);
  const trace: HeadlessMoveTrace[] | undefined = options.trace ? [] : undefined;
  const completedDepths: Record<number, number> = {};
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
  let chains = 0;
  let maxChain = 0;
  let discsCleared = 0;
  let coveredRevealed = 0;
  let searchNodes = 0;
  let searchWork = 0;
  let cacheHits = 0;
  let incompleteSearches = 0;
  let depthZeroSearches = 0;

  while (!state.gameOver && state.movesPlayed < maxMoves) {
    const evaluation = evaluateMoves(state, {
      maxDepth: options.search.maxDepth,
      timeLimitMs:
        options.search.timeLimitMs ??
        (options.search.maxWork === undefined
          ? 1_000
          : Number.POSITIVE_INFINITY),
      maxWork: options.search.maxWork,
      heuristicProfile,
    });
    const column = evaluation.bestColumn;
    if (column === null) {
      throw new Error("The solver returned no move for a live Drop7 game");
    }

    searchNodes += evaluation.nodes;
    searchWork += evaluation.work;
    cacheHits += evaluation.cacheHits;
    completedDepths[evaluation.depth] =
      (completedDepths[evaluation.depth] ?? 0) + 1;
    if (!evaluation.complete) incompleteSearches += 1;
    if (evaluation.depth === 0) depthZeroSearches += 1;

    const disc = state.nextDisc;
    const revealSeed = mix32(
      seed ^
        Math.imul((state.movesPlayed + 1) >>> 0, 0x85ebca6b) ^
        REVEAL_DOMAIN,
    );
    const move = playMove(state, column, seededRandom(revealSeed), {
      captureAnimation: false,
    });
    if (!move) throw new Error(`Solver chose illegal column ${column}`);

    if (move.clearedBoard) clears += 1;
    if (move.waves.length >= 2) chains += 1;
    maxChain = Math.max(maxChain, move.waves.length);
    for (const wave of move.waves) {
      discsCleared += wave.cleared;
      coveredRevealed += wave.revealed;
    }

    state = move.state.gameOver
      ? move.state
      : {
          ...move.state,
          nextDisc: headlessDisc(seed, move.state.movesPlayed),
        };
    trace?.push({
      move: state.movesPlayed,
      disc,
      column,
      scoreDelta: move.scoreDelta,
      score: state.score,
      chainDepth: move.waves.length,
      completedDepth: evaluation.depth,
      searchWork: evaluation.work,
    });
  }

  return {
    seed,
    heuristicProfile,
    score: state.score,
    moves: state.movesPlayed,
    finalLevel: state.level,
    gameOver: state.gameOver,
    censored: !state.gameOver,
    clears,
    chains,
    maxChain,
    discsCleared,
    coveredRevealed,
    searchNodes,
    searchWork,
    cacheHits,
    incompleteSearches,
    depthZeroSearches,
    completedDepths,
    elapsedMs: Math.max(0, performance.now() - startedAt),
    ...(trace ? { trace } : {}),
  };
}

export function runHeadlessTournament(
  options: HeadlessTournamentOptions,
): HeadlessTournamentResult {
  if (options.profiles.length === 0) {
    throw new Error("At least one heuristic profile is required");
  }
  if (options.seeds.length === 0) {
    throw new Error("At least one game seed is required");
  }
  const uniqueProfiles = [...new Set(options.profiles)];
  const uniqueSeeds = [
    ...new Set(options.seeds.map((seed) => normalizeGameSeed(seed))),
  ];
  const games: HeadlessGameResult[] = [];

  // Rotate execution order across seeds to reduce warm-up/order bias in the
  // optional wall-clock mode. Fixed-work runs remain exactly reproducible.
  for (let seedIndex = 0; seedIndex < uniqueSeeds.length; seedIndex += 1) {
    for (let offset = 0; offset < uniqueProfiles.length; offset += 1) {
      const profile =
        uniqueProfiles[(seedIndex + offset) % uniqueProfiles.length];
      games.push(
        runHeadlessGame({
          seed: uniqueSeeds[seedIndex],
          heuristicProfile: profile,
          search: options.search,
          maxMoves: options.maxMoves,
        }),
      );
      options.onGameComplete?.(
        games[games.length - 1],
        games.length,
        uniqueSeeds.length * uniqueProfiles.length,
      );
    }
  }

  games.sort(
    (first, second) =>
      uniqueProfiles.indexOf(first.heuristicProfile) -
        uniqueProfiles.indexOf(second.heuristicProfile) ||
      first.seed - second.seed,
  );
  return {
    referenceProfile: uniqueProfiles[0],
    games,
    summaries: summarizeProfiles(games, uniqueProfiles),
  };
}

function summarizeProfiles(
  games: readonly HeadlessGameResult[],
  profiles: readonly HeuristicProfileName[],
) {
  const referenceScores = new Map(
    games
      .filter(
        (game) =>
          game.heuristicProfile === profiles[0] && !game.censored,
      )
      .map((game) => [game.seed, game.score]),
  );

  return profiles.map((profile): HeadlessProfileSummary => {
    const profileGames = games.filter(
      (game) => game.heuristicProfile === profile,
    );
    const completedGames = profileGames.filter((game) => !game.censored);
    const scores = completedGames.map((game) => game.score).sort(numberOrder);
    const deltas = profileGames
      .filter(
        (game) => !game.censored && referenceScores.has(game.seed),
      )
      .map((game) => game.score - referenceScores.get(game.seed)!)
      .sort(numberOrder);
    let wins = 0;
    let ties = 0;
    let losses = 0;
    for (const delta of deltas) {
      if (delta > 0) wins += 1;
      else if (delta < 0) losses += 1;
      else ties += 1;
    }
    const totalMoves = profileGames.reduce((sum, game) => sum + game.moves, 0);

    return {
      heuristicProfile: profile,
      games: profileGames.length,
      completedGames: completedGames.length,
      meanScore: scores.length === 0 ? null : mean(scores),
      medianScore: scores.length === 0 ? null : percentile(scores, 0.5),
      p10Score: scores.length === 0 ? null : percentile(scores, 0.1),
      p90Score: scores.length === 0 ? null : percentile(scores, 0.9),
      minimumScore: scores[0] ?? null,
      maximumScore: scores.at(-1) ?? null,
      meanMoves: mean(profileGames.map((game) => game.moves)),
      meanFinalLevel: mean(profileGames.map((game) => game.finalLevel)),
      meanCompletedDepth:
        totalMoves === 0
          ? 0
          : profileGames.reduce(
              (sum, game) =>
                sum +
                Object.entries(game.completedDepths).reduce(
                  (depthSum, [depth, count]) =>
                    depthSum + Number(depth) * count,
                  0,
                ),
              0,
            ) / totalMoves,
      censoredGames: profileGames.filter((game) => game.censored).length,
      incompleteSearches: profileGames.reduce(
        (sum, game) => sum + game.incompleteSearches,
        0,
      ),
      depthZeroSearches: profileGames.reduce(
        (sum, game) => sum + game.depthZeroSearches,
        0,
      ),
      meanSearchWorkPerMove:
        totalMoves === 0
          ? 0
          : profileGames.reduce((sum, game) => sum + game.searchWork, 0) /
            totalMoves,
      meanCacheHitsPerMove:
        totalMoves === 0
          ? 0
          : profileGames.reduce((sum, game) => sum + game.cacheHits, 0) /
            totalMoves,
      pairedGames: deltas.length,
      pairedMeanDelta: deltas.length === 0 ? null : mean(deltas),
      pairedMedianDelta:
        deltas.length === 0 ? null : percentile(deltas, 0.5),
      pairedDelta95: pairedBootstrapInterval(deltas),
      wins,
      ties,
      losses,
    };
  });
}

function pairedBootstrapInterval(
  sortedDeltas: readonly number[],
): readonly [number, number] | null {
  if (sortedDeltas.length < 5) return null;
  if (sortedDeltas.every((delta) => delta === sortedDeltas[0])) {
    return [sortedDeltas[0], sortedDeltas[0]];
  }
  const random = seededRandom(0xd707b007 ^ sortedDeltas.length);
  const samples: number[] = [];
  for (let sample = 0; sample < 2_000; sample += 1) {
    let total = 0;
    for (let index = 0; index < sortedDeltas.length; index += 1) {
      total += sortedDeltas[Math.floor(random() * sortedDeltas.length)];
    }
    samples.push(total / sortedDeltas.length);
  }
  samples.sort(numberOrder);
  return [percentile(samples, 0.025), percentile(samples, 0.975)];
}

function percentile(sorted: readonly number[], fraction: number) {
  if (sorted.length === 0) return 0;
  const position = Math.max(
    0,
    Math.min(sorted.length - 1, (sorted.length - 1) * fraction),
  );
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  const mix = position - lower;
  return sorted[lower] * (1 - mix) + sorted[upper] * mix;
}

function mean(values: readonly number[]) {
  if (values.length === 0) return 0;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function positiveInteger(value: number) {
  if (!Number.isFinite(value) || value < 1) {
    throw new Error("Expected a positive integer");
  }
  return Math.trunc(value);
}

function normalizeGameSeed(value: number) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new Error("Game seeds must be uint32 integers");
  }
  return value >>> 0;
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
