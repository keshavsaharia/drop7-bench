/**
 * Site search index, built from the checkout on the server.
 *
 * Every entry is a title, a route, a short summary, and tags, drawn from
 * frontmatter and record fields that already exist. Nothing is fetched and no
 * research number is computed; a summary quotes a record's own text and is
 * cut only at a word boundary. Every source is optional: a checkout with no
 * approaches/, research/, docs/, or content pages yields a shorter index, never
 * a build failure.
 */

import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import GithubSlugger from "github-slugger";
import { listConceptPages, listLearnPages, listTechniquePages, LEARN_DIR } from "./learn.ts";
import { listLogEntries } from "./log.ts";
import { listResults } from "./records.ts";
import { DOCS_DIR, familyMeta, getExperiments, getTheories, listAllApproaches, listFamilies } from "./repo.ts";
import { listTechniques } from "./techniques.ts";

export type SearchEntryKind =
  | "approach"
  | "technique"
  | "engine"
  | "concept"
  | "guide"
  | "glossary"
  | "doc"
  | "theory"
  | "experiment"
  | "result"
  | "log";

export interface SearchEntry {
  kind: SearchEntryKind;
  title: string;
  href: string;
  summary: string;
  tags: string[];
}

const SUMMARY_LIMIT = 240;

/** Cut long text at a word boundary so a recorded figure is never split. */
function clip(text: string): string {
  const flat = text.replace(/\s+/g, " ").trim();
  if (flat.length <= SUMMARY_LIMIT) return flat;
  const cut = flat.lastIndexOf(" ", SUMMARY_LIMIT);
  return `${flat.slice(0, cut > 40 ? cut : SUMMARY_LIMIT)}…`;
}

/** Markdown links and emphasis to plain text, for titles and summaries. */
function plain(markdown: string): string {
  return markdown
    .replace(/\[([^\]]*)\]\([^)]*\)/g, "$1")
    .replace(/[*_`]/g, "")
    .trim();
}

function tags(...values: (string | null | undefined | false)[]): string[] {
  return [...new Set(values.filter((value): value is string => typeof value === "string" && value.length > 0))];
}

function safe<T>(build: () => T[]): T[] {
  try {
    return build();
  } catch {
    return [];
  }
}

/* ---- Approaches, families, techniques ---- */

function approachEntries(): SearchEntry[] {
  const entries: SearchEntry[] = [];
  for (const family of listFamilies()) {
    const meta = familyMeta(family);
    entries.push({
      kind: "approach",
      title: meta.title,
      href: `/approaches/${family}`,
      summary: clip(meta.summary),
      tags: tags(family, "family"),
    });
  }
  for (const approach of listAllApproaches()) {
    entries.push({
      kind: approach.kind === "engine" ? "engine" : "approach",
      title: approach.title,
      href: `/approaches/${approach.family}/${approach.slug}`,
      summary: clip(approach.summary),
      tags: tags(
        approach.family,
        approach.kind,
        approach.technique,
        approach.status,
        approach.evidence,
        approach.reads,
        approach.featured && "featured",
      ),
    });
  }
  return entries;
}

function techniqueEntries(): SearchEntry[] {
  const written = new Map(listTechniquePages().map((page) => [page.technique, page]));
  return listTechniques().map((technique) => {
    const page = written.get(technique.slug);
    return {
      kind: "technique",
      title: page?.title ?? technique.title,
      href: `/learn/techniques/${page?.slug ?? technique.primerSlug}`,
      summary: clip(page?.summary || technique.oneLine),
      tags: tags(technique.slug, "technique", ...(page?.glossary ?? [])),
    };
  });
}

/* ---- Learn pages, concepts, glossary ---- */

function learnEntries(): SearchEntry[] {
  return listLearnPages().map((page) => ({
    kind: "guide",
    title: page.title,
    href: `/learn/${page.slug}`,
    summary: clip(page.summary),
    tags: tags("learn", page.slug),
  }));
}

function conceptEntries(): SearchEntry[] {
  return listConceptPages().filter((page) => !page.hidden).map((page) => ({
    kind: "concept",
    title: page.title,
    href: `/learn/concepts/${page.slug}`,
    summary: clip(page.summary),
    tags: tags("concept", page.slug),
  }));
}

/**
 * Glossary terms are table rows under section headings in
 * web/content/learn/glossary.mdx: `| **term** | meaning |`. Each entry links
 * to its section, whose id rehype-slug derives from the heading text.
 */
function glossaryEntries(): SearchEntry[] {
  const path = join(LEARN_DIR, "glossary.mdx");
  if (!existsSync(path)) return [];
  const slugger = new GithubSlugger();
  const entries: SearchEntry[] = [];
  let section = "";
  let sectionId = "";
  for (const line of readFileSync(path, "utf8").split("\n")) {
    const heading = /^#{2,3}\s+(.+?)\s*$/.exec(line);
    if (heading) {
      section = plain(heading[1]);
      sectionId = slugger.slug(section);
      continue;
    }
    if (!line.startsWith("|")) continue;
    const cells = line.split("|").slice(1, -1).map((cell) => cell.trim());
    if (cells.length < 2) continue;
    const [term, meaning] = cells;
    if (term === "Term" || /^-+$/.test(term)) continue;
    const title = plain(term);
    if (!title) continue;
    entries.push({
      kind: "glossary",
      title,
      href: sectionId ? `/learn/glossary#${sectionId}` : "/learn/glossary",
      summary: clip(plain(meaning)),
      tags: tags("glossary", section),
    });
  }
  return entries;
}

