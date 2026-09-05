#!/usr/bin/env node
/**
 * Keep route discovery, canonical metadata and link previews from drifting.
 * This is intentionally a source check: it runs before `next build`, including
 * on an empty checkout where dynamic content collections have no entries.
 */
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const appRoot = join(webRoot, "app");
const errors = [];

function walk(directory, accept) {
  const found = [];
  for (const name of readdirSync(directory).sort()) {
    const path = join(directory, name);
    const stats = statSync(path);
    if (stats.isDirectory()) found.push(...walk(path, accept));
    else if (accept(path)) found.push(path);
  }
  return found;
}

for (const page of walk(appRoot, (path) => path.endsWith("/page.tsx"))) {
  const routeFile = relative(webRoot, page);
  const routeDirectory = dirname(page);
  const source = readFileSync(page, "utf8");
  const rootPage = routeFile === "app/page.tsx";
  const privatePage = routeFile === "app/analytics/page.tsx";

  if (!rootPage && !privatePage && !source.includes("pageMetadata(")) {
    errors.push(`${routeFile}: indexable pages must use pageMetadata for canonical and social text`);
  }
  if (privatePage && !/robots:\s*\{\s*index:\s*false/.test(source)) {
    errors.push(`${routeFile}: private page must remain noindex`);
  }

  const hasOwnCard = existsSync(join(routeDirectory, "opengraph-image.tsx"));
  const hasExplicitCard = /\bimage:\s*[`"']/.test(source);
  if (!privatePage && !hasOwnCard && !hasExplicitCard) {
    errors.push(`${routeFile}: page needs its own opengraph image or an explicit content-backed image route`);
  }
}

const routeSources = ["app", "components", "content", "lib"].flatMap((directory) =>
  walk(join(webRoot, directory), (path) => /\.(?:ts|tsx|mdx)$/.test(path)),
);
routeSources.push(join(webRoot, "..", "scripts", "generate-approach-docs.mjs"));
const oldRoute = /(["'`(])\/(approaches|engines)(?=\/|["'`?#)])/g;
for (const path of routeSources) {
  if (path === join(webRoot, "next.config.ts")) continue;
  const source = readFileSync(path, "utf8");
  for (const match of source.matchAll(oldRoute)) {
    const line = source.slice(0, match.index).split("\n").length;
    errors.push(`${relative(webRoot, path)}:${line}: old /${match[2]} URL bypasses the canonical route`);
  }
}

const sitemap = readFileSync(join(appRoot, "sitemap.ts"), "utf8");
for (const required of [
  '"/approach"',
  '"/engine"',
  "listAllApproaches()",
  "listDocs()",
  "listLogEntries()",
  "listTechniquePages()",
  "getTheories()",
  "getExperiments()",
  "getResults()",
  "loadLeaderboard()",
  "loadCompetitionLeaderboard(",
]) {
  if (!sitemap.includes(required)) errors.push(`app/sitemap.ts: missing generated sitemap source ${required}`);
}
if (/(["'`(])\/(?:approaches|engines)(?=\/|["'`?#)])/.test(sitemap)) {
  errors.push("app/sitemap.ts: legacy plural routes must not be canonical sitemap entries");
}

for (const artCard of [
  "app/approach/opengraph-image.tsx",
  "app/approach/technique/[technique]/opengraph-image.tsx",
  "app/approach/[family]/[approach]/opengraph-image.tsx",
  "app/engine/opengraph-image.tsx",
  "app/engine/[slug]/opengraph-image.tsx",
  "app/learn/techniques/[slug]/opengraph-image.tsx",
]) {
  const source = readFileSync(join(webRoot, artCard), "utf8");
  if (!source.includes("<TechniqueArt") || !source.includes('mode="static"')) {
    errors.push(`${artCard}: share card must render the registered animation's resting frame`);
  }
}

const socialArt = readFileSync(join(webRoot, "lib/social-art.ts"), "utf8");
const artTokens = new Set(
  walk(join(webRoot, "components/technique-art"), (path) => path.endsWith(".tsx"))
    .flatMap((path) => [...readFileSync(path, "utf8").matchAll(/var\((--[a-z0-9-]+)/g)].map((match) => match[1]))
    .filter((token) => !token.endsWith("-")),
);
for (const token of artTokens) {
  if (!socialArt.includes(`"${token}"`)) {
    errors.push(`lib/social-art.ts: missing literal mapping for animation token ${token}`);
  }
}

if (errors.length > 0) {
  console.error(errors.join("\n"));
  process.exit(1);
}

console.log("Route metadata, sitemap sources, canonical paths and animation stills are covered.");
