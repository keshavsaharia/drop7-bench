#!/usr/bin/env node
/**
 * Compiles MDX files the way the console does (next-mdx-remote with blockJS
 * disabled, remark-gfm) and reports any that fail. Usage:
 *   node web/scripts/check-mdx.mjs <file-or-directory> [...]
 * Exit code 1 if any file fails.
 */
import { createRequire } from "node:module";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, resolve } from "node:path";

const webDir = resolve(new URL(".", import.meta.url).pathname, "..");
const require = createRequire(join(webDir, "package.json"));
const { compile } = await import(require.resolve("@mdx-js/mdx"));
const matter = require("gray-matter");
const remarkGfm = (await import(require.resolve("remark-gfm"))).default;

const KNOWN = new Set([
  "Board","BoardCompare","Callout","Disc","Drop7Intro","Stat",
  "BitboardScan","RunLengthLookup","CascadeAnimation","MovePipeline","TreeShape","AttributionBar","SpeedupBars","PackedKey","GateLadder","BatchLayout","LeverList","Num","MiniBoard","PackedColumn",
  "RulesScenario","DropPhysics","RunCounter","RiseClock","ScoreCurve","PlayerView","LegalColumns","DiscLegend",
  "RootAndChoices","ChanceNode","ChanceStyles","TreeGrowth","SiblingTrap","TreeExplorerSection",
  "LeafXray","LeafTerms","BoardLookalikes","GameTimeline","FlowBalance","ScoreSources","ScoreStrip","CohortScale",
  "OracleSplit","TeacherStudentFlow","NTupleWindows","ValueNetShape","PolicyNetShape","MctsTreeGrowth",
  "EvidenceLabel","ExperimentSummary","ResultSummary","TechnicalDetails","TheorySummary",
  "GameTreeFigure","Drop7Board","Drop7Game","Diagram","Figure","ArmTable","DeadEnd","Direction","Finding","LogQuote","Timeline",
  "EvolutionStatus","CorpusFigure","PretrainFigure","EvolutionFigure","SwarmFigure","ScreenFigure","ScreenGateTable","EvolutionPipeline","GenerationLoop","NnueSketch","PairedSeedsSketch",
]);

/**
 * Components registered for MDX outside the static list above are discovered
 * from their source files, so a new primer figure or accordion variant does
 * not need a hand edit here. Only `export function Name` / `export const Name`
 * with a capitalised name count.
 */
function exportedComponentNames(relativeFiles) {
  const names = new Set();
  for (const rel of relativeFiles) {
    const path = join(webDir, rel);
    let entries = [];
    try {
      entries = statSync(path).isDirectory()
        ? readdirSync(path).filter((f) => f.endsWith(".tsx")).map((f) => join(path, f))
        : [path];
    } catch {
      continue;
    }
    for (const file of entries) {
      const source = readFileSync(file, "utf8");
      for (const match of source.matchAll(/export (?:function|const) ([A-Z][A-Za-z0-9]*)/g)) names.add(match[1]);
    }
  }
  return names;
}
for (const name of exportedComponentNames([
  "components/primers",
  "components/Reveal.tsx",
  "components/technique-art/TechniqueArt.tsx",
  "components/StatTile.tsx",
  "components/StatRow.tsx",
  "components/EvidenceStrip.tsx",
  "components/FigureGrid.tsx",
  "components/Engines.tsx",
])) KNOWN.add(name);

function* walk(path) {
  const st = statSync(path);
  if (st.isDirectory()) {
    for (const entry of readdirSync(path)) {
      if (entry === "node_modules" || entry.startsWith(".")) continue;
      yield* walk(join(path, entry));
    }
  } else if (path.endsWith(".mdx")) {
    yield path;
  }
}

let failures = 0;
let count = 0;
for (const arg of process.argv.slice(2)) {
  for (const file of walk(resolve(arg))) {
    count += 1;
    const raw = readFileSync(file, "utf8");
    const { content, data } = matter(raw);
    try {
      const out = String(await compile(content, { outputFormat: "function-body", remarkPlugins: [remarkGfm] }));
      const used = [...out.matchAll(/_jsx\(([A-Z][A-Za-z0-9]*)/g)].map((m) => m[1]);
      const unknown = [...new Set(used)].filter((name) => !KNOWN.has(name) && name !== "Fragment" && name !== "MDXLayout");
      const problems = [];
      if (unknown.length) problems.push(`unknown components: ${unknown.join(", ")}`);
      if (!data.title) problems.push("frontmatter missing title");
      if (!data.summary && !file.includes("/research/")) problems.push("frontmatter missing summary");
      if (problems.length) { failures += 1; console.log(`WARN ${file}: ${problems.join("; ")}`); }
    } catch (error) {
      failures += 1;
      console.log(`FAIL ${file}: ${error.message.split("\n")[0]}`);
    }
  }
}
console.log(`${count} MDX files checked, ${failures} with problems`);
process.exit(failures ? 1 : 0);
