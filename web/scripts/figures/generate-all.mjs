#!/usr/bin/env node
/**
 * Regenerates every figure spec under web/content/figures/.
 *   node web/scripts/figures/generate-all.mjs
 */
import { readdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { generate } from "./generate.mjs";

const dir = resolve(new URL(".", import.meta.url).pathname, "..", "..", "content", "figures");
const specs = readdirSync(dir).filter((f) => f.endsWith(".json")).sort();
let failed = 0;
for (const spec of specs) {
  try {
    console.log(`wrote ${generate(join(dir, spec))}`);
  } catch (error) {
    failed += 1;
    console.error(String(error.message ?? error));
  }
}
if (failed) process.exit(1);
