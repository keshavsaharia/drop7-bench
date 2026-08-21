import { readdirSync, readFileSync, existsSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import matter from "gray-matter";

/** Absolute path to the repository root (the parent of this Next.js app). */
export const REPO_ROOT = process.env.DROP7_REPO_ROOT
  ? resolve(process.env.DROP7_REPO_ROOT)
  : resolve(process.cwd(), "..");

export const APPROACHES_DIR = join(REPO_ROOT, "approaches");
export const RESEARCH_DIR = join(REPO_ROOT, "research");
export const DOCS_DIR = join(REPO_ROOT, "docs");
export const DATA_DIR = join(REPO_ROOT, "web", "data");

export function readRepoFile(relativePath: string): string | null {
  const path = join(REPO_ROOT, relativePath);
  if (!existsSync(path)) return null;
  return readFileSync(path, "utf8");
}

export function listJsonRecords<T>(subdir: string): (T & { $id: string })[] {
  const dir = join(RESEARCH_DIR, subdir);
  if (!existsSync(dir)) return [];
  return readdirSync(dir)
    .filter((file) => file.endsWith(".json"))
    .sort()
    .map((file) => ({
      $id: file.replace(/\.json$/, ""),
      ...(JSON.parse(readFileSync(join(dir, file), "utf8")) as T),
    }));
}

/* ---- Research record types (mirrors research/schemas/*-v1.schema.json) ---- */

export interface TheoryRecord {
  theoryId: string;
  title: string;
  claim: string;
  mechanism: string;
  falsificationCriteria: string[];
  informationClass: string;
  lifecycle: string;
  assessment: string;
  evidenceTier: string;
  dependencies: string[];
  evidenceRefs: string[];
  createdBy: { platform: string; model: string; agentId: string | null };
  createdAt: string;
  updatedAt: string;
}

export interface ExperimentRecord {
  experimentId: string;
  theoryIds: string[];
  title: string;
  classification: string;
  hypothesis: string;
  candidate: { name: string; entryPoint: string | null; manifestRef: string | null };
  comparator: { name: string; entryPoint: string | null; manifestRef: string | null };
  informationBoundary: string;
  benchmarkTier: string;
  data: { role: string; seedLeaseRefs: string[]; reuseDisclosure: string };
  metrics: { primary: string; secondary: string[]; statisticalUnit: string };
  gate: { passCriteria: string[]; failureAction: string; passAction: string };
  lifecycle: string;
  createdAt: string;
  updatedAt: string;
}

export interface ResultRecord {
  resultId: string;
  theoryIds: string[];
  experimentId: string;
  runIds: string[];
  runValidity: string;
  scientificOutcome: string;
  assessment: string;
  evidenceTier: string;
  summary: string;
  metrics: Record<string, unknown>;
  gateChecks: { criterion: string; passed: boolean | null; observed: string }[];
  limitations: string[];
  contributionIds: string[];
  recordedAt: string;
}

export const getTheories = () => listJsonRecords<TheoryRecord>("theories");
export const getExperiments = () => listJsonRecords<ExperimentRecord>("experiments");
export const getResults = () => listJsonRecords<ResultRecord>("results");

/* ---- Approach directories ---- */

export interface ApproachEntry {
  title: string;
  summary: string;
  status: string;
  draft: boolean;
  family: string;
  slug: string;
  hasDocs: boolean;
  sourceFiles: string[];
}

export function listFamilies(): string[] {
  if (!existsSync(APPROACHES_DIR)) return [];
  return readdirSync(APPROACHES_DIR)
    .filter((entry) => statSync(join(APPROACHES_DIR, entry)).isDirectory())
    .sort();
}

export function listApproaches(family: string): ApproachEntry[] {
  const dir = join(APPROACHES_DIR, family);
  if (!existsSync(dir)) return [];
  return readdirSync(dir)
    .filter((entry) => statSync(join(dir, entry)).isDirectory())
    .sort()
    .map((slug) => {
      const approachDir = join(dir, slug);
      const sourceFiles = readdirSync(approachDir)
        .filter((file) => /\.(ts|tsx|cpp|hpp|py|md|mdx|json)$/.test(file))
        .sort();
      const mdxPath = join(approachDir, "README.mdx");
      const front = existsSync(mdxPath) ? (matter(readFileSync(mdxPath, "utf8")).data as Record<string, unknown>) : {};
      return {
        family,
        slug,
        hasDocs:
          existsSync(join(approachDir, "README.mdx")) ||
          existsSync(join(approachDir, "README.md")),
        sourceFiles,
        title: typeof front.title === "string" ? front.title : slug,
        summary: typeof front.summary === "string" ? front.summary : "",
        status: typeof front.status === "string" ? front.status : "",
        draft: front.draft === true,
      };
    });
}

export function approachDocPath(family: string, slug: string): string | null {
  const dir = join(APPROACHES_DIR, family, slug);
  for (const name of ["README.mdx", "README.md", "PREREGISTRATION.md"]) {
    const path = join(dir, name);
    if (existsSync(path)) return path;
  }
  return null;
}

/** True when a hand-written operational README.md sits alongside the rendered README.mdx. */
export function approachOperationalNotes(family: string, slug: string): string | null {
  const dir = join(APPROACHES_DIR, family, slug);
  if (!existsSync(join(dir, "README.mdx"))) return null;
  return existsSync(join(dir, "README.md")) ? `approaches/${family}/${slug}/README.md` : null;
}

export function familyMeta(family: string): { title: string; summary: string } {
  const path = familyDocPath(family);
  if (!path) return { title: family, summary: "" };
  const data = matter(readFileSync(path, "utf8")).data as Record<string, unknown>;
  return {
    title: typeof data.title === "string" ? data.title : family,
    summary: typeof data.summary === "string" ? data.summary : "",
  };
}

export function familyDocPath(family: string): string | null {
  const path = join(APPROACHES_DIR, family, "README.mdx");
  return existsSync(path) ? path : null;
}
