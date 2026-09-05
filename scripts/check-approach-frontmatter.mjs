/**
 * Validates the frontmatter of every approach README against the closed
 * vocabulary in docs/agents/approach-page-template.md.
 *
 *   approaches/<family>/README.mdx          kind: family
 *   approaches/<family>/<approach>/README.mdx
 *       title, family, summary            required
 *       status, evidence, reads           required, from the closed lists
 *       kind                              strategy | engine | diagnostic
 *       technique                         required for strategy, from the
 *                                         catalogue; absent otherwise
 *       featured                          optional boolean, strategies only
 *       draft                             optional boolean
 *
 * The technique catalogue here must match TECHNIQUE_ORDER in
 * web/lib/techniques.ts; the script checks that too when the file exists.
 *
 * Run: node scripts/check-approach-frontmatter.mjs
 * Exit 1 on any violation. Only the frontmatter block is read.
 */
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";
import { createRequire } from "node:module";

const ROOT = new URL("..", import.meta.url).pathname;
const APPROACHES = join(ROOT, "approaches");
const TECHNIQUES_TS = join(ROOT, "web", "lib", "techniques.ts");

let matter;
try {
  matter = createRequire(join(ROOT, "web", "package.json"))("gray-matter");
} catch {
  console.error("gray-matter is not installed; run `npm install` inside web/ first.");
  process.exit(1);
}

const STATUS = ["completed", "rejected", "runtime-paused", "preregistered", "support-only", "proposal", "unknown"];
const EVIDENCE = ["ledger-recorded", "task-record only", "repository-verified", "reproduced", "none"];
const READS = ["public", "oracle", "teacher", "diagnostic"];
const KIND = ["strategy", "engine", "diagnostic"];
const TECHNIQUES = [
  "expectimax",
  "heuristic-evaluation",
  "q-learning",
  "n-tuple",
  "nnue",
  "policy-gradient",
  "evolution",
  "mcts",
  "rollout-policy-iteration",
  "oracle-distillation",
  "risk-survival",
  "afterstate",
  "constructive-planning",
  "determinization",
];
const KNOWN_KEYS = new Set([
  "title",
  "family",
  "summary",
  "status",
  "evidence",
  "reads",
  "kind",
  "technique",
  "featured",
  "draft",
  "generated",
]);

const violations = [];
const warnings = [];

function fail(file, message) {
  violations.push(`${file}: ${message}`);
}

function warn(file, message) {
  warnings.push(`${file}: ${message}`);
}

function frontmatterOf(path) {
  const text = readFileSync(path, "utf8");
  if (!text.startsWith("---\n")) return null;
  try {
    return matter(text).data;
  } catch (error) {
    return { $error: String(error.message ?? error) };
  }
}

function expectOneOf(file, data, key, allowed, { required }) {
  const value = data[key];
  if (value === undefined) {
    if (required) fail(file, `missing \`${key}\``);
    return;
  }
  if (typeof value !== "string" || !allowed.includes(value)) {
    fail(file, `\`${key}: ${JSON.stringify(value)}\` is outside {${allowed.join(", ")}}`);
  }
}

function expectString(file, data, key) {
  if (typeof data[key] !== "string" || data[key].trim().length === 0) {
    fail(file, `missing or empty \`${key}\``);
  }
}

function expectBoolean(file, data, key) {
  if (data[key] !== undefined && typeof data[key] !== "boolean") {
    fail(file, `\`${key}\` must be true or false`);
  }
}

