/**
 * Snapshots one ntuple-scale run into web/content/figures/ntuple-scale/<run-id>.json
 * so the approach page can chart it on a checkout that has no runs/ directory.
 *
 * The source is the run's artifact directory, runs/<run-id>/ntuple-scale/,
 * which is not committed. The script first refreshes analysis.json with the
 * approach's own analyze.py (the run's summary artifact), then copies the
 * gate lines, every pilot arm's and the main run's progress rows and
 * validation points, the frozen candidate's hash, and the held-out screen
 * arms, per-game rows and contrasts. Nothing is computed except the per-seed
 * paired differences under `screen.paired[*].perSeed`, listed under
 * `derived`.
 *
 *   cd web && node --experimental-strip-types scripts/extract-ntuple-scale.ts --run RUN-…
 *
 * If the run directory is absent the script writes nothing and exits 0.
 */
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import type { PairedContrast, ScreenArm, ScreenGame } from "../lib/charts/evolution.ts";
import { SNAPSHOT_FORMAT, type ChunkRow, type NTupleSnapshot, type TrainingRun, type ValidationPoint } from "../lib/charts/ntuple-scale.ts";

const args = process.argv.slice(2);
function arg(name: string, fallback?: string): string | undefined {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : fallback;
}

const runId = arg("--run");
if (!runId || !/^RUN-[A-Za-z0-9-]+$/.test(runId)) {
  console.error("usage: extract-ntuple-scale.ts --run RUN-<id> [--root <repo>] [--skip-analyze]");
  process.exit(2);
}
const root = arg("--root", join(import.meta.dirname, "..", ".."))!;
const runDir = join(root, "runs", runId, "ntuple-scale");
if (!existsSync(runDir)) {
  console.log(`${relative(root, runDir)} is not present in this checkout; nothing written.`);
  process.exit(0);
}

const approachDir = join(root, "approaches", "ntuple-rl", "ntuple-scale");
if (!args.includes("--skip-analyze")) {
  const analyze = spawnSync("/usr/bin/python3", [join(approachDir, "scripts", "analyze.py"), "--run", runId, "--root", root], { stdio: ["ignore", "ignore", "inherit"] });
  if (analyze.status !== 0) console.error("warning: analyze.py did not run cleanly; using the existing analysis.json if present");
}

