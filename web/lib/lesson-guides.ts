/** Short, qualitative reading aids. These diagrams contain no measured results. */
export const LESSON_GUIDES: Record<string, { idea: string; watch: string; vocabulary: string }> = {
  "chance-vs-choice": {
    idea: "Choose a column using the disc you can see. Then consider the different discs the game could deal next.",
    watch: "The highlighted branch is a player’s choice. The branches that fan out afterward are possibilities to average over.",
    vocabulary: "search",
  },
  "evaluating-a-board": {
    idea: "A search eventually stops looking ahead. A board evaluator estimates how useful the position at that stopping point will be.",
    watch: "Several features feed one estimate: room above the stacks, covered discs, and opportunities to clear.",
    vocabulary: "search",
  },
  "survival-vs-score": {
    idea: "Every rise earns a bonus and pushes the board closer to the top. Clearing discs makes room to keep playing.",
    watch: "The rising steps represent successive row rises. More points arrive together with more pressure on the board.",
    vocabulary: "game",
  },
  "heavy-tails": {
    idea: "A few unusually long games can pull the average upward. To understand a strategy, look at the full set of games.",
    watch: "The dots illustrate a cluster of ordinary games and a few much longer ones. This is a schematic, with no measured scores.",
    vocabulary: "evidence",
  },
  "ranking-siblings": {
    idea: "The useful question is which available column is best. Predicting one board’s future does not guarantee that the alternatives are ordered correctly.",
    watch: "All seven alternatives lead back to the same decision. Comparing them brings one choice into focus.",
    vocabulary: "search",
  },
  "oracles-and-teachers": {
    idea: "A teacher can use hidden information to prepare examples. The student must learn to choose using only what a player can see.",
    watch: "Training examples pass from teacher to student. The hidden information stays on the teacher’s side of the boundary.",
    vocabulary: "learning",
  },
  "does-more-compute-help": {
    idea: "More time can buy deeper search, more possible futures, or more training. Each spends that time on a different source of uncertainty.",
    watch: "The tree expands down for more moves and outward for more possible outcomes. More branches mean more work to evaluate.",
    vocabulary: "search",
  },
  "learning-from-play": {
    idea: "A player generates examples by playing. A model learns from those examples, then helps choose moves in the next games.",
    watch: "Play, record, and learn form a loop. A changed player still needs to be checked on separate games.",
    vocabulary: "learning",
  },
};

export const LEARN_DESCRIPTION = "Learn the rules, explore the ideas behind a good move, and find clear definitions of the game’s vocabulary.";
export const CONCEPTS_DESCRIPTION = "Visual lessons on choosing moves, evaluating boards, learning from play, and understanding results.";
