#!/usr/bin/env node
/**
 * Verifies every figure/diagram embed points at an existing file:
 *   - ```figure <name>``` / ```figure\n<name>\n...``` fences and
 *     <Figure name="…"/> tags -> web/content/figures/<name>.svg
 *   - ```diagram ...``` fences and <Diagram name="…"/> tags
 *     -> web/content/figures/diagrams/<name>.svg
 * Scans docs/**\/*.md and web/content/**\/*.mdx by default, or the paths given.
 * Usage: node web/scripts/check-figures.mjs [file-or-directory ...]
 * Exit code 1 if any reference is unresolved.
 */
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, resolve } from "node:path";

const webDir = resolve(new URL(".", import.meta.url).pathname, "..");
const repoRoot = resolve(webDir, "..");
const figuresDir = join(webDir, "content", "figures");

function* walk(path) {
  const st = statSync(path);
  if (st.isDirectory()) {
    for (const entry of readdirSync(path)) {
      if (entry === "node_modules" || entry.startsWith(".")) continue;
      yield* walk(join(path, entry));
    }
  } else if (/\.mdx?$/.test(path)) {
    yield path;
  }
}

/** Mirror of parseEmbedFence in web/components/Markdown.tsx, on raw text. */
function* fenceRefs(text) {
  const re = /^(`{3,}|~{3,})[ \t]*(figure|diagram)\b[ \t]*([^\n]*)\n([\s\S]*?)^\1[ \t]*$/gm;
  for (const m of text.matchAll(re)) {
    const [, , kind, meta, body] = m;
    const line = text.slice(0, m.index).split("\n").length;
    const lines = body.split("\n").map((l) => l.trim()).filter(Boolean);
    const name = (meta.trim().split(/\s+/)[0] || lines[0] || "");
    yield { kind, name, line, form: "fence" };
  }
}

function* tagRefs(text) {
  const re = /<(Figure|Diagram)\b[^>]*?\bname=["']([^"']*)["']/g;
  for (const m of text.matchAll(re)) {
    const line = text.slice(0, m.index).split("\n").length;
    yield { kind: m[1].toLowerCase(), name: m[2], line, form: "tag" };
  }
}

function target(ref) {
  return ref.kind === "diagram" ? join(figuresDir, "diagrams", `${ref.name}.svg`) : join(figuresDir, `${ref.name}.svg`);
}

const args = process.argv.slice(2);
const roots = args.length ? args.map((a) => resolve(a)) : [join(repoRoot, "docs"), join(webDir, "content")];

let files = 0;
let refs = 0;
let failures = 0;
for (const root of roots) {
  if (!existsSync(root)) continue;
  for (const file of walk(root)) {
    files += 1;
    const text = readFileSync(file, "utf8");
    const isMdx = file.endsWith(".mdx");
    const found = [...fenceRefs(text), ...(isMdx ? tagRefs(text) : [])];
    for (const ref of found) {
      refs += 1;
      const rel = relative(repoRoot, file);
      if (!/^[a-z0-9-]+$/.test(ref.name)) {
        failures += 1;
        console.log(`FAIL ${rel}:${ref.line}: ${ref.kind} ${ref.form} has no valid name (${JSON.stringify(ref.name)})`);
        continue;
      }
      const path = target(ref);
      if (!existsSync(path)) {
        failures += 1;
        console.log(`FAIL ${rel}:${ref.line}: ${ref.kind} "${ref.name}" -> missing ${relative(repoRoot, path)}`);
      }
    }
  }
}
console.log(`${files} files scanned, ${refs} figure/diagram references, ${failures} unresolved`);
process.exit(failures ? 1 : 0);
