import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import matter from "gray-matter";

export const LEARN_DIR = join(process.cwd(), "content", "learn");
export const CONCEPTS_DIR = join(LEARN_DIR, "concepts");

export interface LearnPageInfo {
  slug: string;
  title: string;
  summary: string;
  order: number;
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
