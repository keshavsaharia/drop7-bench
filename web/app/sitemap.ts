/**
 * The sitemap: every page this console serves, built from the same list
 * functions the pages themselves read.
 *
 * A checkout with no `approaches/`, no `research/` and no `web/content/` still
 * produces a valid sitemap of the fixed pages, because every list function
 * returns an empty array rather than throwing.
 *
 * `lastModified` is only set where the record carries a date of its own — a
 * theory's or experiment's `updatedAt`, a result's `recordedAt`, a log
 * entry's own day. Nothing here stamps today's date on a page to make it look
 * fresh, and prose pages carry no date at all.
 */
import { VOCABULARY_TOPICS } from "@/lib/vocabulary";
import type { MetadataRoute } from "next";
import { COMPETITION_GAME_KEY } from "@/lib/competition/game";
import { loadCompetitionLeaderboard } from "@/lib/competition/ledger";
import { listDocs } from "@/lib/docs";
import { ENGINES } from "@/lib/engines";
import { listConceptPages, listLearnPages, listTechniquePages } from "@/lib/learn";
import { listLogEntries } from "@/lib/log";
import { loadLeaderboard } from "@/lib/leaderboard";
import { SITE_URL } from "@/lib/metadata";
import {
  getExperiments,
  getResults,
  getTheories,
  listAllApproaches,
  listFamilies,
} from "@/lib/repo";
import { listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";

/**
 * The pages that exist whatever the checkout holds. `/analytics` is
 * authenticated and already marked noindex; `/src` and `/api` are excluded
 * here and in robots.txt.
 */
const FIXED_PATHS: readonly string[] = [
  "/",
  "/play",
  "/leaderboard",
  "/compete",
  "/approach",
  "/engine",
  "/diagnostics",
  "/learn",
  "/learn/concepts",
  "/learn/vocabulary",
  "/learn/techniques",
  "/research",
  "/theories",
  "/experiments",
  "/results",
  "/log",
  "/docs",
  "/privacy",
  "/support",
  "/terms",
];

function url(path: string): string {
  return new URL(path, SITE_URL).toString();
}

/** A date the record actually carries, or nothing. Never a substitute. */
function recorded(value: unknown): string | undefined {
  if (typeof value !== "string" || value.length === 0) return undefined;
  return Number.isNaN(Date.parse(value)) ? undefined : value;
}

function entry(path: string, lastModified?: string): MetadataRoute.Sitemap[number] {
  return lastModified === undefined ? { url: url(path) } : { url: url(path), lastModified };
}

export default async function sitemap(): Promise<MetadataRoute.Sitemap> {
  const leaderboard = loadLeaderboard();
  const competition = await loadCompetitionLeaderboard(COMPETITION_GAME_KEY, 250);
  return [
    ...FIXED_PATHS.map((path) => entry(path)),

    // Approaches: the technique groups, the family directories, the approaches.
    ...listTechniques().map((technique) => entry(`/approach/technique/${technique.slug}`)),
    ...listFamilies().map((family) => entry(`/approach/${family}`)),
    ...listAllApproaches().map((approach) => entry(`/approach/${approach.family}/${approach.slug}`)),

    // Engines.
    ...ENGINES.map((engine) => entry(`/engine/${engine.slug}`)),

    // Replays present in the generated playground data and public competition ledger.
    ...(leaderboard?.games ?? []).map((game) => entry(`/leaderboard/${game.policyId}/${game.roundId}`)),
    ...competition.entries.map((submission) =>
      entry(`/leaderboard/human/${submission.submissionId}`, recorded(submission.submittedAt)),
    ),

    // Learn: the pages, the concepts in reading order, the technique primers.
    ...VOCABULARY_TOPICS.map((topic) => entry(`/learn/vocabulary/${topic.slug}`)),
    ...listLearnPages().map((page) => entry(`/learn/${page.slug}`)),
    ...listConceptPages().map((page) => entry(`/learn/concepts/${page.slug}`)),
    ...listTechniquePages().map((page) => entry(`/learn/techniques/${page.slug}`)),

    // The research log: the entry's own day is its last modification.
    ...listLogEntries().map((log) => entry(`/log/${log.date}`, recorded(log.date))),

    // Repository documents.
    ...listDocs().map((doc) => entry(doc.href)),

    // Records, each with the timestamp its own file carries.
    ...getTheories().map((theory) => entry(`/theories/${theory.theoryId}`, recorded(theory.updatedAt))),
    ...getExperiments().map((experiment) =>
      entry(`/experiments/${experiment.experimentId}`, recorded(experiment.updatedAt)),
    ),
    ...getResults().map((result) => entry(`/results/${result.resultId}`, recorded(result.recordedAt))),
  ];
}
