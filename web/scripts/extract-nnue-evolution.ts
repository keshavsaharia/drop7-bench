/**
 * Snapshots one nnue-evolution run into web/content/figures/nnue-evolution/<run-id>.json
 * so the approach page can chart it on a checkout that has no runs/ directory.
 *
 * The source is the run's artifact directory, runs/<run-id>/nnue-evolution/,
 * which is not committed. The script first refreshes analysis.json with the
 * approach's own analyze.py (the run's summary artifact), then copies:
 *   - the corpus summary and every completed teacher game's row (part files);
 *   - the supervised report, epoch curve and ordering probe;
 *   - every generation's summary line and per-candidate fitness (progress.jsonl);
 *   - the elite re-selection;
 *   - the held-out screen arms, per-game rows and the compare.py contrasts.
 * Nothing is computed except the per-seed paired differences under
 * `paired[*].perSeed`, which subtract two recorded scores for the same seed
 * and are listed under `derived` in the snapshot.
 *
 *   cd web && node --experimental-strip-types scripts/extract-nnue-evolution.ts --run RUN-…
 *
 * If the run directory is absent the script writes nothing and exits 0.
 */
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import {
  SNAPSHOT_FORMAT,
  type CorpusGame,
  type EvolutionSnapshot,
  type Generation,
  type PairedContrast,
  type ScreenArm,
  type ScreenGame,
} from "../lib/charts/evolution.ts";

const args = process.argv.slice(2);
function arg(name: string, fallback?: string): string | undefined {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : fallback;
}

const runId = arg("--run");
if (!runId || !/^RUN-[A-Za-z0-9-]+$/.test(runId)) {
  console.error("usage: extract-nnue-evolution.ts --run RUN-<id> [--root <repo>] [--skip-analyze]");
  process.exit(2);
}
const root = arg("--root", join(import.meta.dirname, "..", ".."))!;
const runDir = join(root, "runs", runId, "nnue-evolution");
if (!existsSync(runDir)) {
  console.log(`${relative(root, runDir)} is not present in this checkout; nothing written.`);
  process.exit(0);
}

const approachDir = join(root, "approaches", "lifetime-objective", "nnue-evolution");
if (!args.includes("--skip-analyze")) {
  const analyze = spawnSync("python3", [join(approachDir, "scripts", "analyze.py"), "--run", runId, "--root", root], { stdio: ["ignore", "ignore", "inherit"] });
  if (analyze.status !== 0) console.error("warning: analyze.py did not run cleanly; using the existing analysis.json if present");
}

