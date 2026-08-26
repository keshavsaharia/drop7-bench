import type {
  Board,
  DiscValue,
  GameState,
} from "../core/typescript/engine.ts";
import { evaluateMoves } from "../core/typescript/solver.ts";
import { evaluateGrayThroughputMoves } from "../core/typescript/gray-throughput-policy.ts";
import { evaluateRolloutMoves } from "../core/typescript/rollout-solver.ts";
import { evaluateMctsMoves } from "../core/typescript/mcts-solver.ts";
import { evaluateSparseExpectimaxMoves } from "../core/typescript/sparse-expectimax.ts";
import { evaluateRiskSensitiveMoves } from "../core/typescript/risk-sensitive-planner.ts";
import { evaluateRobustOpenLoopBeam } from "../core/typescript/robust-open-loop-beam.ts";
import {
  nativeDecide,
  RUST_BUILD_HINT,
  RUST_DECIDE_BINARY,
} from "./native-policy.ts";

/**
 * The Drop7 Policy Protocol (D7P) TypeScript interface, mirroring the text
 * protocol in docs/d7p-protocol.md. A policy is a deterministic function of
 * the public state: the visible board, the visible next disc, the moves until
 * the next rise, and the terminal flag. It never receives the round id, the
 * generator seed, future discs, latent values, or the score.
 */
export interface PublicState {
  board: Board;
  nextDisc: DiscValue;
  movesRemaining: number;
  gameOver: boolean;
}

export interface BenchPolicy {
  /** Stable kebab-case identifier used in leaderboard data and the D7P wire protocol. */
  id: string;
  name: string;
  family: string;
  description: string;
  /** Repository-backed page explaining the exact strategy family/configuration. */
  researchPath: `/approaches/${string}`;
  /**
   * True when the decision depends only on the legal public state. Policies
   * that additionally read level or move number (which the research contract
   * excludes from the deployable interface) are flagged and shown separately.
   */
  publicInformation: boolean;
  /** Excluded from the default suite when true (runtime), opt in explicitly. */
  slow?: boolean;
  chooseColumn(state: GameState): number | null;
}

/**
 * Strict policies receive a sanitized state whose score, level, and move
 * counter are fixed constants, so they cannot read non-public fields even
 * accidentally. The board, next disc, rise clock, and terminal flag are the
 * only inputs that vary.
 */
function publicOnly(state: GameState): GameState {
  return {
    board: state.board,
    nextDisc: state.nextDisc,
    score: 0,
    level: 1,
    movesRemaining: state.movesRemaining,
    movesPlayed: 0,
    gameOver: state.gameOver,
  };
}

/** Solver-local seed derived from the policy id; never a game/round seed. */
function policySeed(id: string): number {
  let hash = 0x811c9dc5;
  for (let index = 0; index < id.length; index += 1) {
    hash = Math.imul(hash ^ id.charCodeAt(index), 0x01000193);
  }
  return hash >>> 0;
}

function define(policy: BenchPolicy): BenchPolicy {
  return policy;
}

