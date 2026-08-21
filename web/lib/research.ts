import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import matter from "gray-matter";

/** Plain-English overlays for machine-readable records: web/content/research/<record-id>.mdx */
export const RESEARCH_CONTENT_DIR = join(process.cwd(), "content", "research");

export function loadRecordOverlay(recordId: string) {
  if (!/^[A-Za-z0-9-]+$/.test(recordId)) return null;
  const path = join(RESEARCH_CONTENT_DIR, `${recordId}.mdx`);
  if (!existsSync(path)) return null;
  return matter(readFileSync(path, "utf8"));
}
