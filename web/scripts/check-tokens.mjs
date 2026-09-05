#!/usr/bin/env node
/**
 * Design-token check.
 *
 * The `@theme static` block at the top of app/globals.css is the one place a
 * literal colour may appear. Every stylesheet, every card art and every
 * figure kit reads a `var(--color-*)` instead, so a palette change lands in
 * one file and dark-mode contrast stays checked in one place.
 *
 * A few exceptions are recorded here rather than left to be rediscovered:
 * the chart kit's own validated ramps (mirrored in lib/charts/palette.ts and
 * held there by a test), SourceBrowser's code ground, the Satori
 * social-image renderer (which cannot resolve a CSS variable), and the
 * file-type marks in FileTree, which are the languages' own colours.
 *
 *   node scripts/check-tokens.mjs
 */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const WEB = resolve(dirname(fileURLToPath(import.meta.url)), "..");

/** Directories scanned, and which extensions in each. */
const SCAN = [
  { dir: "app", extensions: [".css"] },
  { dir: "components", extensions: [".css"] },
  { dir: "components/technique-art", extensions: [".tsx"] },
  { dir: "components/primers", extensions: [".tsx"] },
];

/** Files whose literal colours are deliberate, with the reason. */
const ALLOWED = new Map([
  ["app/globals.css", "the token block itself"],
  ["components/SourceBrowser.module.css", "the code ground, declared on the module root"],
  ["components/CodeSnippet.module.css", "the Shiki code ground, matching the source browser"],
  ["components/discs.tsx", "the Satori palette, mirroring the tokens verbatim"],
  ["lib/social-image.tsx", "Satori cannot resolve a CSS variable"],
  ["lib/social-card.tsx", "Satori cannot resolve a CSS variable"],
  ["components/FileTree.tsx", "file-type marks in the languages' own colours"],
  ["components/charts/charts.css", "the validated chart ramps, mirrored in lib/charts/palette.ts"],
]);

const HEX = /#[0-9a-fA-F]{3,8}\b/g;

function* walk(dir) {
  let entries;
  try {
    entries = readdirSync(dir);
  } catch {
    return;
  }
  for (const entry of entries) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) yield* walk(path);
    else yield path;
  }
}

const violations = [];
const seen = new Set();

for (const { dir, extensions } of SCAN) {
  for (const path of walk(join(WEB, dir))) {
    if (!extensions.some((extension) => path.endsWith(extension))) continue;
    const name = relative(WEB, path).replaceAll("\\", "/");
    if (seen.has(name) || ALLOWED.has(name)) continue;
    seen.add(name);
    const lines = readFileSync(path, "utf8").split("\n");
    lines.forEach((line, index) => {
      // A URL fragment or an SVG local reference is not a colour.
      const stripped = line.replace(/(href|url|xlink:href)\s*[=(]\s*["']?[^"')]*/g, "");
      for (const match of stripped.matchAll(HEX)) {
        violations.push(`${name}:${index + 1}  ${match[0]}  ${line.trim().slice(0, 90)}`);
      }
    });
  }
}

if (violations.length > 0) {
  console.error(`check-tokens: ${violations.length} literal colour(s) outside the token block\n`);
  for (const violation of violations) console.error(`  ${violation}`);
  console.error("\nUse a var(--color-*) token from the @theme static block in app/globals.css.");
  process.exit(1);
}

console.log(`check-tokens: ${seen.size} files scanned, 0 literal colours outside the token block`);
