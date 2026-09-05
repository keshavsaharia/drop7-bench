/**
 * The technique catalogue: the fourteen ways a Drop7 player can be built that
 * this program has tried. Each strategy approach's README carries one of these
 * slugs in its `technique` frontmatter key; engines and diagnostics carry none.
 *
 * The catalogue is data, not derived from the checkout, so a page can list
 * every technique even when a checkout has no approaches directory. The
 * canonical list is also the closed vocabulary that
 * `scripts/check-approach-frontmatter.mjs` enforces.
 */

export const TECHNIQUE_ORDER = [
  "expectimax",
  "heuristic-evaluation",
  "q-learning",
  "n-tuple",
  "nnue",
  "policy-gradient",
  "evolution",
  "mcts",
  "rollout-policy-iteration",
  "oracle-distillation",
  "risk-survival",
  "afterstate",
  "constructive-planning",
  "determinization",
] as const;

export type TechniqueSlug = (typeof TECHNIQUE_ORDER)[number];

export interface Technique {
  slug: TechniqueSlug;
  /** Plain noun phrase, used as the group heading. */
  title: string;
  /** One sentence for a newcomer, in the site voice. */
  oneLine: string;
  /** Slug of the primer page under web/content/learn/techniques/. */
  primerSlug: string;
}

const CATALOGUE: Record<TechniqueSlug, Omit<Technique, "slug">> = {
  expectimax: {
    title: "Expectimax search",
    oneLine:
      "Look a few moves ahead, take the best column on your own turns, and average over the discs the game might deal.",
    primerSlug: "expectimax",
  },
  "heuristic-evaluation": {
    title: "Heuristic evaluation",
    oneLine:
      "Score a board with a hand-written sum of visible traits, then play the column whose board scores highest.",
    primerSlug: "heuristic-evaluation",
  },
  "q-learning": {
    title: "Q-learning and value learning",
    oneLine:
      "Learn from past games how much each column is worth, so the player can rank moves without searching ahead.",
    primerSlug: "q-learning",
  },
  "n-tuple": {
    title: "N-tuple networks",
    oneLine:
      "Learn the value of a board from small cell patterns, each with its own lookup table, trained over millions of self-play moves.",
    primerSlug: "n-tuple",
  },
  nnue: {
    title: "NNUE evaluators",
    oneLine:
      "A small neural network, cheap enough to run at every leaf of a search, that judges a board from its cells.",
    primerSlug: "nnue",
  },
  "policy-gradient": {
    title: "Policy gradients",
    oneLine:
      "Train a network that picks columns directly, nudging it toward the choices that led to longer games.",
    primerSlug: "policy-gradient",
  },
  evolution: {
    title: "Evolutionary optimisation",
    oneLine:
      "Tune a player's weights by playing complete games, keeping the settings that scored best, and repeating.",
    primerSlug: "evolution",
  },
  mcts: {
    title: "Monte Carlo tree search",
    oneLine:
      "Grow the look-ahead only where it seems promising, guided by quick simulated playouts.",
    primerSlug: "mcts",
  },
  "rollout-policy-iteration": {
    title: "Rollouts and policy iteration",
    oneLine:
      "Judge a column by playing many moves forward with a fast policy, and let that overrule the search when the evidence is strong.",
    primerSlug: "rollout-policy-iteration",
  },
  "oracle-distillation": {
    title: "Oracles, teachers and distillation",
    oneLine:
      "Let a planner that can see the future label positions, then train a player that cannot see the future to imitate it.",
    primerSlug: "oracle-distillation",
  },
  "risk-survival": {
    title: "Risk and survival objectives",
    oneLine:
      "Judge a move by its worst outcomes, or by how long the game keeps going, and see whether caution extends life.",
    primerSlug: "risk-survival",
  },
  afterstate: {
    title: "Afterstates",
    oneLine:
      "Score the board a move leaves behind, and build training data in which every column the player could have chosen was measured.",
    primerSlug: "afterstate",
  },
  "constructive-planning": {
    title: "Constructive planning",
    oneLine:
      "Plan a whole rise cycle toward a target board shape, so that later rises set off stacks prepared in advance.",
    primerSlug: "constructive-planning",
  },
  determinization: {
    title: "Determinized planning",
    oneLine:
      "Imagine several complete futures, plan each one as if it were certain, and average the plans.",
    primerSlug: "determinization",
  },
};

export function isTechniqueSlug(slug: string): slug is TechniqueSlug {
  return (TECHNIQUE_ORDER as readonly string[]).includes(slug);
}

/** The catalogue entry for a slug, or null for anything outside the fourteen. */
export function getTechnique(slug: string): Technique | null {
  if (!isTechniqueSlug(slug)) return null;
  return { slug, ...CATALOGUE[slug] };
}

/** Every technique, in catalogue order. */
export function listTechniques(): Technique[] {
  return TECHNIQUE_ORDER.map((slug) => ({ slug, ...CATALOGUE[slug] }));
}
