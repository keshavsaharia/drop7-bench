/**
 * Technique card art registry.
 *
 * Every entry's `Art` is a server component returning one inline SVG:
 * `<svg class="tart tart--<name>" viewBox="0 0 320 180">` with named groups,
 * `data-anim` on every animated element, keyframes prefixed `tart-<name>-*`
 * (in the art's own `<name>.css`), and a `.tart-final` group holding the
 * resting frame. The shared play/pause contract lives in `art.css`, imported
 * by `TechniqueArt.tsx`.
 *
 * `TechniqueArt` looks a name up here and falls back to `FallbackArt` for
 * anything it does not know, so a page never throws over a missing art.
 * Technique names match `TECHNIQUE_ORDER` in web/lib/techniques.ts; the
 * three engine names are used by the Engines section.
 */
import type { ReactElement } from "react";
import { AfterstateArt } from "./AfterstateArt";
import { ConstructivePlanningArt } from "./ConstructivePlanningArt";
import { DeterminizationArt } from "./DeterminizationArt";
import { EngineNativeArt } from "./EngineNativeArt";
import { EngineRustArt } from "./EngineRustArt";
import { EngineTypescriptArt } from "./EngineTypescriptArt";
import { EvolutionArt } from "./EvolutionArt";
import { ExpectimaxArt } from "./ExpectimaxArt";
import { FallbackArt } from "./FallbackArt";
import { HeuristicEvaluationArt } from "./HeuristicEvaluationArt";
import { MctsArt } from "./MctsArt";
import { NnueArt } from "./NnueArt";
import { NTupleArt } from "./NTupleArt";
import { OracleDistillationArt } from "./OracleDistillationArt";
import { PolicyGradientArt } from "./PolicyGradientArt";
import { QLearningArt } from "./QLearningArt";
import { RiskSurvivalArt } from "./RiskSurvivalArt";
import { RolloutPolicyIterationArt } from "./RolloutPolicyIterationArt";

export type ArtMode = "hover" | "loop" | "once" | "static";

export type TechniqueName =
  | "expectimax"
  | "heuristic-evaluation"
  | "q-learning"
  | "n-tuple"
  | "nnue"
  | "policy-gradient"
  | "evolution"
  | "mcts"
  | "rollout-policy-iteration"
  | "oracle-distillation"
  | "risk-survival"
  | "afterstate"
  | "constructive-planning"
  | "determinization"
  | "engine-native"
  | "engine-typescript"
  | "engine-rust"
  | "fallback";

export interface ArtProps {
  /**
   * `hover` (default) pauses on the first frame until the enclosing `.card`
   * is hovered or focused; `loop` plays continuously; `once` plays one cycle
   * and rests on the final frame; `static` shows the final frame only.
   */
  mode?: ArtMode;
  /** Accessible name; the art's own description is used when absent. */
  title?: string;
  className?: string;
}

export interface TechniqueEntry {
  /** Server component rendering the inline SVG. */
  Art: (props: ArtProps) => ReactElement;
  /** Display name for the card and the home grid. */
  label: string;
  /** One plain sentence for the card and the home grid. */
  oneLine: string;
}

export const TECHNIQUES: Record<TechniqueName, TechniqueEntry> = {
  expectimax: {
    Art: ExpectimaxArt,
    label: "Expectimax search",
    oneLine:
      "Look a few moves ahead, average over the disc the game might deal next, and play the column with the best average.",
  },
  "heuristic-evaluation": {
    Art: HeuristicEvaluationArt,
    label: "Heuristic evaluation",
    oneLine:
      "Score a board with a short list of hand-picked features, such as height and gray discs, and drop where the score is best.",
  },
  "q-learning": {
    Art: QLearningArt,
    label: "Q-learning and value learning",
    oneLine: "Learn a number for every column from what happened after playing it.",
  },
  "n-tuple": {
    Art: NTupleArt,
    label: "N-tuple networks",
    oneLine:
      "Read the board through small fixed windows and add up one learned number per window pattern.",
  },
  nnue: {
    Art: NnueArt,
    label: "NNUE evaluators",
    oneLine:
      "A small network whose first layer updates one changed cell at a time, so evaluating the next board costs almost nothing.",
  },
  "policy-gradient": {
    Art: PolicyGradientArt,
    label: "Policy gradients",
    oneLine:
      "Play a game, then nudge the odds of every move up or down by how the game turned out.",
  },
  evolution: {
    Art: EvolutionArt,
    label: "Evolutionary optimisation",
    oneLine: "Keep a population of evaluators, score them on games, and breed the best ones.",
  },
  mcts: {
    Art: MctsArt,
    label: "Monte Carlo tree search",
    oneLine:
      "Grow a lopsided tree by playing random games out and spending more effort where the results look good.",
  },
  "rollout-policy-iteration": {
    Art: RolloutPolicyIterationArt,
    label: "Rollouts and policy iteration",
    oneLine:
      "Try each column, play the rest of the game out many times, and keep the column whose average holds up.",
  },
  "oracle-distillation": {
    Art: OracleDistillationArt,
    label: "Oracles, teachers and distillation",
    oneLine:
      "Let a teacher that sees hidden values pick moves, then train a student that sees only the public board to copy it.",
  },
  "risk-survival": {
    Art: RiskSurvivalArt,
    label: "Risk and survival objectives",
    oneLine:
      "Value staying alive as well as scoring, since a game that ends early forfeits every later clear.",
  },
  afterstate: {
    Art: AfterstateArt,
    label: "Afterstates",
    oneLine: "Judge the board after the disc lands and before the next disc is dealt.",
  },
  "constructive-planning": {
    Art: ConstructivePlanningArt,
    label: "Constructive planning",
    oneLine:
      "Build a structure that pays nothing now and a lot a few moves later, and let the rise set it off.",
  },
  determinization: {
    Art: DeterminizationArt,
    label: "Determinized planning",
    oneLine:
      "Sample a few concrete futures, plan against each as if it were certain, and combine the plans.",
  },
  "engine-native": {
    Art: EngineNativeArt,
    label: "C++ engine",
    oneLine: "The C++ engine that plays the reference games and every large research run.",
  },
  "engine-typescript": {
    Art: EngineTypescriptArt,
    label: "TypeScript engine",
    oneLine: "The readable engine behind the browser game and every board drawn on this site.",
  },
  "engine-rust": {
    Art: EngineRustArt,
    label: "Rust engine",
    oneLine: "A packed bitboard port, replayed move for move against the C++ engine.",
  },
  fallback: {
    Art: FallbackArt,
    label: "Drop7",
    oneLine: "A Drop7 board.",
  },
};

/** The art used for any name the registry does not know. */
export const FALLBACK: TechniqueEntry = TECHNIQUES.fallback;

export function isTechniqueName(name: string): name is TechniqueName {
  return Object.hasOwn(TECHNIQUES, name);
}
