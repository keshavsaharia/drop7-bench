/**
 * The repository's evidence vocabulary and its rendering rules, shared by the
 * EvidenceStrip, stat tiles and record summaries. Each chip carries the
 * record's own word, a tone from the fixed status scale, an icon name (a chip
 * is never colour alone), and the sentence the hover shows.
 *
 * Tone rules (docs: the chart audit, section 3.2): run validity `valid` is
 * neutral, `partial` a warning, `invalid` serious; scientific outcome `pass`
 * is good and `fail` is NEUTRAL (a completed contribution, never styled as an
 * error), `inconclusive` a warning; `reads: oracle|teacher` is serious with an
 * eye-slash icon. An unknown word renders as a neutral chip with the raw word;
 * nothing here invents a tier or promotes a claim.
 */

export type EvidenceField = "status" | "evidence" | "reads" | "tier" | "validity" | "outcome" | "cohort";

export type ChipTone = "neutral" | "good" | "warning" | "serious" | "critical" | "running";

export type ChipIcon = "check" | "cross" | "clock" | "flag" | "eye" | "eye-slash" | "tool" | "layers" | "ledger" | "note" | "dash" | "users" | "tag";

export interface EvidenceInput {
  status?: string;
  evidence?: string;
  reads?: string;
  tier?: string;
  validity?: string;
  outcome?: string;
  cohort?: string;
}

export interface EvidenceChip {
  field: EvidenceField;
  /** The word exactly as the record wrote it. */
  word: string;
  tone: ChipTone;
  icon: ChipIcon;
  /** The sentence shown on hover / as the accessible description. */
  sentence: string;
  /** False when the word is outside the closed vocabulary (rendered neutral, never relabelled). */
  known: boolean;
}

export const STATUSES = ["completed", "rejected", "runtime-paused", "preregistered", "support-only", "proposal", "unknown"] as const;
export const EVIDENCE = ["ledger-recorded", "task-record only", "repository-verified", "reproduced", "none"] as const;
export const READS = ["public", "oracle", "teacher", "diagnostic"] as const;
export const VALIDITY = ["valid", "partial", "invalid"] as const;
export const OUTCOMES = ["pass", "fail", "inconclusive", "not-applicable"] as const;
/** Benchmark tiers plus the finding tiers exactly as the findings state them. */
export const TIERS = [
  "CHECK",
  "PILOT",
  "SCREEN",
  "STANDARD",
  "QUALIFY",
  "PROTECTED",
  "FINAL",
  "proposal",
  "mechanics",
  "pilot",
  "development",
  "public-development",
  "independently-replicated-development",
  "protected-validation",
  "final-confirmation",
] as const;

export const VOCABULARY: Record<Exclude<EvidenceField, "cohort">, readonly string[]> = {
  status: STATUSES,
  evidence: EVIDENCE,
  reads: READS,
  tier: TIERS,
  validity: VALIDITY,
  outcome: OUTCOMES,
};

interface Rule {
  tone: ChipTone;
  icon: ChipIcon;
  sentence: string;
}

const STATUS_RULES: Record<string, Rule> = {
  completed: { tone: "good", icon: "check", sentence: "the experiment ran to completion and its result is recorded" },
  rejected: { tone: "neutral", icon: "dash", sentence: "the tested configuration was rejected; a completed negative result" },
  "runtime-paused": { tone: "warning", icon: "clock", sentence: "paused by the runtime; not finished" },
  preregistered: { tone: "running", icon: "flag", sentence: "registered before any game was played; no result yet" },
  "support-only": { tone: "neutral", icon: "tool", sentence: "supports another line of work; makes no claim of its own" },
  proposal: { tone: "neutral", icon: "note", sentence: "a proposal; no games played" },
  unknown: { tone: "neutral", icon: "tag", sentence: "status not recorded" },
};

const EVIDENCE_RULES: Record<string, Rule> = {
  "ledger-recorded": { tone: "neutral", icon: "ledger", sentence: "recorded in the result ledger" },
  "task-record only": { tone: "neutral", icon: "note", sentence: "exists only in a task record, not in the ledger" },
  "repository-verified": { tone: "neutral", icon: "check", sentence: "verified against files in the record" },
  reproduced: { tone: "neutral", icon: "check", sentence: "reproduced independently" },
  none: { tone: "warning", icon: "dash", sentence: "no evidence recorded" },
};