const sources: string[] = [];
function readJson<T>(path: string): T | null {
  if (!existsSync(path)) return null;
  sources.push(relative(root, path));
  return JSON.parse(readFileSync(path, "utf8")) as T;
}
function readJsonl<T>(path: string): T[] | null {
  if (!existsSync(path)) return null;
  sources.push(relative(root, path));
  return readFileSync(path, "utf8")
    .split("\n")
    .filter((line) => line.trim())
    .map((line) => JSON.parse(line) as T);
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Analysis = Record<string, any>;
const analysis = readJson<Analysis>(join(runDir, "analysis.json"));
if (!analysis) {
  console.error(`no analysis.json under ${relative(root, runDir)}; run analyze.py first`);
  process.exit(1);
}

/* ---- records ---------------------------------------------------------- */
const runRecord = readJson<{ experimentId?: string; lifecycle?: string }>(join(root, "research", "runs", `${runId}.json`));
const experimentId = runRecord?.experimentId ?? null;
const experiment = experimentId ? readJson<{ theoryIds?: string[] }>(join(root, "research", "experiments", `${experimentId}.json`)) : null;

/* ---- stage A ---------------------------------------------------------- */
let corpus: EvolutionSnapshot["corpus"] = null;
if (analysis.corpus) {
  const partsDir = join(runDir, "corpus", "parts");
  const perGame: CorpusGame[] = [];
  if (existsSync(partsDir)) {
    sources.push(relative(root, partsDir));
    for (const file of readdirSync(partsDir).filter((f) => f.endsWith(".jsonl")).sort()) {
      for (const line of readFileSync(join(partsDir, file), "utf8").split("\n")) {
        if (!line.trim()) continue;
        const row = JSON.parse(line) as { type: string; seed: string; score: number; moves: number; wallSeconds: number; censored: boolean };
        if (row.type === "game") perGame.push({ seed: row.seed, score: row.score, moves: row.moves, wallSeconds: row.wallSeconds, censored: row.censored });
      }
    }
  }
  // Completion order is the order in the corpus event log when it exists.
  const log = join(runDir, "corpus.err");
  if (existsSync(log)) {
    sources.push(relative(root, log));
    const order = new Map<string, number>();
    for (const line of readFileSync(log, "utf8").split("\n")) {
      const match = line.match(/^\[(\d+)\/\d+\] seed (0x[0-9a-f]+)/);
      if (match) order.set(match[2], Number(match[1]));
    }
    perGame.sort((a, b) => (order.get(a.seed) ?? Infinity) - (order.get(b.seed) ?? Infinity));
  }
  const c = analysis.corpus;
  corpus = {
    games: c.games,
    roots: c.roots,
    censoredGames: c.censoredGames,
    score: c.score,
    moves: c.moves,
    wallSecondsPerGame: c.wallSecondsPerGame,
    secondsPerRoot: c.secondsPerRoot ?? null,
    rootsPerGame: c.rootsPerGame ?? null,
    legalColumnsPerRoot: c.legalColumnsPerRoot ?? null,
    teacherSiblingSpreadPoints: c.teacherSiblingSpreadPoints ?? null,
    perGame,
  };
}

/* ---- stage B ---------------------------------------------------------- */
const pretrain: EvolutionSnapshot["pretrain"] = analysis.pretrain
  ? { report: analysis.pretrain.report, epochs: analysis.pretrain.epochs ?? [], probe: analysis.pretrain.probe ?? null }
  : null;

/* ---- stage C ---------------------------------------------------------- */
let evolve: EvolutionSnapshot["evolve"] = null;
if (analysis.evolve) {
  const progress = readJsonl<{ generation: number; blockStart: string; best: number; mean: number; controlFair: number; controlInit: number; fitness: number[] }>(
    join(runDir, "evolve", "progress.jsonl"),
  ) ?? [];
  const byGeneration = new Map(progress.map((row) => [row.generation, row]));
  const generations: Generation[] = analysis.evolve.generations.map((g: Generation) => ({
    generation: g.generation,
    blockStart: g.blockStart,
    best: g.best,
    mean: g.mean,
    top4Mean: g.top4Mean,
    controlFair: g.controlFair,
    controlInit: g.controlInit,
    fitness: byGeneration.get(g.generation)?.fitness ?? [],
  }));
  evolve = {
    config: analysis.evolve.config ?? null,
    generationsCompleted: analysis.evolve.generationsCompleted,
    generations,
    trainingSignalCheck: analysis.evolve.trainingSignalCheck,
    artifactIntegrity: analysis.evolve.artifactIntegrity,
  };
}

const select: EvolutionSnapshot["select"] = analysis.select
  ? { selection: analysis.select.selection ?? null, finalistMeans: analysis.select.finalistMeans ?? [], candidateSha256: analysis.select.candidateSha256 ?? null }
  : null;

/* ---- stage D ---------------------------------------------------------- */
const derived: string[] = [];
let screen: EvolutionSnapshot["screen"] = null;
if (analysis.screen) {
  const heldout = readJson<{ individuals: { name: string; games: { seedHex: string; score: number; moves: number; censored: boolean }[] }[] }>(
    join(runDir, "screen", "heldout.json"),
  );
  const gamesByArm = new Map<string, ScreenGame[]>();
  for (const individual of heldout?.individuals ?? []) {
    gamesByArm.set(
      individual.name,
      individual.games.map((g) => ({ seedHex: g.seedHex, score: g.score, moves: g.moves, censored: g.censored })),
    );
  }
  const arms: Record<string, ScreenArm> = {};
  for (const [name, arm] of Object.entries(analysis.screen.arms as Record<string, Omit<ScreenArm, "perGame">>)) {
    arms[name] = { ...arm, perGame: gamesByArm.get(name) ?? [] };
  }
  const pairs: Record<string, [string, string]> = {
    "candidate-vs-fair-d3s7": ["candidate", "fair-d3s7"],
    "init-vs-fair-d3s7": ["init-d3s7", "fair-d3s7"],
    "candidate-vs-init": ["candidate", "init-d3s7"],
    "fair-d4s7-vs-fair-d3s7": ["fair-d4s7", "fair-d3s7"],
  };
  const paired: Record<string, PairedContrast> = {};
  for (const [name, contrast] of Object.entries(analysis.screen.paired as Record<string, Omit<PairedContrast, "perSeed" | "candidateArm" | "referenceArm">>)) {
    const [candidateArm, referenceArm] = pairs[name] ?? [name, name];
    const reference = new Map((gamesByArm.get(referenceArm) ?? []).map((g) => [g.seedHex, g.score]));
    const perSeed = (gamesByArm.get(candidateArm) ?? [])
      .filter((g) => reference.has(g.seedHex))
      .map((g) => ({ seedHex: g.seedHex, delta: g.score - reference.get(g.seedHex)! }));
    if (perSeed.length) derived.push(`screen.paired.${name}.perSeed = ${candidateArm} score minus ${referenceArm} score on the same seed`);
    paired[name] = { candidateArm, referenceArm, ...contrast, perSeed };
    const report = join(runDir, "screen", `compare-${name}.json`);
    if (existsSync(report)) sources.push(relative(root, report));
  }
  screen = { config: analysis.screen.config, seedStartHex: analysis.screen.seedStartHex, arms, paired, gate: analysis.screen.gate ?? null };
}

const snapshot: EvolutionSnapshot = {
  format: SNAPSHOT_FORMAT,
  runId,
  experimentId,
  theoryId: experiment?.theoryIds?.[0] ?? null,
  runLifecycle: runRecord?.lifecycle ?? null,
  capturedAt: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
  sources: [...new Set(sources)],
  derived,
  corpus,
  pretrain,
  evolve,
  select,
  screen,
  rusage: analysis.rusage ?? null,
};

const outDir = join(import.meta.dirname, "..", "content", "figures", "nnue-evolution");
mkdirSync(outDir, { recursive: true });
const outPath = join(outDir, `${runId}.json`);
writeFileSync(outPath, `${JSON.stringify(snapshot, null, 2)}\n`);
console.log(
  `wrote ${relative(root, outPath)}: corpus ${corpus ? `${corpus.games} games` : "absent"}, pretrain ${pretrain ? `best epoch ${pretrain.report.bestEpoch}` : "absent"}, evolve ${evolve ? `${evolve.generationsCompleted} generations` : "absent"}, select ${select ? "present" : "absent"}, screen ${screen ? "present" : "absent"}`,
);
