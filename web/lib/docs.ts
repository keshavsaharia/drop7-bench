/**
 * Repository documents under docs/.
 *
 * `RESEARCH_DOCS` is the curated short list other pages link to. The
 * catalogue functions walk `docs/**\/*.md` at request time through the same
 * REPO_ROOT resolution as lib/repo.ts and group every file by its path, so
 * the /docs index shows what the checkout actually contains. A title is the
 * file's first `# ` heading; nothing else in the file is parsed. A checkout
 * with no docs/ directory yields an empty catalogue.
 */
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";
import { DOCS_DIR } from "./repo.ts";

export const RESEARCH_DOCS = [
  {
    href: "/docs/research/status",
    title: "Status",
    summary: "The current evidence boundary: what is known, what is open, and what is not claimed.",
  },
  {
    href: "/docs/methodology",
    title: "Methodology",
    summary: "Game, information, seed, and statistical rules the program is bound to.",
  },
  {
    href: "/docs/benchmarks",
    title: "Benchmark contract",
    summary: "How a policy advances through the research tiers, and what each tier is allowed to claim.",
  },
  {
    href: "/docs/strategies",
    title: "Strategy landscape",
    summary: "The families of approaches that have been tried, and how they relate.",
  },
  {
    href: "/docs/research/experiment-index",
    title: "Experiment index",
    summary: "The inventory of registered and ledger-recorded experiments.",
  },
  {
    href: "/docs/research/history",
    title: "Full ledger",
    summary: "The historical record of configurations and outcomes.",
  },
  {
    href: "/docs/d7p-protocol",
    title: "D7P protocol",
    summary: "The wire protocol used to drive a policy against scripted rounds.",
  },
  {
    href: "/docs/reproducibility",
    title: "Reproducibility",
    summary: "How a checkout is expected to rebuild and re-run the recorded work.",
  },
] as const;

export type DocGroupId = "reader" | "ledger" | "findings" | "hardware" | "agents" | "other";

export interface DocEntry {
  /** Path relative to docs/ without the extension: "research/status". */
  slug: string;
  /** Console route: "/docs/research/status". */
  href: string;
  /** Repository path: "docs/research/status.md". */
  path: string;
  /** The first `# ` heading, or the file stem when there is none. */
  title: string;
  /** The first prose paragraph, flattened for metadata and link previews. */
  summary: string;
  group: DocGroupId;
}

export interface DocGroup {
  id: DocGroupId;
  title: string;
  /** One sentence for the index. */
  summary: string;
  /** Written for the agents that work on the program; the index marks it. */
  agentFacing: boolean;
  entries: DocEntry[];
}

/** Reader documents, in the order the index lists them. */
const READER_ORDER: readonly string[] = [
  "research/status",
  "methodology",
  "benchmarks",
  "strategies",
  "research/roadmap",
  "reproducibility",
  "provenance",
  "d7p-protocol",
];

const LEDGER_SLUGS: readonly string[] = ["research/history", "research/experiment-index"];

const GROUPS: readonly Omit<DocGroup, "entries">[] = [
  {
    id: "reader",
    title: "Reader documents",
    summary:
      "The rules of the program: how a game is scored, how a claim is tested, where the evidence stands, and what comes next.",
    agentFacing: false,
  },
  {
    id: "ledger",
    title: "Ledger",
    summary: "Every configuration that was run and what came of it, as recorded.",
    agentFacing: false,
  },
  {
    id: "findings",
    title: "Findings and audits",
    summary: "Exploratory findings and independent audits, each labelled with its own evidence tier.",
    agentFacing: false,
  },
  {
    id: "hardware",
    title: "Hardware",
    summary: "Profiles of the machines the runs were timed on.",
    agentFacing: false,
  },
  {
    id: "agents",
    title: "Agent contracts",
    summary: "The contracts an agent follows when it works on this program.",
    agentFacing: true,
  },
  {
    id: "other",
    title: "Other documents",
    summary: "Documents that fall in none of the groups above.",
    agentFacing: false,
  },
];

