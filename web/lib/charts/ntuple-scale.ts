/**
 * The committed snapshot of one ntuple-scale run, written by
 * web/scripts/extract-ntuple-scale.ts from the run's artifacts under
 * runs/<run-id>/ntuple-scale/ and read by web/components/NTupleScale.tsx.
 *
 * Every number in a snapshot is copied from a run artifact (analysis.json,
 * progress.jsonl, val-*.json, heldout.json). The only values the extractor
 * derives itself are the per-seed paired differences in the screen
 * contrasts, which subtract two recorded scores for the same seed; the
 * snapshot says so in `derived`. Shared by server and client code, so this
 * module imports nothing from Node.
 */
import type { GateCheck, PairedContrast, Rusage, ScreenStage } from "./evolution";

export const SNAPSHOT_FORMAT = "drop7-ntuple-scale-snapshot-v1";

export type { PairedContrast, Rusage, ScreenStage };

/** The plateau rule's reading at one validation point (the replication run). */
export interface PlateauReading {
  points: number;
  window: number;
  recentMean: number;
  previousMean: number;
  stop: boolean;
}

/** One validation point: the paired line-up on the training-role validation block. */
export interface ValidationPoint {
  movesTrained: number;
  artifact: string;
  ntupleD3Mean: number;
  ntupleD3MovesMean: number;
  directMean: number | null;
  directMovesMean: number | null;
  fairD3Mean: number;
  fairD3MovesMean: number;
  pairedDeltaD3: number;
  bootstrapLower95: number;
  bootstrapUpper95: number;
  studentTLower95: number;
  detectionFloor: number;
  wtl: [number, number, number];
  halves: [number, number];
  pairedDeltaDirect: number | null;
  touchedEntries: number | null;
  isBest: boolean;
  plateau: PlateauReading | null;
}

/** One training chunk's progress row. */
export interface ChunkRow {
  chunk: number;
  movesTotal: number;
  gamesTotal: number;
  wallSeconds: number;
  movesPerSecond: number;
  trainMeanScore: number;
  trainMeanMoves: number;
  trainMaxScore: number;
  trainClearsPerMove: number;
  trainRevealsPerMove: number;
  meanAbsDelta: number;
  meanBeta: number;
  seedWraps: number | null;
  /** The direct one-ply policy's mean on the validation block, when this chunk ran the quick check. */
  quickDirectMean: number | null;
}

export interface TrainingRun {
  name: string;
  /** The warm start's own validation point before any training move (the search-target experiment), else null. */
  start?: { moves: number; artifact: string; pairedDeltaD3: number; ntupleD3Mean: number; fairD3Mean: number; directMean: number; pairedDeltaDirect: number } | null;
  layout: string;
  alpha: number;
  entries: number;
  activePerState: number;
  /** The frozen table file a warm-started arm began from (the fill-conditioned experiment), else null. */
  initFrom?: string | null;
  /** Games in the paired validation block (64 in the first experiment, 256 in the replication). */
  validateGames: number;
  movesTotal: number;
  gamesTotal: number;
  wallSeconds: number;
  meanMovesPerSecond: number | null;
  done: boolean;
  chunks: ChunkRow[];
  validations: ValidationPoint[];
  best: { moves: number; artifact: string; pairedDeltaD3: number; ntupleD3Mean: number; fairD3Mean: number } | null;
  finalMargin: number | null;
  bestMargin: number | null;
  anyPositiveMargin: boolean;
  illegalDecisions: number;
  incompleteDecisions: number;
  /** Why the run stopped, from stop.json (the replication run). */
  stop: {
    reason: string;
    movesTotal: number;
    validationPoints: number;
    plateauWindow: number;
    recentWindowMean: number | null;
    previousWindowMean: number | null;
    bestMargin: number | null;
  } | null;
}

/** The first experiment's selection: the arm with the largest final validation margin. */
export interface PilotSelection {
  arm: string;
  layout: string;
  alpha: number;
  finalMargin: number;
  ranking: { arm: string; finalMargin: number; entries: number }[];
  rule: string;
}

/** The fill-conditioned experiment's selection: the fill arm with the larger best validation margin, beside the control arm's best point. */
export interface FillSelection {
  candidateArm: string;
  candidateBestMargin: number;
  candidateBestMoves: number | null;
  controlBestMargin: number;
  controlBestMoves: number | null;
  arms: Record<string, { bestMargin: number; finalMargin: number | null; bestMoves: number | null; movesTotal: number; validationPoints: number; stop: string | null }>;
  trainingSignal: { criterion: string; passed: boolean };
  rule: string;
}

/** The search-target experiment's selection: the treestrap arm by protocol, beside the searchtd ablation and each arm's warm-start margin. */
export interface TreeSelection {
  candidateArm: string;
  candidateBestMargin: number;
  candidateBestMoves: number | null;
  ablationArm: string;
  ablationBestMargin: number;
  ablationBestMoves: number | null;
  arms: Record<string, { bestMargin: number; finalMargin: number | null; bestMoves: number | null; movesTotal: number; validationPoints: number; stop: string | null; warmStartMargin: number | null }>;
  trainingSignal: { criterion: string; passed: boolean | null };
  rule: string;
}

export interface PilotStage {
  arms: Record<string, TrainingRun>;
  selection: PilotSelection | FillSelection | TreeSelection | null;
  rule: string;
}

export function isTreeSelection(selection: PilotSelection | FillSelection | TreeSelection | null): selection is TreeSelection {
  return selection !== null && "ablationArm" in selection;
}

