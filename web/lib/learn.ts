import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import matter from "gray-matter";

export const LEARN_DIR = join(process.cwd(), "content", "learn");
export const CONCEPTS_DIR = join(LEARN_DIR, "concepts");
export const TECHNIQUES_DIR = join(LEARN_DIR, "techniques");

export interface LearnPageInfo {
  slug: string;
  title: string;
  summary: string;
  order: number;
  /** `hidden: true` keeps the page reachable by URL but out of every list. */
  hidden?: boolean;
}

/** A technique primer: web/content/learn/techniques/<slug>.mdx. */
export interface TechniquePageInfo extends LearnPageInfo {
  /** Catalogue slug from web/lib/techniques.ts that this page explains. */
  technique: string;
  /** `family/slug` approach directories the page points at, as written. */
  approaches: string[];
  /** Glossary terms the page introduces, as written. */
  glossary: string[];
}

function toStringList(value: unknown): string[] {
  if (Array.isArray(value)) {
    return value.map((item) => String(item).trim()).filter((item) => item.length > 0);
  }
  if (typeof value === "string" && value.trim().length > 0) return [value.trim()];
  return [];
}

function describeTechniquePage(file: string, data: Record<string, unknown>): TechniquePageInfo {
  const stem = file.replace(/\.mdx$/, "");
  return {
    slug: typeof data.slug === "string" && data.slug.length > 0 ? data.slug : stem,
    title: typeof data.title === "string" ? data.title : stem,
    summary: typeof data.summary === "string" ? data.summary : "",
    order: typeof data.order === "number" ? data.order : 99,
    technique: typeof data.technique === "string" ? data.technique : stem,
    approaches: toStringList(data.approaches),
    glossary: toStringList(data.glossary),
  };
}

/**
 * Technique primers, in `order`. The directory is written page by page, so a
 * checkout may hold some, all, or none of them; whatever exists is returned.
 */
export function listTechniquePages(): TechniquePageInfo[] {
  if (!existsSync(TECHNIQUES_DIR)) return [];
  return readdirSync(TECHNIQUES_DIR)
    .filter((file) => file.endsWith(".mdx"))
    .sort()
    .map((file) => {
      const { data } = matter(readFileSync(join(TECHNIQUES_DIR, file), "utf8"));
      return describeTechniquePage(file, data as Record<string, unknown>);
    })
    .sort((a, b) => a.order - b.order || a.slug.localeCompare(b.slug));
}

/**
 * One technique primer by its `slug` (the filename stem, or the frontmatter
 * `slug` when a page declares one). Null when the page does not exist yet.
 */
export function loadTechniquePage(slug: string) {
  if (!/^[a-z0-9-]+$/.test(slug)) return null;
  const direct = join(TECHNIQUES_DIR, `${slug}.mdx`);
  if (existsSync(direct)) return matter(readFileSync(direct, "utf8"));
  if (!existsSync(TECHNIQUES_DIR)) return null;
  for (const file of readdirSync(TECHNIQUES_DIR)) {
    if (!file.endsWith(".mdx")) continue;
    const parsed = matter(readFileSync(join(TECHNIQUES_DIR, file), "utf8"));
    if ((parsed.data as Record<string, unknown>).slug === slug) return parsed;
  }
  return null;
}

/** The primer whose `technique` key names this catalogue slug, if it has been written. */
export function techniquePageForTechnique(technique: string): TechniquePageInfo | null {
  return listTechniquePages().find((page) => page.technique === technique) ?? null;
}

export function listLearnPages(): LearnPageInfo[] {
  if (!existsSync(LEARN_DIR)) return [];
  return readdirSync(LEARN_DIR)
    .filter((file) => file.endsWith(".mdx"))
    .sort()
    .map((file) => {
      const { data } = matter(readFileSync(join(LEARN_DIR, file), "utf8"));
      return {
        slug: file.replace(/\.mdx$/, ""),
        title: (data.title as string) ?? file.replace(/\.mdx$/, ""),
        summary: (data.summary as string) ?? "",
        order: (data.order as number) ?? 99,
        hidden: data.hidden === true,
      };
    })
    .sort((a, b) => a.order - b.order);
}

export function loadLearnPage(slug: string) {
  if (!/^[a-z0-9-]+$/.test(slug)) return null;
  const path = join(LEARN_DIR, `${slug}.mdx`);
  if (!existsSync(path)) return null;
  return matter(readFileSync(path, "utf8"));
}

/** Concept pages: web/content/learn/concepts/*.mdx — the plain-English primer. */
export function listConceptPages(): LearnPageInfo[] {
  if (!existsSync(CONCEPTS_DIR)) return [];
  return readdirSync(CONCEPTS_DIR)
    .filter((file) => file.endsWith(".mdx"))
    .sort()
    .map((file) => {
      const { data } = matter(readFileSync(join(CONCEPTS_DIR, file), "utf8"));
      return {
        slug: file.replace(/\.mdx$/, ""),
        title: (data.title as string) ?? file.replace(/\.mdx$/, ""),
        summary: (data.summary as string) ?? "",
        order: (data.order as number) ?? 99,
        hidden: data.hidden === true,
      };
    })
    .sort((a, b) => a.order - b.order);
}

export function loadConceptPage(slug: string) {
  if (!/^[a-z0-9-]+$/.test(slug)) return null;
  const path = join(CONCEPTS_DIR, `${slug}.mdx`);
  if (!existsSync(path)) return null;
  return matter(readFileSync(path, "utf8"));
}