const sources: string[] = [];
function readJson<T>(path: string): T | null {
  if (!existsSync(path)) return null;
  sources.push(relative(root, path));
  return JSON.parse(readFileSync(path, "utf8")) as T;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Analysis = Record<string, any>;
const analysis = readJson<Analysis>(join(runDir, "analysis.json"));
if (!analysis) {
  console.error(`no analysis.json under ${relative(root, runDir)}; run analyze.py first`);
  process.exit(1);
}

const runRecord = readJson<{ experimentId?: string; lifecycle?: string }>(join(root, "research", "runs", `${runId}.json`));
const experimentId = runRecord?.experimentId ?? null;
const experiment = experimentId ? readJson<{ theoryIds?: string[] }>(join(root, "research", "experiments", `${experimentId}.json`)) : null;

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function trainingRun(name: string, summary: any, dir: string): TrainingRun {
  sources.push(relative(root, join(dir, "progress.jsonl")));
  if (summary.config) sources.push(relative(root, join(dir, "config.json")));
  const chunks: ChunkRow[] = (summary.curve ?? []).map((c: Record<string, unknown>) => ({
    chunk: c.chunk as number,
    movesTotal: c.movesTotal as number,
    gamesTotal: c.gamesTotal as number,
    wallSeconds: c.wallSeconds as number,
    movesPerSecond: c.movesPerSecond as number,
    trainMeanScore: c.trainMeanScore as number,
    trainMeanMoves: c.trainMeanMoves as number,
    trainMaxScore: c.trainMaxScore as number,
    trainClearsPerMove: c.trainClearsPerMove as number,
    trainRevealsPerMove: c.trainRevealsPerMove as number,
    meanAbsDelta: c.meanAbsDelta as number,
    meanBeta: c.meanBeta as number,
    seedWraps: (c.seedWraps as number | undefined) ?? null,
    quickDirectMean: c.quick && typeof (c.quick as Record<string, unknown>).directMean === "number" ? ((c.quick as Record<string, unknown>).directMean as number) : null,
  }));
  const touchedByMoves = new Map<number, number>();
  for (const c of summary.curve ?? []) {
    if (c.validation && typeof c.validation.touchedEntries === "number") touchedByMoves.set(c.validation.moves, c.validation.touchedEntries);
  }
  const bestMoves = summary.best?.moves ?? null;
  const validations: ValidationPoint[] = (summary.validations ?? []).map((v: Analysis) => {
    sources.push(relative(root, join(dir, v.artifact)));
    const d = v.pairedD3VsFair;
    const one = v.paired1plyVsFair;
    return {
      movesTrained: v.movesTrained,
      artifact: v.artifact,
      ntupleD3Mean: v.arms["ntuple-d3s7"].meanScore,
      ntupleD3MovesMean: v.arms["ntuple-d3s7"].meanMoves,
      directMean: v.arms["ntuple-1ply"]?.meanScore ?? null,
      directMovesMean: v.arms["ntuple-1ply"]?.meanMoves ?? null,
      fairD3Mean: v.arms["fair-d3s7"].meanScore,
      fairD3MovesMean: v.arms["fair-d3s7"].meanMoves,
      pairedDeltaD3: d.meanDelta,
      bootstrapLower95: d.bootstrapLower95,
      bootstrapUpper95: d.bootstrapUpper95,
      studentTLower95: d.studentTLower95,
      detectionFloor: d.detectionFloor,
      wtl: [d.wins, d.ties, d.losses],
      halves: [d.firstHalfMeanDelta, d.secondHalfMeanDelta],
      pairedDeltaDirect: one ? one.meanDelta : null,
      touchedEntries: touchedByMoves.get(v.movesTrained) ?? null,
      isBest: bestMoves !== null && v.movesTrained === bestMoves,
      plateau: v.plateau
        ? { points: v.plateau.points, window: v.plateau.window, recentMean: v.plateau.recentMean, previousMean: v.plateau.previousMean, stop: Boolean(v.plateau.stop) }
        : null,
    };
  });
  const run: TrainingRun = {
    name,
    layout: summary.config?.layout ?? summary.layout ?? "",
    alpha: summary.config?.alpha ?? summary.alpha ?? 0,
    entries: summary.config?.entries ?? 0,
    activePerState: summary.config?.activePerState ?? 0,
    initFrom: summary.config?.initFrom ?? null,
    validateGames: summary.validateGames ?? summary.config?.validateGames ?? 64,
    movesTotal: summary.movesTotal,
    gamesTotal: summary.gamesTotal,
    wallSeconds: summary.wallSeconds,
    meanMovesPerSecond: summary.meanMovesPerSecond ?? null,
    done: Boolean(summary.done),
    chunks,
    validations,
    best: summary.best ?? null,
    finalMargin: summary.finalMargin ?? null,
    bestMargin: summary.bestMargin ?? null,
    anyPositiveMargin: Boolean(summary.anyPositiveMargin),
    illegalDecisions: summary.artifactIntegrity?.illegalDecisions ?? 0,
    incompleteDecisions: summary.artifactIntegrity?.incompleteDecisions ?? 0,
    stop: summary.stop
      ? {
          reason: summary.stop.reason,
          movesTotal: summary.stop.movesTotal,
          validationPoints: summary.stop.validationPoints,
          plateauWindow: summary.stop.plateauWindow,
          recentWindowMean: summary.stop.recentWindowMean ?? null,
          previousWindowMean: summary.stop.previousWindowMean ?? null,
          bestMargin: summary.stop.bestMargin ?? null,
        }
      : null,
  };
  if (summary.stop) sources.push(relative(root, join(dir, "stop.json")));
  return run;
}

/* ---- gates ------------------------------------------------------------- */
const gates: NTupleSnapshot["gates"] = analysis.gates ? { passed: Boolean(analysis.gates.passed), gates: analysis.gates.gates } : null;
if (analysis.gates) sources.push(relative(root, join(runDir, "gates.log")));

/* ---- pilot ------------------------------------------------------------- */
let pilot: NTupleSnapshot["pilot"] = null;
if (analysis.pilot) {
  const arms: Record<string, TrainingRun> = {};
  for (const [name, summary] of Object.entries(analysis.pilot.arms as Record<string, Analysis>)) {
    arms[name] = trainingRun(name, summary, join(runDir, "pilot", name));
  }
  if (analysis.pilot.selection) sources.push(relative(root, join(runDir, "pilot", "selection.json")));
  pilot = { arms, selection: analysis.pilot.selection ?? null, rule: analysis.pilot.rule };
}

/* ---- smoke ------------------------------------------------------------- */
const smoke: NTupleSnapshot["smoke"] = analysis.smoke ? trainingRun("smoke", analysis.smoke, join(runDir, "smoke")) : null;

/* ---- main -------------------------------------------------------------- */
const main: NTupleSnapshot["main"] = analysis.main ? trainingRun("main", analysis.main, join(runDir, "main")) : null;

/* ---- freeze ------------------------------------------------------------ */
let freeze: NTupleSnapshot["freeze"] = null;
const hashPath = join(runDir, "main", "candidate-weights.sha256");
if (existsSync(hashPath)) {
  sources.push(relative(root, hashPath));
  freeze = { candidateSha256: readFileSync(hashPath, "utf8").split(/\s+/)[0] || null };
  const priorPath = join(runDir, "main", "prior-weights.sha256");
  if (existsSync(priorPath)) {
    sources.push(relative(root, priorPath));
    freeze.priorSha256 = readFileSync(priorPath, "utf8").split(/\s+/)[0] || null;
  }
  for (const name of ["control", "zeroed", "classmean"] as const) {
    const path = join(runDir, "main", `${name}-weights.sha256`);
    if (existsSync(path)) {
      sources.push(relative(root, path));
      freeze[`${name}Sha256`] = readFileSync(path, "utf8").split(/\s+/)[0] || null;
    }
  }
}

/* ---- screen ------------------------------------------------------------ */
const derived: string[] = [];
let screen: NTupleSnapshot["screen"] = null;
if (analysis.screen) {
  const heldout = readJson<{ individuals: { name: string; games: { seedHex: string; score: number; moves: number; censored: boolean }[] }[] }>(join(runDir, "screen", "heldout.json"));
  const gamesByArm = new Map<string, ScreenGame[]>();
  for (const individual of heldout?.individuals ?? []) {
    gamesByArm.set(individual.name, individual.games.map((g) => ({ seedHex: g.seedHex, score: g.score, moves: g.moves, censored: g.censored })));
  }
  const arms: Record<string, ScreenArm> = {};
  for (const [name, a] of Object.entries(analysis.screen.arms as Record<string, Analysis>)) {
    arms[name] = {
      games: a.games,
      mean: a.meanScore,
      median: a.medianScore,
      q25: a.q25Score,
      max: a.maxScore,
      movesMean: a.meanMoves,
      clearsPerMove: a.numberedClearsPerMove,
      revealsPerMove: a.coverRevealsPerMove,
      censored: a.censoredGames,
      illegal: a.illegalDecisions,
      incomplete: a.incompleteDecisions,
      wallSeconds: a.meanWallSecondsPerGame,
      perGame: gamesByArm.get(name) ?? [],
    };
  }
  const paired: Record<string, PairedContrast> = {};
  for (const [name, contrast] of Object.entries(analysis.screen.contrasts as Record<string, Analysis>)) {
    const [candidateArm, referenceArm] = name.split("-vs-");
    const s = contrast.score;
    const reference = new Map((gamesByArm.get(referenceArm) ?? []).map((g) => [g.seedHex, g.score]));
    const perSeed = (gamesByArm.get(candidateArm) ?? []).filter((g) => reference.has(g.seedHex)).map((g) => ({ seedHex: g.seedHex, delta: g.score - reference.get(g.seedHex)! }));
    if (perSeed.length) derived.push(`screen.paired.${name}.perSeed = ${candidateArm} score minus ${referenceArm} score on the same seed`);
    paired[name] = {
      candidateArm,
      referenceArm,
      meanDelta: s.meanDelta,
      bootstrapLower95: s.bootstrapLower95,
      bootstrapUpper95: s.bootstrapUpper95,
      studentTLower95: s.studentTLower95,
      pairedSd: s.pairedSd,
      detectionFloor: s.detectionFloor,
      wtl: [s.wins, s.ties, s.losses],
      halves: [s.firstHalfMeanDelta, s.secondHalfMeanDelta],
      q25Delta: s.q25Delta,
      movesDelta: contrast.moves.meanDelta,
      perSeed,
    };
    const report = join(runDir, "screen", `compare-${name}.json`);
    if (existsSync(report)) sources.push(relative(root, report));
  }
  const primaryContrast: string = analysis.screen.primaryContrast ?? "candidate-d3s7-vs-fair-d3s7";
  const primary = paired[primaryContrast];
  const scale = analysis.screen.scale;
  const reading = (r: Analysis) => ({
    meanDelta: r.meanDelta,
    bootstrapLower95: r.bootstrapLower95,
    bootstrapUpper95: r.bootstrapUpper95,
    studentTLower95: r.studentTLower95,
    detectionFloor: r.detectionFloor,
    wtl: [r.wins, r.ties, r.losses] as [number, number, number],
    halves: [r.firstHalfMeanDelta, r.secondHalfMeanDelta] as [number, number],
  });
  const depthRaw = analysis.screen.depth;
  const depth = depthRaw
    ? {
        primary: depthRaw.primary as string,
        tablesStep: reading(depthRaw.tablesStep),
        fairStep: depthRaw.fairStep ? reading(depthRaw.fairStep) : null,
        persistence: depthRaw.persistence ? { checks: depthRaw.persistence.checks, allPassed: depthRaw.persistence.passed } : null,
        interaction: { ...reading(depthRaw.interaction), verdict: depthRaw.interaction.verdict },
      }
    : null;
  if (depth) derived.push("screen.depth.interaction = per game, (prior-d4s7 minus prior-d3s7) minus (fair-d4s7 minus fair-d3s7), bootstrapped by analyze.py");
  const fillRaw = analysis.screen.fill;
  const fillReading = (r: Analysis | null) =>
    r
      ? {
          ...reading(r),
          contrast: r.contrast as string,
          verdict: r.verdict as "supported" | "refuted" | "inconclusive",
          candidateQ25: r.candidateQ25 as number,
          referenceQ25: r.referenceQ25 as number,
          ...(r.checks ? { checks: r.checks, allPassed: Boolean(r.passed) } : {}),
        }
      : null;
  const fillMap = (m: Record<string, Analysis> | null | undefined) => Object.fromEntries(Object.entries(m ?? {}).map(([k, v]) => [k, fillReading(v)!]));
  const fill = fillRaw
    ? {
        primary: fillRaw.primary as string,
        conditioning: fillReading(fillRaw.conditioning),
        continuation: fillReading(fillRaw.continuation),
        zeroed: fillReading(fillRaw.zeroed),
        classmean: fillReading(fillRaw.classmean),
        fillDepth4: fillReading(fillRaw.fillDepth4),
        zeroedDepth4: fillReading(fillRaw.zeroedDepth4),
        depthSteps: fillMap(fillRaw.depthSteps),
        direct: fillMap(fillRaw.direct),
        replicationOfPrior: fillReading(fillRaw.replicationOfPrior),
        theory: fillRaw.theory as Record<string, boolean | null>,
      }
    : null;
  screen = {
    config: analysis.screen.config,
    seedStartHex: analysis.screen.seedStartHex,
    arms,
    paired,
    primaryContrast,
    depth,
    fill,
    gate: analysis.screen.gate && primary
      ? { checks: analysis.screen.gate.checks, allPassed: analysis.screen.gate.passed, meanDelta: primary.meanDelta, pairedSd: primary.pairedSd, detectionFloor: primary.detectionFloor, wtl: primary.wtl }
      : null,
    replication: analysis.screen.replication ? { checks: analysis.screen.replication.checks, allPassed: analysis.screen.replication.passed } : null,
    scale: scale
      ? { verdict: scale.verdict, meanDelta: scale.meanDelta, bootstrapLower95: scale.bootstrapLower95, bootstrapUpper95: scale.bootstrapUpper95, studentTLower95: scale.studentTLower95, detectionFloor: scale.detectionFloor, wtl: [scale.wins, scale.ties, scale.losses] }
      : null,
  };
}

const snapshot: NTupleSnapshot = {
  format: SNAPSHOT_FORMAT,
  runId,
  experimentId,
  theoryId: experiment?.theoryIds?.[0] ?? null,
  runLifecycle: runRecord?.lifecycle ?? null,
  capturedAt: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
  sources: [...new Set(sources)],
  derived,
  gates,
  gatesHgt5: analysis.gatesHgt5 ? { passed: Boolean(analysis.gatesHgt5.passed), gates: analysis.gatesHgt5.gates } : null,
  frozenGates: analysis.frozenGates ?? null,
  edits: analysis.edits ? { entries: analysis.edits.entries, optimisticStart: analysis.edits.optimisticStart, untouchedEntries: analysis.edits.untouchedEntries, touchedEntries: analysis.edits.touchedEntries, classesWithNoTouchedEntry: analysis.edits.classesWithNoTouchedEntry, outputs: analysis.edits.outputs } : null,
  smoke,
  pilot,
  main,
  freeze,
  screen,
  rusage: analysis.rusage ?? null,
};
if (analysis.gatesHgt5) sources.push(relative(root, join(runDir, "gates-hgt5.log")));
if (analysis.edits) sources.push(relative(root, join(runDir, "main", "edits.json")));
for (const name of Object.keys(analysis.frozenGates ?? {})) sources.push(relative(root, join(runDir, "main", `gates-${name}.log`)));
snapshot.sources = [...new Set(sources)];

const outDir = join(import.meta.dirname, "..", "content", "figures", "ntuple-scale");
mkdirSync(outDir, { recursive: true });
const outPath = join(outDir, `${runId}.json`);
writeFileSync(outPath, `${JSON.stringify(snapshot, null, 2)}\n`);
console.log(
  `wrote ${relative(root, outPath)}: gates ${gates ? (gates.passed ? "passed" : "failed") : "absent"}, smoke ${smoke ? `${smoke.movesTotal} moves` : "absent"}, pilot ${pilot ? `${Object.keys(pilot.arms).length} arms` : "absent"}, main ${main ? `${main.movesTotal} moves, ${main.validations.length} validation points` : "absent"}, freeze ${freeze ? "present" : "absent"}, screen ${screen ? "present" : "absent"}`,
);
