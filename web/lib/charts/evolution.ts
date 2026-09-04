/**
 * The committed snapshot of one nnue-evolution run, written by
 * web/scripts/extract-nnue-evolution.ts from the run's artifacts under
 * runs/<run-id>/nnue-evolution/ and read by web/components/Evolution.tsx.
 *
 * Every number in a snapshot is copied from a run artifact (analysis.json,
 * corpus part files, progress.jsonl, heldout.json, compare-*.json). The only
 * values the extractor derives itself are the per-seed paired differences in
 * `PairedContrast.perSeed`, which subtract two recorded scores for the same
 * seed; the snapshot says so in `derived`. Shared by server and client code,
 * so this module imports nothing from Node.
 */

export const SNAPSHOT_FORMAT = "drop7-nnue-evolution-snapshot-v1";

export interface Dist {
  n: number;
  mean: number;
  sd: number;
  min: number;
  q25: number;
  median: number;
  q75: number;
  max: number;
}

export interface CorpusGame {
  seed: string;
  score: number;
  moves: number;
  wallSeconds: number;
  censored: boolean;
}

export interface CorpusStage {
  games: number;
  roots: number;
  censoredGames: number;
  score: Dist;
  moves: Dist;
  wallSecondsPerGame: Dist;
  secondsPerRoot: number | null;
  rootsPerGame: number | null;
  legalColumnsPerRoot: Dist | null;
  teacherSiblingSpreadPoints: Dist | null;
  /** One row per completed teacher game, in completion order. */
  perGame: CorpusGame[];
}

export interface PretrainEpoch {
  epoch: number;
  trainHuber: number;
  valHuber: number;
  valPearson: number;
}

export interface PretrainStage {
  report: {
    roots: number;
    games: number;
    trainRoots: number;
    valRoots: number;
    epochs: number;
    batch: number;
    lr: number;
    seed: string;
    bestEpoch: number;
    bestValHuber: number;
  };
  epochs: PretrainEpoch[];
  probe: { probeRoots: number; top1: number; meanTeacherValueRegret: number } | null;
}

export interface Generation {
  generation: number;
  blockStart: string;
  best: number;
  mean: number;
  top4Mean: number;
  controlFair: number;
  controlInit: number;
  /** Continuation runs: the first run's frozen candidate playing the same block. */
  controlBaseline?: number | null;
  /** Relative mutation sigma used to produce the next generation (continuation runs). */
  sigmaRel?: number | null;
  /** Mean score of every candidate in the population, in population order. */
  fitness: number[];
}

export interface PlateauCheck {
  generation: number;
  window: number;
  slopePerGeneration: number;
  standardError: number;
  lowerBound95: number;
  windowMeanFirstHalf: number;
  windowMeanSecondHalf: number;
  stop: boolean;
}

export interface TrainingSignalCheck {
  rule: string;
  window: number[];
  generationsMeanAboveFair: number;
  generationsTop4AboveFair: number;
  generationsBestAboveFair: number;
  generationsInitAboveFair: number;
  meanMarginLast10: number | null;
  top4MarginLast10: number | null;
  passed: boolean | null;
}

export interface EvolveStage {
  config: Record<string, unknown> | null;
  generationsCompleted: number;
  generations: Generation[];
  trainingSignalCheck: TrainingSignalCheck;
  plateauChecks?: PlateauCheck[];
  stoppedOnPlateau?: boolean;
  artifactIntegrity: {
    generationArtifacts: number;
    illegalDecisions: number;
    incompleteDecisions: number;
    censoredGames: number;
  };
}

export interface SelectStage {
  selection: Record<string, unknown> | null;
  finalistMeans: string[];
  candidateSha256: string | null;
}

export interface ScreenGame {
  seedHex: string;
  score: number;
  moves: number;
  censored: boolean;
}

export interface ScreenArm {
  games: number;
  mean: number;
  median: number;
  q25: number;
  max: number;
  movesMean: number;
  clearsPerMove: number;
  revealsPerMove: number;
  censored: number;
  illegal: number;
  incomplete: number;
  wallSeconds: number;
  perGame: ScreenGame[];
}

