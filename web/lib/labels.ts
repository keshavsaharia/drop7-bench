/**
 * The one map from the repository's record vocabulary to the words a reader
 * sees. Badge text, the reads sentence and the tier sentence live here so
 * that text and colour (globals.css `.badge[data-kind][data-value]`) never
 * disagree by route again. Nothing here relabels a value: a badge shows the
 * recorded token, optionally with a kind prefix that says what it is.
 */

export type BadgeKind =
  | "status"
  | "evidence"
  | "reads"
  | "outcome"
  | "validity"
  | "tier"
  | "plain";

/** What an approach's `reads:` frontmatter means, in one clause. */
export const READS_TEXT: Record<string, string> = {
  public: "reads only what a player can see",
  oracle: "reads hidden values or the future: a teacher, never a policy",
  teacher: "reads hidden values or the future: a teacher, never a policy",
  diagnostic: "a measurement tool, not a policy",
};

export function readsSentence(reads: string): string {
  return READS_TEXT[reads] ?? "";
}

/**
 * Evidence and benchmark tiers as noun phrases that fit the sentence
 * "at the … level" (Research.tsx). The parenthesis says what the tier can and
 * cannot claim; the tier word itself is the recorded token.
 */
export const TIER_TEXT: Record<string, string> = {
  proposal: "proposal (no games played)",
  mechanics: "mechanics (checks only, no games played)",
  "mechanics-only": "mechanics-only (checks only, no games played)",
  pilot: "pilot (a small run to find bugs and project cost, not a strength claim)",
  development: "development (a cohort for deciding what to try next, not confirmation)",
  "public-development": "public-development (a cohort for deciding what to try next, not confirmation)",
  "independently-replicated-development": "independently replicated development",
  "protected-validation": "protected validation",
  "final-confirmation": "final confirmation (the one-shot cohort)",
  CHECK: "CHECK (mechanics checks only, no games played)",
  PILOT: "PILOT (a small run to find bugs and project cost, not a strength claim)",
  SCREEN: "SCREEN (a 32-game paired screen)",
  STANDARD: "STANDARD (a 64-game paired development cohort)",
  QUALIFY: "QUALIFY (a 256-game qualification cohort)",
  PROTECTED: "PROTECTED (the protected validation cohort)",
  FINAL: "FINAL (the one-shot final cohort)",
};

export function tierSentence(tier: string): string {
  return TIER_TEXT[tier] ?? tier;
}

/** Result outcomes that take the "outcome:" prefix; other values shown under the
 * outcome kind (theory assessments, log finding kinds) are shown bare. */
const OUTCOME_VALUES = new Set(["pass", "fail", "inconclusive", "not-applicable"]);

/** The text a badge shows for a kind/value pair. */
export function badgeText(kind: BadgeKind, value: string): string {
  switch (kind) {
    case "reads":
      return `reads: ${value}`;
    case "tier":
      return `tier: ${value}`;
    case "validity":
      return `${value} run`;
    case "outcome":
      return OUTCOME_VALUES.has(value) ? `outcome: ${value}` : value;
    default:
      return value;
  }
}
