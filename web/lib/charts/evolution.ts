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