export interface PairedContrast {
  candidateArm: string;
  referenceArm: string;
  meanDelta: number;
  bootstrapLower95: number;
  bootstrapUpper95: number;
  studentTLower95: number;
  pairedSd: number;
  detectionFloor: number;
  wtl: [number, number, number];
  halves: [number, number];
  q25Delta: number;
  movesDelta: number;
  /** Candidate score minus reference score on the same seed (derived by the extractor). */
  perSeed: { seedHex: string; delta: number }[];
}

export interface GateCheck {
  criterion: string;
  passed: boolean;
  observed: unknown;
}

export interface ScreenStage {
  config: Record<string, unknown>;
  seedStartHex: string;
  arms: Record<string, ScreenArm>;
  paired: Record<string, PairedContrast>;
  gate: {
    checks: GateCheck[];
    allPassed: boolean;
    meanDelta: number;
    pairedSd: number;
    detectionFloor: number;
    wtl: [number, number, number];
  } | null;
}

export interface Rusage {
  label: string;
  wallSeconds: number;
  userSeconds: number;
  systemSeconds: number;
  peakRssBytes: number;
  exitCode: number;
}

export interface EvolutionSnapshot {
  format: typeof SNAPSHOT_FORMAT;
  runId: string;
  experimentId: string | null;
  theoryId: string | null;
  runLifecycle: string | null;
  /** When the extractor ran (UTC). Not a research number. */
  capturedAt: string;
  /** Repository-relative artifact paths the snapshot was copied from. */
  sources: string[];
  derived: string[];
  corpus: CorpusStage | null;
  pretrain: PretrainStage | null;
  evolve: EvolveStage | null;
  select: SelectStage | null;
  screen: ScreenStage | null;
  rusage: Rusage[] | null;
}

export const CONTRAST_LABELS: Record<string, string> = {
  "candidate-vs-baseline-run1": "continued candidate minus the first run's candidate, both at depth 3",
  "baseline-run1-vs-fair-d3s7": "the first run's candidate minus fair leaf, both at depth 3",
  "candidate-vs-fair-d3s7": "evolved candidate minus fair leaf, both at depth 3",
  "init-vs-fair-d3s7": "unevolved init minus fair leaf, both at depth 3",
  "candidate-vs-init": "evolved candidate minus unevolved init",
  "fair-d4s7-vs-fair-d3s7": "fair leaf at depth 4 minus fair leaf at depth 3 (reference)",
};

export const ARM_LABELS: Record<string, string> = {
  candidate: "evolved NNUE leaf, depth 3",
  "baseline-run1": "first run's frozen candidate, depth 3",
  "init-d3s7": "unevolved (supervised) NNUE leaf, depth 3",
  "fair-d3s7": "frozen fair leaf, depth 3",
  "fair-d4s7": "frozen fair leaf, depth 4 (reference)",
};

/* ------------------------------------------------------------ validation */

type Fail = (message: string) => never;

