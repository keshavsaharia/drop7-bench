/**
 * Generates starter README.mdx documentation for every approach directory
 * that lacks one, using the purpose/status rows of
 * docs/research/experiment-index.md. Hand-written docs (README.mdx,
 * README.md, PREREGISTRATION.md) are never overwritten.
 *
 * Run: node scripts/generate-approach-docs.mjs
 */
import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const ROOT = new URL("..", import.meta.url).pathname;
const INDEX = join(ROOT, "docs/research/experiment-index.md");
const APPROACHES = join(ROOT, "approaches");

/** Escape text so it is safe inline in MDX (no JSX parsing of <, {, }). */
function mdxText(markdown) {
  return markdown
    .replace(/\[([^\]]*)\]\([^)]*\)/g, "$1") // links -> label text
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\{/g, "&#123;")
    .replace(/\}/g, "&#125;");
}

/** Parse the index tables into per-approach rows. */
function parseIndex(source) {
  const byApproach = new Map(); // "family/slug" -> [{title, purpose, status}]
  for (const line of source.split("\n")) {
    if (!line.startsWith("|") || line.includes("---")) continue;
    const cells = line.split("|").slice(1, -1).map((cell) => cell.trim());
    if (cells.length < 3) continue;
    const [first, purpose, status] = cells;
    if (first === "Approach and sources" || first === "Component and sources") continue;
    const links = [...first.matchAll(/\]\((?:\.\.\/)*approaches\/([a-z0-9-]+)\/([a-z0-9-]+)\//g)];
    if (links.length === 0) continue;
    const title = first.replace(/:.*/, "").trim();
    const seen = new Set();
    for (const link of links) {
      const key = `${link[1]}/${link[2]}`;
      if (seen.has(key)) continue;
      seen.add(key);
      if (!byApproach.has(key)) byApproach.set(key, []);
      const rows = byApproach.get(key);
      if (!rows.some((row) => row.title === title && row.purpose === purpose)) {
        rows.push({ title, purpose, status });
      }
    }
  }
  return byApproach;
}

/** Pull a leading status word and evidence class out of a status cell. */
function parseStatus(statusCell) {
  const match = statusCell.match(/\*\*([A-Za-z-]+)(?:\s+—\s+([^*;]+))?\s*;/);
  return {
    status: match?.[1] ?? "Unknown",
    evidence: match?.[2]?.trim() ?? "repository-verified",
    note: statusCell.replace(/\*\*[^*]+\*\*;?\s*/, ""),
  };
}

function titleCase(slug) {
  return slug
    .split("-")
    .map((word) => word[0].toUpperCase() + word.slice(1))
    .join(" ");
}

function main() {
  const index = parseIndex(readFileSync(INDEX, "utf8"));
  let created = 0;
  let skipped = 0;

  for (const family of readdirSync(APPROACHES).sort()) {
    const familyDir = join(APPROACHES, family);
    if (!statSync(familyDir).isDirectory()) continue;
    for (const slug of readdirSync(familyDir).sort()) {
      const dir = join(familyDir, slug);
      if (!statSync(dir).isDirectory()) continue;
      const mdxPath = join(dir, "README.mdx");
      const existing = existsSync(mdxPath) ? readFileSync(mdxPath, "utf8") : null;
      const machineMade = existing !== null && /^(generated|draft): true$/m.test(existing.slice(0, 600));
      if (
        (existing !== null && !machineMade) ||
        existsSync(join(dir, "README.md")) ||
        existsSync(join(dir, "PREREGISTRATION.md"))
      ) {
        skipped += 1;
        continue;
      }
      const rows = index.get(`${family}/${slug}`) ?? [];
      const sources = readdirSync(dir)
        .filter((file) => /\.(ts|cpp|hpp|py|json)$/.test(file))
        .sort();
      // First comment block of each source file, as the code's own description.
      const headers = sources
        .map((file) => {
          const text = readFileSync(join(dir, file), "utf8").slice(0, 4000);
          let block = null;
          const star = text.match(/\/\*\*?([\s\S]*?)\*\//);
          const hash = text.match(/^((?:#(?!!)[^\n]*\n){2,})/m);
          const slash = text.match(/^((?:\/\/[^\n]*\n){2,})/m);
          const doc = text.match(/^"""([\s\S]*?)"""/m);
          if (star) block = star[1].split("\n").map((l) => l.replace(/^\s*\*\s?/, "")).join(" ");
          else if (doc) block = doc[1];
          else if (slash) block = slash[1].split("\n").map((l) => l.replace(/^\/\/\s?/, "")).join(" ");
          else if (hash) block = hash[1].split("\n").map((l) => l.replace(/^#\s?/, "")).join(" ");
          if (!block) return null;
          const clean = block.replace(/\s+/g, " ").trim();
          if (clean.length < 40) return null;
          return { file, text: clean.length > 600 ? clean.slice(0, 600) + "…" : clean };
        })
        .filter(Boolean);
      const primary = rows[0] ? parseStatus(rows[0].status) : null;
      const statuses = [...new Set(rows.map((row) => parseStatus(row.status).status))];
      const evidences = [...new Set(rows.map((row) => parseStatus(row.status).evidence))];

      const body = [];
      body.push(
        '<Callout title="Draft page — generated from the repository\'s records" tone="warn">',
        "This page was assembled automatically from the experiment index and the",
        "source files' own comments. It has not been reviewed or rewritten for a",
        "general reader yet, and it makes no claim beyond what those records say.",
        "See the family page for the idea in context.",
        "</Callout>",
        "",
      );
      if (rows.length === 0) {
        body.push(
          "## What the records say",
          "",
          "This approach has no row in the experiment index and no ledger entry,",
          "so there is **no retained result** for it. What follows is taken from",
          "the source files themselves.",
          "",
        );
      } else {
        body.push("## What the records say", "");
        for (const row of rows) {
          const parsed = parseStatus(row.status);
          body.push(`## ${mdxText(row.title)}`, "");
          body.push(mdxText(row.purpose), "");
          body.push(
            `**Status: ${parsed.status}** (${parsed.evidence}). ${mdxText(parsed.note)}`,
            "",
          );
        }
      }

      if (headers.length > 0) {
        body.push("## What the code says about itself", "");
        for (const h of headers) {
          body.push(`**\`${h.file}\`** — ${mdxText(h.text)}`, "");
        }
      }
      body.push(
        "## Where to look next",
        "",
        `- The [${family} family page](/approaches/${family}) for the idea in plain language.`,
        "- The [experiment index](/docs/research/experiment-index) and [full ledger](/docs/research/history) for the recorded evidence.",
        "- The [concepts primer](/learn/concepts) for the search and learning ideas this approach uses.",
        "",
      );

      const summarySource = rows[0]
        ? mdxText(rows[0].purpose).replace(/\s+/g, " ").trim()
        : "No retained result; the page describes what the source files say they do.";
      const summary = summarySource.replace(/"/g, "'").slice(0, 240);
      const mdx = `---
title: ${titleCase(slug)}
family: ${family}
summary: "${summary}"
status: ${(statuses[0] ?? "unknown").toLowerCase()}
evidence: ${evidences[0] ?? "unknown"}
draft: true
---

${body.join("\n")}
## Sources

${sources.map((file) => `- \`${file}\``).join("\n")}
`;
      writeFileSync(join(dir, "README.mdx"), mdx);
      created += 1;
    }
  }
  console.log(`created ${created} README.mdx files; skipped ${skipped} documented approaches`);
}

main();