const READS_RULES: Record<string, Rule> = {
  public: { tone: "neutral", icon: "eye", sentence: "reads only what a player can see" },
  oracle: { tone: "serious", icon: "eye-slash", sentence: "reads hidden values or the future; a teacher, never a policy" },
  teacher: { tone: "serious", icon: "eye-slash", sentence: "reads hidden values or the future; a teacher, never a policy" },
  diagnostic: { tone: "neutral", icon: "tool", sentence: "a measurement tool, not a policy" },
};

const VALIDITY_RULES: Record<string, Rule> = {
  valid: { tone: "neutral", icon: "check", sentence: "the run completed under its protocol" },
  partial: { tone: "warning", icon: "clock", sentence: "the run was interrupted or incomplete; read its numbers with that in mind" },
  invalid: { tone: "serious", icon: "cross", sentence: "the run broke its protocol; its numbers do not count" },
};

const OUTCOME_RULES: Record<string, Rule> = {
  pass: { tone: "good", icon: "check", sentence: "the preregistered gate was met" },
  fail: { tone: "neutral", icon: "dash", sentence: "the preregistered gate was not met; a completed negative result" },
  inconclusive: { tone: "warning", icon: "clock", sentence: "the run could not decide the gate" },
  "not-applicable": { tone: "neutral", icon: "dash", sentence: "no gate applies to this record" },
};

/** Sentence form of a tier (the wording Research.tsx has used since the first console). */
export const TIER_SENTENCES: Record<string, string> = {
  proposal: "a proposal with no games played",
  mechanics: "mechanics checks only, no games played",
  pilot: "a pilot: a small run to find bugs and project cost, not a strength claim",
  development: "a development cohort, useful for deciding what to try next, not confirmation",
  "public-development": "a public-information development cohort, useful for deciding what to try next, not confirmation",
  "independently-replicated-development": "a development cohort that was independently replicated",
  "protected-validation": "the protected validation cohort",
  "final-confirmation": "the one-shot final cohort",
  CHECK: "mechanics checks only, no games played",
  PILOT: "a pilot: a small run to find bugs and project cost, not a strength claim",
  SCREEN: "a 32-game paired screen",
  STANDARD: "a 64-game paired development cohort",
  QUALIFY: "a 256-game qualification cohort",
  PROTECTED: "the protected validation cohort",
  FINAL: "the one-shot final cohort",
};

export function tierSentence(tier: string): string {
  return TIER_SENTENCES[tier] ?? tier;
}

const RULES: Record<Exclude<EvidenceField, "cohort" | "tier">, Record<string, Rule>> = {
  status: STATUS_RULES,
  evidence: EVIDENCE_RULES,
  reads: READS_RULES,
  validity: VALIDITY_RULES,
  outcome: OUTCOME_RULES,
};

/** One chip for one field. Unknown words are neutral and flagged `known: false`. */
export function evidenceChip(field: EvidenceField, word: string): EvidenceChip {
  const trimmed = word.trim();
  if (field === "cohort") return { field, word: trimmed, tone: "neutral", icon: "users", sentence: `cohort: ${trimmed}`, known: true };
  if (field === "tier") {
    const known = (TIERS as readonly string[]).includes(trimmed);
    return { field, word: trimmed, tone: "neutral", icon: "layers", sentence: known ? `evidence tier: ${tierSentence(trimmed)}` : `evidence tier ${trimmed} (not in the closed vocabulary)`, known };
  }
  const rule = RULES[field][trimmed];
  if (!rule) return { field, word: trimmed, tone: "neutral", icon: "tag", sentence: `${field} ${trimmed} (not in the closed vocabulary)`, known: false };
  return { field, word: trimmed, tone: rule.tone, icon: rule.icon, sentence: rule.sentence, known: true };
}

const ORDER: EvidenceField[] = ["status", "tier", "validity", "outcome", "reads", "evidence", "cohort"];

/** Chips for every field the input carries, in the strip's fixed order. Absent fields produce nothing. */
export function evidenceChips(input: EvidenceInput): EvidenceChip[] {
  const out: EvidenceChip[] = [];
  for (const field of ORDER) {
    const word = input[field];
    if (typeof word === "string" && word.trim()) out.push(evidenceChip(field, word));
  }
  return out;
}

/** Words outside the closed vocabulary, for the figure checker. */
export function unknownEvidenceWords(input: EvidenceInput): { field: EvidenceField; word: string }[] {
  return evidenceChips(input)
    .filter((chip) => !chip.known)
    .map((chip) => ({ field: chip.field, word: chip.word }));
}