function isObject(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

function num(o: Record<string, unknown>, key: string, at: string, fail: Fail): number {
  const v = o[key];
  if (typeof v !== "number" || !Number.isFinite(v)) return fail(`${at}.${key} must be a finite number`);
  return v;
}

function optionalNum(o: Record<string, unknown>, key: string, at: string, fail: Fail): void {
  const v = o[key];
  if (v === undefined || v === null) return;
  if (typeof v !== "number" || !Number.isFinite(v)) return fail(`${at}.${key} must be a finite number or null`);
}

function str(o: Record<string, unknown>, key: string, at: string, fail: Fail): string {
  const v = o[key];
  if (typeof v !== "string") return fail(`${at}.${key} must be a string`);
  return v;
}

function dist(raw: unknown, at: string, fail: Fail): void {
  if (!isObject(raw)) return fail(`${at} must be a distribution object`);
  for (const key of ["n", "mean", "sd", "min", "q25", "median", "q75", "max"]) num(raw, key, at, fail);
}

function optionalDist(raw: unknown, at: string, fail: Fail): void {
  if (raw === null || raw === undefined) return;
  dist(raw, at, fail);
}

function validateCorpus(raw: unknown, fail: Fail): void {
  if (!isObject(raw)) return fail("corpus must be an object");
  const at = "corpus";
  const games = num(raw, "games", at, fail);
  num(raw, "roots", at, fail);
  num(raw, "censoredGames", at, fail);
  dist(raw.score, `${at}.score`, fail);
  dist(raw.moves, `${at}.moves`, fail);
  dist(raw.wallSecondsPerGame, `${at}.wallSecondsPerGame`, fail);
  optionalNum(raw, "secondsPerRoot", at, fail);
  optionalNum(raw, "rootsPerGame", at, fail);
  optionalDist(raw.legalColumnsPerRoot, `${at}.legalColumnsPerRoot`, fail);
  optionalDist(raw.teacherSiblingSpreadPoints, `${at}.teacherSiblingSpreadPoints`, fail);
  if (!Array.isArray(raw.perGame)) return fail(`${at}.perGame must be an array`);
  if (raw.perGame.length > 0 && raw.perGame.length !== games) return fail(`${at}.perGame has ${raw.perGame.length} rows but games is ${games}`);
  raw.perGame.forEach((g, i) => {
    const where = `${at}.perGame[${i}]`;
    if (!isObject(g)) return fail(`${where} must be an object`);
    str(g, "seed", where, fail);
    num(g, "score", where, fail);
    num(g, "moves", where, fail);
    num(g, "wallSeconds", where, fail);
    if (typeof g.censored !== "boolean") return fail(`${where}.censored must be a boolean`);
  });
}

function validatePretrain(raw: unknown, fail: Fail): void {
  if (!isObject(raw)) return fail("pretrain must be an object");
  if (!isObject(raw.report)) return fail("pretrain.report must be an object");
  const report = raw.report;
  for (const key of ["roots", "games", "trainRoots", "valRoots", "epochs", "batch", "lr", "bestEpoch", "bestValHuber"]) num(report, key, "pretrain.report", fail);
  str(report, "seed", "pretrain.report", fail);
  if (!Array.isArray(raw.epochs)) return fail("pretrain.epochs must be an array");
  raw.epochs.forEach((e, i) => {
    const where = `pretrain.epochs[${i}]`;
    if (!isObject(e)) return fail(`${where} must be an object`);
    for (const key of ["epoch", "trainHuber", "valHuber", "valPearson"]) num(e, key, where, fail);
  });
  if (raw.probe !== null && raw.probe !== undefined) {
    if (!isObject(raw.probe)) return fail("pretrain.probe must be an object or null");
    for (const key of ["probeRoots", "top1", "meanTeacherValueRegret"]) num(raw.probe, key, "pretrain.probe", fail);
  }
}

function validateEvolve(raw: unknown, fail: Fail): void {
  if (!isObject(raw)) return fail("evolve must be an object");
  if (raw.config !== null && raw.config !== undefined && !isObject(raw.config)) return fail("evolve.config must be an object or null");
  const completed = num(raw, "generationsCompleted", "evolve", fail);
  if (!Array.isArray(raw.generations)) return fail("evolve.generations must be an array");
  if (raw.generations.length !== completed) return fail(`evolve.generations has ${raw.generations.length} rows but generationsCompleted is ${completed}`);
  raw.generations.forEach((g, i) => {
    const where = `evolve.generations[${i}]`;
    if (!isObject(g)) return fail(`${where} must be an object`);
    for (const key of ["generation", "best", "mean", "top4Mean", "controlFair", "controlInit"]) num(g, key, where, fail);
    str(g, "blockStart", where, fail);
    optionalNum(g, "controlBaseline", where, fail);
    optionalNum(g, "sigmaRel", where, fail);
    if (!Array.isArray(g.fitness) || !g.fitness.every((f) => typeof f === "number" && Number.isFinite(f))) return fail(`${where}.fitness must be a list of numbers`);
  });
  if (!isObject(raw.trainingSignalCheck)) return fail("evolve.trainingSignalCheck must be an object");
  const check = raw.trainingSignalCheck;
  str(check, "rule", "evolve.trainingSignalCheck", fail);
  if (!Array.isArray(check.window)) return fail("evolve.trainingSignalCheck.window must be an array");
  for (const key of ["generationsMeanAboveFair", "generationsTop4AboveFair", "generationsBestAboveFair", "generationsInitAboveFair"]) num(check, key, "evolve.trainingSignalCheck", fail);
  optionalNum(check, "meanMarginLast10", "evolve.trainingSignalCheck", fail);
  optionalNum(check, "top4MarginLast10", "evolve.trainingSignalCheck", fail);
  if (check.passed !== null && typeof check.passed !== "boolean") return fail("evolve.trainingSignalCheck.passed must be a boolean or null");
  if (raw.plateauChecks !== undefined) {
    if (!Array.isArray(raw.plateauChecks)) return fail("evolve.plateauChecks must be an array");
    raw.plateauChecks.forEach((p, i) => {
      const where = `evolve.plateauChecks[${i}]`;
      if (!isObject(p)) return fail(`${where} must be an object`);
      for (const key of ["generation", "window", "slopePerGeneration", "standardError", "lowerBound95", "windowMeanFirstHalf", "windowMeanSecondHalf"]) num(p, key, where, fail);
      if (typeof p.stop !== "boolean") return fail(`${where}.stop must be a boolean`);
    });
  }
  if (!isObject(raw.artifactIntegrity)) return fail("evolve.artifactIntegrity must be an object");
  for (const key of ["generationArtifacts", "illegalDecisions", "incompleteDecisions", "censoredGames"]) num(raw.artifactIntegrity, key, "evolve.artifactIntegrity", fail);
}

function validateSelect(raw: unknown, fail: Fail): void {
  if (!isObject(raw)) return fail("select must be an object");
  if (raw.selection !== null && raw.selection !== undefined && !isObject(raw.selection)) return fail("select.selection must be an object or null");
  if (!Array.isArray(raw.finalistMeans) || !raw.finalistMeans.every((f) => typeof f === "string")) return fail("select.finalistMeans must be a list of strings");
  if (raw.candidateSha256 !== null && typeof raw.candidateSha256 !== "string") return fail("select.candidateSha256 must be a string or null");
}

function validateScreen(raw: unknown, fail: Fail): void {
  if (!isObject(raw)) return fail("screen must be an object");
  if (!isObject(raw.config)) return fail("screen.config must be an object");
  str(raw, "seedStartHex", "screen", fail);
  if (!isObject(raw.arms)) return fail("screen.arms must be an object");
  const armGames = new Map<string, number>();
  for (const [name, arm] of Object.entries(raw.arms)) {
    const where = `screen.arms.${name}`;
    if (!isObject(arm)) return fail(`${where} must be an object`);
    const games = num(arm, "games", where, fail);
    for (const key of ["mean", "median", "q25", "max", "movesMean", "clearsPerMove", "revealsPerMove", "censored", "illegal", "incomplete", "wallSeconds"]) num(arm, key, where, fail);
    if (!Array.isArray(arm.perGame)) return fail(`${where}.perGame must be an array`);
    if (arm.perGame.length > 0 && arm.perGame.length !== games) return fail(`${where}.perGame has ${arm.perGame.length} rows but games is ${games}`);
    arm.perGame.forEach((g, i) => {
      const at = `${where}.perGame[${i}]`;
      if (!isObject(g)) return fail(`${at} must be an object`);
      str(g, "seedHex", at, fail);
      num(g, "score", at, fail);
      num(g, "moves", at, fail);
      if (typeof g.censored !== "boolean") return fail(`${at}.censored must be a boolean`);
    });
    armGames.set(name, games);
  }
  if (!isObject(raw.paired)) return fail("screen.paired must be an object");
  for (const [name, contrast] of Object.entries(raw.paired)) {
    const where = `screen.paired.${name}`;
    if (!isObject(contrast)) return fail(`${where} must be an object`);
    const candidate = str(contrast, "candidateArm", where, fail);
    const reference = str(contrast, "referenceArm", where, fail);
    if (!armGames.has(candidate)) return fail(`${where}.candidateArm ${candidate} is not a screen arm`);
    if (!armGames.has(reference)) return fail(`${where}.referenceArm ${reference} is not a screen arm`);
    for (const key of ["meanDelta", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "pairedSd", "detectionFloor", "q25Delta", "movesDelta"]) num(contrast, key, where, fail);
    const wtl = contrast.wtl;
    if (!Array.isArray(wtl) || wtl.length !== 3 || !wtl.every((v) => Number.isInteger(v) && v >= 0)) return fail(`${where}.wtl must be [wins, ties, losses]`);
    const halves = contrast.halves;
    if (!Array.isArray(halves) || halves.length !== 2 || !halves.every((v) => typeof v === "number" && Number.isFinite(v))) return fail(`${where}.halves must be two numbers`);
    if (!Array.isArray(contrast.perSeed)) return fail(`${where}.perSeed must be an array`);
    const games = armGames.get(candidate) as number;
    if (contrast.perSeed.length > 0 && contrast.perSeed.length !== games) return fail(`${where}.perSeed has ${contrast.perSeed.length} rows but the candidate arm played ${games} games`);
    contrast.perSeed.forEach((r, i) => {
      const at = `${where}.perSeed[${i}]`;
      if (!isObject(r)) return fail(`${at} must be an object`);
      str(r, "seedHex", at, fail);
      num(r, "delta", at, fail);
    });
  }
  if (raw.gate !== null && raw.gate !== undefined) {
    if (!isObject(raw.gate)) return fail("screen.gate must be an object or null");
    if (!Array.isArray(raw.gate.checks)) return fail("screen.gate.checks must be an array");
    raw.gate.checks.forEach((c, i) => {
      if (!isObject(c) || typeof c.criterion !== "string" || typeof c.passed !== "boolean") return fail(`screen.gate.checks[${i}] needs a criterion and a boolean passed`);
    });
    if (typeof raw.gate.allPassed !== "boolean") return fail("screen.gate.allPassed must be a boolean");
  }
}

/**
 * Checks a parsed snapshot's format string and stage shapes, and that the
 * array lengths agree with the counts they sit beside (generations with
 * generationsCompleted, per-game rows with the arm's game count). A snapshot
 * whose stages are all null is valid: the run had produced no artifacts yet.
 */
export function validateSnapshot(raw: unknown, where = "snapshot"): EvolutionSnapshot {
  const fail: Fail = (message) => {
    throw new Error(`${where}: ${message}`);
  };
  if (!isObject(raw)) return fail("snapshot is not an object");
  if (raw.format !== SNAPSHOT_FORMAT) return fail(`format must be ${SNAPSHOT_FORMAT} (found ${String(raw.format)})`);
  const runId = str(raw, "runId", "snapshot", fail);
  if (!/^RUN-[A-Za-z0-9-]+$/.test(runId)) return fail(`runId ${runId} is not a run id`);
  for (const key of ["experimentId", "theoryId", "runLifecycle"]) {
    if (raw[key] !== null && typeof raw[key] !== "string") return fail(`${key} must be a string or null`);
  }
  str(raw, "capturedAt", "snapshot", fail);
  if (!Array.isArray(raw.sources) || !raw.sources.every((s) => typeof s === "string")) return fail("sources must be a list of strings");
  if (!Array.isArray(raw.derived) || !raw.derived.every((s) => typeof s === "string")) return fail("derived must be a list of strings");
  for (const key of ["corpus", "pretrain", "evolve", "select", "screen", "rusage"]) {
    if (!(key in raw)) return fail(`missing stage ${key} (use null for a stage with no artifacts)`);
  }
  if (raw.corpus !== null) validateCorpus(raw.corpus, fail);
  if (raw.pretrain !== null) validatePretrain(raw.pretrain, fail);
  if (raw.evolve !== null) validateEvolve(raw.evolve, fail);
  if (raw.select !== null) validateSelect(raw.select, fail);
  if (raw.screen !== null) validateScreen(raw.screen, fail);
  if (raw.rusage !== null && !Array.isArray(raw.rusage)) return fail("rusage must be an array or null");
  return raw as unknown as EvolutionSnapshot;
}

/** The stages a snapshot carries artifacts for, in pipeline order. */
export function snapshotStages(snapshot: EvolutionSnapshot): ("corpus" | "pretrain" | "evolve" | "select" | "screen")[] {
  const out: ("corpus" | "pretrain" | "evolve" | "select" | "screen")[] = [];
  if (snapshot.corpus) out.push("corpus");
  if (snapshot.pretrain) out.push("pretrain");
  if (snapshot.evolve) out.push("evolve");
  if (snapshot.select) out.push("select");
  if (snapshot.screen) out.push("screen");
  return out;
}
