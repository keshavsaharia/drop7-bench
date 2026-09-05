import GithubSlugger from "github-slugger";
import { loadLearnPage } from "./learn.ts";

/** The existing glossary remains the single source of definitions and old anchors. */
export const VOCABULARY_TOPICS = [
  { slug: "game", section: "The game", anchor: "the-game", title: "Game mechanics", summary: "Discs, clears, chains, and the rising board.", art: "vocabulary-game", lesson: "survival-vs-score" },
  { slug: "search", section: "Searching", anchor: "searching", title: "Search and decisions", summary: "Choices, chance, and looking ahead before a move.", art: "vocabulary-search", lesson: "chance-vs-choice" },
  { slug: "learning", section: "Learning", anchor: "learning", title: "Learning and training", summary: "Policies, teachers, and learning from played games.", art: "vocabulary-learning", lesson: "learning-from-play" },
  { slug: "evidence", section: "Evidence and records", anchor: "evidence-and-records", title: "Evidence and results", summary: "Game samples, comparisons, and the labels on a result.", art: "vocabulary-evidence", lesson: "heavy-tails" },
] as const;

export interface VocabularyTerm { id: string; title: string; meaning: string }

export function vocabularyTopic(slug: string) {
  return VOCABULARY_TOPICS.find((topic) => topic.slug === slug) ?? null;
}

export function vocabularyTerms(slug: string): VocabularyTerm[] {
  const topic = vocabularyTopic(slug);
  const doc = loadLearnPage("glossary");
  if (!topic || !doc) return [];
  const slugger = new GithubSlugger();
  const terms: VocabularyTerm[] = [];
  let inSection = false;
  for (const line of doc.content.split("\n")) {
    const heading = /^## (.+)$/.exec(line);
    if (heading) inSection = heading[1] === topic.section;
    if (!inSection) continue;
    const row = /^\| \*\*(.+?)\*\* \| (.+) \|\s*$/.exec(line);
    if (row) terms.push({ id: slugger.slug(row[1]), title: row[1], meaning: row[2] });
  }
  return terms;
}