/* ---- Repository docs ---- */

function walkMarkdown(dir: string): string[] {
  if (!existsSync(dir)) return [];
  const out: string[] = [];
  for (const name of readdirSync(dir).sort()) {
    if (name.startsWith(".")) continue;
    const path = join(dir, name);
    const stats = statSync(path);
    if (stats.isDirectory()) out.push(...walkMarkdown(path));
    else if (stats.isFile() && name.endsWith(".md")) out.push(path);
  }
  return out;
}

/** Title from the first heading and summary from the first paragraph after it. */
function describeDoc(path: string): { title: string; summary: string } {
  const lines = readFileSync(path, "utf8").split("\n");
  let title = "";
  let summary = "";
  let afterTitle = false;
  const paragraph: string[] = [];
  for (const line of lines) {
    if (!title) {
      const heading = /^#\s+(.+?)\s*$/.exec(line);
      if (heading) {
        title = plain(heading[1]);
        afterTitle = true;
      }
      continue;
    }
    if (!afterTitle) continue;
    const trimmed = line.trim();
    const isProse = trimmed.length > 0 && !/^(#|\||[-*]\s|\d+\.\s|<|```|>|!\[)/.test(trimmed) && !/^\*\*[^*]+:\*\*/.test(trimmed);
    if (isProse) {
      paragraph.push(trimmed);
      continue;
    }
    if (paragraph.length > 0) break;
  }
  if (paragraph.length > 0) summary = plain(paragraph.join(" "));
  return { title, summary };
}

function docEntries(): SearchEntry[] {
  return walkMarkdown(DOCS_DIR).map((path) => {
    const rel = relative(DOCS_DIR, path).split("\\").join("/");
    const stem = rel.replace(/\.md$/, "");
    const { title, summary } = describeDoc(path);
    const segments = stem.split("/");
    return {
      kind: "doc",
      title: title || segments.at(-1) || stem,
      href: `/docs/${stem}`,
      summary: clip(summary),
      tags: tags("doc", ...segments.slice(0, -1)),
    };
  });
}

/* ---- Research records ---- */

function theoryEntries(): SearchEntry[] {
  return getTheories().map((theory) => ({
    kind: "theory",
    title: theory.title ?? theory.$id,
    href: `/theories/${theory.$id}`,
    summary: clip(theory.claim ?? ""),
    tags: tags("theory", theory.lifecycle, theory.assessment, theory.evidenceTier, theory.informationClass),
  }));
}

function experimentEntries(): SearchEntry[] {
  return getExperiments().map((experiment) => ({
    kind: "experiment",
    title: experiment.title ?? experiment.$id,
    href: `/experiments/${experiment.$id}`,
    summary: clip(experiment.hypothesis ?? ""),
    tags: tags(
      "experiment",
      experiment.lifecycle,
      experiment.classification,
      experiment.benchmarkTier,
      experiment.informationBoundary,
      experiment.data?.role,
    ),
  }));
}

function resultEntries(): SearchEntry[] {
  return listResults().map((result) => ({
    kind: "result",
    title: result.$id,
    href: `/results/${result.$id}`,
    summary: clip(result.summary ?? ""),
    tags: tags("result", result.runValidity, result.scientificOutcome, result.assessment, result.evidenceTier),
  }));
}

function logEntries(): SearchEntry[] {
  return listLogEntries().map((entry) => ({
    kind: "log",
    title: entry.title,
    href: `/log/${entry.date}`,
    summary: clip(entry.summary ?? ""),
    tags: tags("log", entry.date, ...entry.tags, ...entry.contributors),
  }));
}

/**
 * The whole index in a stable order: approaches, techniques, learn pages,
 * concepts, glossary terms, docs, theories, experiments, results, log entries.
 * Each source is built on its own, so one unreadable file empties only its
 * own section.
 */
export function buildSearchIndex(): SearchEntry[] {
  return [
    ...safe(approachEntries),
    ...safe(techniqueEntries),
    ...safe(learnEntries),
    ...safe(conceptEntries),
    ...safe(glossaryEntries),
    ...safe(docEntries),
    ...safe(theoryEntries),
    ...safe(experimentEntries),
    ...safe(resultEntries),
    ...safe(logEntries),
  ];
}
