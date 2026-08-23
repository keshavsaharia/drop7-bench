import { readdirSync, readFileSync, existsSync, statSync } from "node:fs";
import { basename, extname, join, resolve, sep } from "node:path";
import matter from "gray-matter";

/** Absolute path to the checkout, or its build-staged copy in a Lambda bundle. */
const checkoutRoot = resolve(process.cwd(), "..");
const packagedRoot = resolve(process.cwd(), "build", "repo");

export const REPO_ROOT = process.env.DROP7_REPO_ROOT
  ? resolve(process.env.DROP7_REPO_ROOT)
  : existsSync(join(checkoutRoot, "approaches"))
    ? checkoutRoot
    : packagedRoot;

export const APPROACHES_DIR = join(REPO_ROOT, "approaches");
export const RESEARCH_DIR = join(REPO_ROOT, "research");
export const DOCS_DIR = join(REPO_ROOT, "docs");
export const DATA_DIR = join(REPO_ROOT, "web", "data");

export function readRepoFile(relativePath: string): string | null {
  const path = join(REPO_ROOT, relativePath);
  if (!existsSync(path)) return null;
  return readFileSync(path, "utf8");
}

/* ---- Repository source browser ---- */

const SOURCE_ROOTS = new Set(["src", "approaches"]);
const MAX_SOURCE_BYTES = 2 * 1024 * 1024;

const SOURCE_LANGUAGES: Record<string, string> = {
  ".bash": "bash",
  ".c": "c",
  ".cc": "cpp",
  ".cpp": "cpp",
  ".css": "css",
  ".csv": "csv",
  ".cxx": "cpp",
  ".go": "go",
  ".h": "cpp",
  ".hh": "cpp",
  ".hpp": "cpp",
  ".html": "html",
  ".java": "java",
  ".js": "javascript",
  ".json": "json",
  ".jsonl": "json",
  ".jsx": "jsx",
  ".kt": "kotlin",
  ".md": "markdown",
  ".mdx": "mdx",
  ".mjs": "javascript",
  ".py": "python",
  ".rs": "rust",
  ".scss": "scss",
  ".sh": "bash",
  ".sql": "sql",
  ".swift": "swift",
  ".toml": "toml",
  ".ts": "typescript",
  ".tsx": "tsx",
  ".txt": "text",
  ".xml": "xml",
  ".yaml": "yaml",
  ".yml": "yaml",
  ".zsh": "bash",
};

const SPECIAL_SOURCE_LANGUAGES: Record<string, string> = {
  Dockerfile: "dockerfile",
  Makefile: "makefile",
};

export interface RepoSourceTreeNode {
  name: string;
  path: string;
  href: string;
  children?: RepoSourceTreeNode[];
}

export interface RepoSourceFile {
  kind: "file";
  name: string;
  path: string;
  href: string;
  language: string;
  source: string;
  bytes: number;
  lines: number;
}

export interface RepoSourceDirectory {
  kind: "directory";
  name: string;
  path: string;
  href: string;
  children: RepoSourceTreeNode[];
}

export type RepoSourceEntry = RepoSourceFile | RepoSourceDirectory;

export function sourceLanguage(path: string): string | null {
  return SPECIAL_SOURCE_LANGUAGES[basename(path)] ?? SOURCE_LANGUAGES[extname(path).toLowerCase()] ?? null;
}

export function sourceLanguageLabel(language: string): string {
  const labels: Record<string, string> = {
    bash: "Shell",
    c: "C",
    cpp: "C++",
    css: "CSS",
    dockerfile: "Dockerfile",
    go: "Go",
    html: "HTML",
    java: "Java",
    javascript: "JavaScript",
    json: "JSON",
    jsx: "JSX",
    kotlin: "Kotlin",
    makefile: "Makefile",
    markdown: "Markdown",
    mdx: "MDX",
    python: "Python",
    rust: "Rust",
    scss: "SCSS",
    sql: "SQL",
    swift: "Swift",
    text: "Plain text",
    toml: "TOML",
    tsx: "TSX",
    typescript: "TypeScript",
    xml: "XML",
    yaml: "YAML",
  };
  return labels[language] ?? language;
}

function sourceHref(path: string): string {
  return `/${path.split("/").map(encodeURIComponent).join("/")}`;
}

function sourceAbsolutePath(repoPath: string): string | null {
  if (!repoPath || repoPath.includes("\\") || repoPath.startsWith("/")) return null;
  const parts = repoPath.split("/");
  if (!SOURCE_ROOTS.has(parts[0]) || parts.some((part) => !part || part === "." || part === "..")) {
    return null;
  }
  const absolute = resolve(REPO_ROOT, ...parts);
  if (!absolute.startsWith(`${REPO_ROOT}${sep}`)) return null;
  return absolute;
}

function sourceTreeNode(repoPath: string): RepoSourceTreeNode | null {
  const absolute = sourceAbsolutePath(repoPath);
  if (!absolute || !existsSync(absolute)) return null;
  const stats = statSync(absolute);
  if (stats.isFile()) {
    if (!sourceLanguage(repoPath) || stats.size > MAX_SOURCE_BYTES) return null;
    return {
      name: basename(repoPath),
      path: repoPath,
      href: sourceHref(repoPath),
    };
  }
  if (!stats.isDirectory()) return null;

  const children = readdirSync(absolute, { withFileTypes: true })
    .filter((entry) => !entry.name.startsWith(".") && !entry.isSymbolicLink())
    .map((entry) => sourceTreeNode(`${repoPath}/${entry.name}`))
    .filter((entry): entry is RepoSourceTreeNode => entry !== null)
    .sort((a, b) => {
      const aDirectory = a.children !== undefined;
      const bDirectory = b.children !== undefined;
      if (aDirectory !== bDirectory) return aDirectory ? -1 : 1;
      return a.name.localeCompare(b.name);
    });

  if (children.length === 0) return null;
  return {
    name: basename(repoPath),
    path: repoPath,
    href: sourceHref(repoPath),
    children,
  };
}

/** Build a serializable, source-only tree rooted at `src/` or one approach. */
export function getRepoSourceTree(repoPath: string): RepoSourceTreeNode | null {
  return sourceTreeNode(repoPath);
}

/** Read one viewable source file or list one source directory from the checkout. */
export function getRepoSource(repoPath: string): RepoSourceEntry | null {
  const absolute = sourceAbsolutePath(repoPath);
  if (!absolute || !existsSync(absolute)) return null;
  const stats = statSync(absolute);
  const href = sourceHref(repoPath);

  if (stats.isDirectory()) {
    const tree = sourceTreeNode(repoPath);
    if (!tree?.children) return null;
    return {
      kind: "directory",
      name: basename(repoPath),
      path: repoPath,
      href,
      children: tree.children,
    };
  }

  const language = sourceLanguage(repoPath);
  if (!stats.isFile() || !language || stats.size > MAX_SOURCE_BYTES) return null;
  const source = readFileSync(absolute, "utf8");
  const lines = source.length === 0 ? 0 : source.split("\n").length - (source.endsWith("\n") ? 1 : 0);
  return {
    kind: "file",
    name: basename(repoPath),
    path: repoPath,
    href,
    language,
    source,
    bytes: Buffer.byteLength(source, "utf8"),
    lines,
  };
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