export const BENCH_POLICIES: readonly BenchPolicy[] = [
  define({
    id: "greedy",
    name: "Greedy 1-ply",
    family: "heuristic-search",
    description:
      "Exact chance average over the immediate move, scored by the combined hand evaluator. The fast baseline.",
    researchPath: "/approaches/fair-expectimax/fair-policy",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateMoves(publicOnly(state), { maxDepth: 1, maxWork: 100_000 })
        .bestColumn,
  }),
  define({
    id: "expectimax-d2",
    name: "Expectimax D2",
    family: "fair-expectimax",
    description:
      "Full-width expectimax, two completed plies, combined leaf, fixed work bound.",
    researchPath: "/approaches/fair-expectimax/reference",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateMoves(publicOnly(state), { maxDepth: 2, maxWork: 100_000 })
        .bestColumn,
  }),
  define({
    id: "expectimax-d3",
    name: "TypeScript Expectimax D3",
    family: "fair-expectimax",
    description:
      "Full-width expectimax, three completed plies. The affordable end of the reference line.",
    researchPath: "/approaches/fair-expectimax/reference",
    publicInformation: true,
    slow: true,
    chooseColumn: (state) =>
      evaluateMoves(publicOnly(state), { maxDepth: 3, maxWork: 400_000 })
        .bestColumn,
  }),
  define({
    id: "expectimax-d4",
    name: "TypeScript Expectimax D4",
    family: "fair-expectimax",
    description:
      "Completed full-width TypeScript depth 4 with exact engine outcomes and the combined leaf. A playground analogue of the native research reference, not a source-identical port.",
    researchPath: "/approaches/fair-expectimax/reference",
    publicInformation: true,
    slow: true,
    chooseColumn: (state) =>
      evaluateMoves(publicOnly(state), { maxDepth: 4, maxWork: 3_200_000 })
        .bestColumn,
  }),
  define({
    id: "gray-throughput",
    name: "Gray throughput",
    family: "heuristic-search",
    description:
      "Rule policy prioritizing cracks, reveals, cover altitude, and occupancy flow.",
    researchPath: "/approaches/heuristic-search/gray-throughput",
    publicInformation: false,
    chooseColumn: (state) => evaluateGrayThroughputMoves(state).bestColumn,
  }),
  define({
    id: "rollout-h8",
    name: "Rollout H8",
    family: "heuristic-search",
    description:
      "Eight paired rollouts per root column over an eight-move horizon with a greedy continuation.",
    researchPath: "/approaches/heuristic-search/rollout",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateRolloutMoves(publicOnly(state), {
        rollouts: 8,
        horizon: 8,
        seed: policySeed("rollout-h8"),
      }).bestColumn,
  }),
  define({
    id: "mcts",
    name: "MCTS",
    family: "tree-search",
    description:
      "Chance-sampled Monte Carlo tree search, 400 simulations, 16-move horizon, heuristic leaf.",
    researchPath: "/approaches/tree-search/mcts",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateMctsMoves(publicOnly(state), {
        simulations: 400,
        horizon: 16,
        seed: policySeed("mcts"),
      }).bestColumn,
  }),
  define({
    id: "sparse-d2",
    name: "Sparse expectimax D2",
    family: "heuristic-search",
    description:
      "Iterative-deepening expectimax with five stratified chance samples per branch, two plies.",
    researchPath: "/approaches/heuristic-search/sparse-expectimax",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateSparseExpectimaxMoves(publicOnly(state), {
        maxDepth: 2,
        chanceSamples: 5,
        seed: policySeed("sparse-d2"),
      }).bestColumn,
  }),
  define({
    id: "risk-d2",
    name: "Risk-sensitive D2",
    family: "heuristic-search",
    description:
      "CVaR-weighted root over a two-ply expectimax continuation; trades mean for a safer lower tail.",
    researchPath: "/approaches/heuristic-search/risk-sensitive",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateRiskSensitiveMoves(publicOnly(state), {
        scenarios: 8,
        continuationDepth: 2,
        chanceSamples: 3,
        riskWeight: 0.5,
        maxWork: 150_000,
        seed: policySeed("risk-d2"),
      }).bestColumn,
  }),
  define({
    id: "open-loop-beam",
    name: "Open-loop beam",
    family: "heuristic-search",
    description:
      "Replanning open-loop prefix beam over twelve shared scenarios, bounded by logical work (the wall-clock cap is a loose safety net).",
    researchPath: "/approaches/heuristic-search/open-loop",
    publicInformation: true,
    chooseColumn: (state) =>
      evaluateRobustOpenLoopBeam(publicOnly(state), {
        scenarios: 12,
        depth: 3,
        beamWidth: 8,
        maxWork: 150_000,
        timeLimitMs: 60_000,
        seed: policySeed("open-loop-beam"),
      }).bestColumn,
  }),
  define({
    id: "native-fair-d4-s7",
    name: "Native fair D4, 7 strata",
    family: "lifetime-objective",
    description:
      "The research search itself: the bit-exact fast engine's completed full-width depth 4 with seven chance strata and the frozen fair leaf, through a one-shot native binary (build it with approaches/lifetime-objective/leaf-evolution/build.sh decide).",
    researchPath: "/approaches/lifetime-objective/leaf-evolution",
    publicInformation: true,
    slow: true,
    chooseColumn: (state) => nativeDecide(publicOnly(state), { chanceSamples: 7 }),
  }),
  define({
    id: "rust-fair-d7-s7",
    name: "Rust fair D7, 7 strata",
    family: "fair-expectimax",
    description:
      "The Rust bitboard engine's completed full-width depth 7 with seven chance strata and the frozen fair leaf, evaluated by the value-identical central-frontier scheduler under one shared cache budget, through the one-shot decide binary (cargo build --release --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml).",
    researchPath: "/approaches/fair-expectimax/rust-engine",
    publicInformation: true,
    slow: true,
    chooseColumn: (state) =>
      nativeDecide(publicOnly(state), {
        binary: RUST_DECIDE_BINARY,
        buildHint: RUST_BUILD_HINT,
        depth: 7,
        chanceSamples: 7,
        // The decide CLI partitions this aggregate budget across workers.
        cache: 16_777_216,
      }),
  }),
];

export const DEFAULT_POLICY_IDS = BENCH_POLICIES.filter(
  (policy) => !policy.slow,
).map((policy) => policy.id);

/** Public-information policies seeded into each human competition by default. */
export const COMPETITION_POLICY_IDS = [
  "expectimax-d4",
  "expectimax-d3",
  "expectimax-d2",
  "greedy",
  "open-loop-beam",
  "risk-d2",
  "sparse-d2",
  "rollout-h8",
  "mcts",
] as const;

export function getPolicy(id: string): BenchPolicy {
  const policy = BENCH_POLICIES.find((candidate) => candidate.id === id);
  if (!policy) {
    throw new Error(
      `Unknown policy "${id}". Available: ${BENCH_POLICIES.map((p) => p.id).join(", ")}`,
    );
  }
  return policy;
}