function groupFor(slug: string): DocGroupId {
  if (READER_ORDER.includes(slug)) return "reader";
  if (LEDGER_SLUGS.includes(slug) || slug.startsWith("research/status/")) return "ledger";
  if (slug.startsWith("exploratory/")) return "findings";
  if (slug.startsWith("hardware/")) return "hardware";
  if (slug.startsWith("agents/")) return "agents";
  return "other";
}

/** Every .md file under a directory, as paths relative to it with `/` separators. */
function walkMarkdown(dir: string): string[] {
  if (!existsSync(dir)) return [];
  const found: string[] = [];
  const visit = (current: string) => {
    for (const name of readdirSync(current).sort()) {
      const path = join(current, name);
      const stats = statSync(path);
      if (stats.isDirectory()) {
        visit(path);
      } else if (stats.isFile() && name.endsWith(".md")) {
        found.push(relative(dir, path).split(sep).join("/"));
      }
    }
  };
  visit(dir);
  return found;
}

/** The first `# ` heading outside a code fence. */
function firstHeading(source: string): string | null {
  let inFence = false;
  for (const line of source.split(/\r?\n/)) {
    if (/^ {0,3}(`{3,}|~{3,})/.test(line)) {
      inFence = !inFence;
      continue;
    }
    if (inFence) continue;
    const match = /^ {0,3}# {1,}(.+?)\s*#*\s*$/.exec(line);
    if (match) return match[1].trim();
  }
  return null;
}

/** The first prose paragraph after the title, with Markdown marks removed. */
function firstParagraph(source: string): string {
  const lines = source.split(/\r?\n/);
  const paragraph: string[] = [];
  let pastTitle = false;
  let inFence = false;
  for (const line of lines) {
    if (/^ {0,3}(`{3,}|~{3,})/.test(line)) {
      inFence = !inFence;
      continue;
    }
    if (inFence) continue;
    if (!pastTitle && /^ {0,3}#\s+/.test(line)) {
      pastTitle = true;
      continue;
    }
    if (!pastTitle) continue;
    const trimmed = line.trim();
    const isProse =
      trimmed.length > 0 &&
      !/^(#|\||[-*]\s|\d+\.\s|<|>|!\[)/.test(trimmed) &&
      !/^\*\*[^*]+:\*\*/.test(trimmed);
    if (isProse) {
      paragraph.push(trimmed);
    } else if (paragraph.length > 0) {
      break;
    }
  }
  return paragraph
    .join(" ")
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/[*_`]/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

function compareEntries(a: DocEntry, b: DocEntry): number {
  if (a.group === "reader" && b.group === "reader") {
    return READER_ORDER.indexOf(a.slug) - READER_ORDER.indexOf(b.slug);
  }
  // A folder's README is its index and leads its group.
  const aReadme = /(^|\/)README$/.test(a.slug) ? 0 : 1;
  const bReadme = /(^|\/)README$/.test(b.slug) ? 0 : 1;
  if (aReadme !== bReadme) return aReadme - bReadme;
  return a.slug.localeCompare(b.slug);
}

/** Every document under docs/, ungrouped. Empty when the directory is absent. */
export function listDocs(): DocEntry[] {
  return walkMarkdown(DOCS_DIR).map((file) => {
    const slug = file.replace(/\.md$/, "");
    const source = readFileSync(join(DOCS_DIR, file), "utf8");
    const stem = slug.split("/").at(-1) ?? slug;
    return {
      slug,
      href: `/docs/${slug}`,
      path: `docs/${file}`,
      title: firstHeading(source) ?? stem,
      summary: firstParagraph(source),
      group: groupFor(slug),
    };
  });
}

/** The documents grouped for the /docs index, in a fixed group order; empty groups are dropped. */
export function listDocCatalogue(): DocGroup[] {
  const entries = listDocs();
  return GROUPS.map((group) => ({
    ...group,
    entries: entries.filter((entry) => entry.group === group.id).sort(compareEntries),
  })).filter((group) => group.entries.length > 0);
}
