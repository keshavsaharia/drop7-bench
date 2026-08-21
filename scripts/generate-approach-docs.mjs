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
      if (
        existsSync(join(dir, "README.mdx")) ||
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
      const primary = rows[0] ? parseStatus(rows[0].status) : null;
      const statuses = [...new Set(rows.map((row) => parseStatus(row.status).status))];
      const evidences = [...new Set(rows.map((row) => parseStatus(row.status).evidence))];

      const body = [];
      if (rows.length === 0) {
        body.push(
          "This approach is not yet listed in the experiment index. Read the",
          "source files below and the family page for context, and expand this",
          "page when the approach is understood.",
        );
      } else {
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

      const mdx = `---
title: ${titleCase(slug)}
family: ${family}
status: ${statuses[0] ?? "Unknown"}
evidence: ${evidences[0] ?? "unknown"}
generated: true
---

<Callout title="Generated starter page" tone="info">
This page was generated from the experiment index. Expand it with a visual
explanation of the mechanism when you work on this approach.
</Callout>

${body.join("\n")}
## Sources

${sources.map((file) => `- \`${file}\``).join("\n")}

## More evidence

- [Experiment index](/docs/research/experiment-index)
- [Full research ledger](/docs/research/history)
- [Family overview](/approaches/${family})
`;
      writeFileSync(join(dir, "README.mdx"), mdx);
      created += 1;
    }
  }
  console.log(`created ${created} README.mdx files; skipped ${skipped} documented approaches`);
}

main();