function checkFamilyReadme(family, path, file) {
  const data = frontmatterOf(path);
  if (!data) return fail(file, "no frontmatter block");
  if (data.$error) return fail(file, `frontmatter does not parse: ${data.$error}`);
  expectString(file, data, "title");
  expectString(file, data, "summary");
  if (data.kind !== "family") fail(file, `family README must carry \`kind: family\` (found ${JSON.stringify(data.kind)})`);
  expectOneOf(file, data, "status", STATUS, { required: false });
  expectOneOf(file, data, "evidence", EVIDENCE, { required: false });
  expectOneOf(file, data, "reads", READS, { required: false });
  if (data.technique !== undefined) fail(file, "family README must not carry `technique`");
  if (data.featured !== undefined) fail(file, "family README must not carry `featured`");
  for (const key of Object.keys(data)) if (!KNOWN_KEYS.has(key)) warn(file, `unknown key \`${key}\``);
}

function checkApproachReadme(family, slug, path, file) {
  const data = frontmatterOf(path);
  if (!data) return fail(file, "no frontmatter block");
  if (data.$error) return fail(file, `frontmatter does not parse: ${data.$error}`);
  expectString(file, data, "title");
  expectString(file, data, "summary");
  if (data.family !== family) fail(file, `\`family\` must be \`${family}\` (found ${JSON.stringify(data.family)})`);
  expectOneOf(file, data, "status", STATUS, { required: true });
  expectOneOf(file, data, "evidence", EVIDENCE, { required: true });
  expectOneOf(file, data, "reads", READS, { required: true });
  expectOneOf(file, data, "kind", KIND, { required: true });
  if (data.kind === "strategy") {
    expectOneOf(file, data, "technique", TECHNIQUES, { required: true });
  } else if (data.technique !== undefined) {
    fail(file, `\`technique\` is only for strategies (kind is ${JSON.stringify(data.kind)})`);
  }
  expectBoolean(file, data, "featured");
  if (data.featured === true && data.kind !== "strategy") fail(file, "`featured` is only for strategies");
  expectBoolean(file, data, "draft");
  for (const key of Object.keys(data)) if (!KNOWN_KEYS.has(key)) warn(file, `unknown key \`${key}\``);
}

function checkCatalogueSync() {
  if (!existsSync(TECHNIQUES_TS)) return;
  const source = readFileSync(TECHNIQUES_TS, "utf8");
  const block = /TECHNIQUE_ORDER\s*=\s*\[([\s\S]*?)\]\s*as const/.exec(source);
  if (!block) return warn("web/lib/techniques.ts", "could not find TECHNIQUE_ORDER to compare against");
  const listed = [...block[1].matchAll(/"([a-z0-9-]+)"/g)].map((m) => m[1]);
  if (listed.join(",") !== TECHNIQUES.join(",")) {
    fail(
      "web/lib/techniques.ts",
      `TECHNIQUE_ORDER [${listed.join(", ")}] differs from this script's catalogue [${TECHNIQUES.join(", ")}]`,
    );
  }
}

if (!existsSync(APPROACHES)) {
  console.log("no approaches/ directory; nothing to check");
  process.exit(0);
}

let families = 0;
let approaches = 0;
for (const family of readdirSync(APPROACHES).sort()) {
  const familyDir = join(APPROACHES, family);
  if (!statSync(familyDir).isDirectory()) continue;
  const familyReadme = join(familyDir, "README.mdx");
  if (existsSync(familyReadme)) {
    families += 1;
    checkFamilyReadme(family, familyReadme, `approaches/${family}/README.mdx`);
  }
  for (const slug of readdirSync(familyDir).sort()) {
    const dir = join(familyDir, slug);
    if (!statSync(dir).isDirectory()) continue;
    const readme = join(dir, "README.mdx");
    if (!existsSync(readme)) continue;
    approaches += 1;
    checkApproachReadme(family, slug, readme, `approaches/${family}/${slug}/README.mdx`);
  }
}
checkCatalogueSync();

for (const line of warnings) console.log(`warning: ${line}`);
for (const line of violations) console.error(`violation: ${line}`);
console.log(
  `${families} family README(s), ${approaches} approach README(s): ${violations.length} violation(s), ${warnings.length} warning(s)`,
);
process.exit(violations.length > 0 ? 1 : 0);