export function isFillSelection(selection: PilotSelection | FillSelection | TreeSelection | null): selection is FillSelection {
  return selection !== null && "candidateArm" in selection && !("ablationArm" in selection);
}

/** One paired reading copied from analyze.py (a contrast or the interaction series). */
export interface DepthReading {
  meanDelta: number;
  bootstrapLower95: number;
  bootstrapUpper95: number;
  studentTLower95: number;
  detectionFloor: number;
  wtl: [number, number, number];
  halves: [number, number];
}

/** The depth-4 experiment's readings: the frozen tables one ply deeper. */
export interface DepthStage {
  /** The contrast the preregistered gate reads (prior-d4s7-vs-prior-d3s7). */
  primary: string;
  /** The fourth ply's paired gain on the tables (the gate contrast). */
  tablesStep: DepthReading;
  /** The fourth ply's paired gain on the fair leaf on the same seeds. */
  fairStep: DepthReading | null;
  /** prior-d4s7 against fair-d4s7 under the same four criteria as the gate. */
  persistence: { checks: GateCheck[]; allPassed: boolean } | null;
  /** Per game, (tables d4 minus tables d3) minus (fair d4 minus fair d3): the preregistered verdict. */
  interaction: DepthReading & { verdict: "larger" | "smaller" | "inconclusive" };
}

/** One reading of the fill-conditioned experiment: a paired contrast with the preregistered three-way verdict. */
export interface FillReading extends DepthReading {
  contrast: string;
  verdict: "supported" | "refuted" | "inconclusive";
  candidateQ25: number;
  referenceQ25: number;
  /** Present when the reading is also judged under the gate's four criteria (the continuation reading). */
  checks?: GateCheck[];
  allPassed?: boolean;
}

/** The fill-conditioned experiment's readings beside its gate. */
export interface FillStage {
  /** The contrast the gate reads (fill-d3s7-vs-prior-d3s7). */
  primary: string;
  conditioning: FillReading | null;
  continuation: FillReading | null;
  zeroed: FillReading | null;
  classmean: FillReading | null;
  fillDepth4: FillReading | null;
  zeroedDepth4: FillReading | null;
  depthSteps: Record<string, FillReading>;
  direct: Record<string, FillReading>;
  replicationOfPrior: FillReading | null;
  theory: Record<string, boolean | null>;
}

/** The search-target experiment's readings beside its gate (the same reading shape as the fill experiment's). */
export interface TreeStage {
  primary: string;
  offPathBoards: FillReading | null;
  ablation: FillReading | null;
  vsOnePlyContinuation: FillReading | null;
  treestrapDepth4: FillReading | null;
  ablationDepth4: FillReading | null;
  depthSteps: Record<string, FillReading>;
  direct: Record<string, FillReading>;
  replicationOfPrior: FillReading | null;
  theory: Record<string, boolean | null>;
}

/** The screen with the replication's two extra readings, and the depth experiment's, on top of the shared shape. */
export interface NTupleScreenStage extends ScreenStage {
  /** Which paired contrast the gate reads; candidate-d3s7-vs-fair-d3s7 for the first two experiments. */
  primaryContrast: string;
  /** The depth-4 experiment's readings, null for the training experiments. */
  depth: DepthStage | null;
  /** The fill-conditioned experiment's readings, null elsewhere. */
  fill: FillStage | null;
  /** The search-target (TreeStrap) experiment's readings, null elsewhere. */
  tree: TreeStage | null;
  /** The first experiment's frozen tables against the fair leaf on this fresh block. */
  replication: { checks: GateCheck[]; allPassed: boolean } | null;
  /** The wider candidate against the first candidate: the preregistered verdict. */
  scale: {
    verdict: "supported" | "refuted" | "inconclusive";
    meanDelta: number;
    bootstrapLower95: number;
    bootstrapUpper95: number;
    studentTLower95: number;
    detectionFloor: number;
    wtl: [number, number, number];
  } | null;
}

export interface NTupleSnapshot {
  format: typeof SNAPSHOT_FORMAT;
  runId: string;
  experimentId: string | null;
  theoryId: string | null;
  runLifecycle: string | null;
  /** When the extractor ran (UTC). Not a research number. */
  capturedAt: string;
  sources: string[];
  derived: string[];
  gates: { passed: boolean; gates: string[] } | null;
  /** The fill-conditioned experiment's second gate run (the hgt5 layout) and the gate runs on each frozen table file before the screen. */
  gatesHgt5?: { passed: boolean; gates: string[] } | null;
  frozenGates?: Record<string, { passed: boolean; gates: string[] }> | null;
  /** The fill-conditioned experiment's no-training edits of the frozen tables (edits.json without the per-family detail). */
  edits?: {
    entries: number;
    optimisticStart: number;
    untouchedEntries: number;
    touchedEntries: number;
    classesWithNoTouchedEntry: number;
    outputs: Record<string, { sha256: string; entriesChanged: number; entriesChangedOnlyWhereUntouched: boolean }>;
  } | null;
  /** The throughput smoke run on the probe block (the replication run); tables discarded. */
  smoke: TrainingRun | null;
  pilot: PilotStage | null;
  main: TrainingRun | null;
  freeze: { candidateSha256: string | null; priorSha256?: string | null; controlSha256?: string | null; zeroedSha256?: string | null; classmeanSha256?: string | null; searchtdSha256?: string | null } | null;
  screen: NTupleScreenStage | null;
  rusage: Rusage[] | null;
}
