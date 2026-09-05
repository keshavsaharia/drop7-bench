/**
 * Linkage between research records and approach directories.
 *
 * A theory, experiment, or result JSON under research/ names the code it is
 * about by repository path (`candidate.entryPoint`, `comparator.entryPoint`,
 * `expectedArtifacts`, `evidenceRefs`, prose fields). This module scans each
 * record's text for `approaches/<family>/<slug>` references and follows the
 * record graph (result -> experiment -> theories) so a page can list the
 * records that mention an approach and an approach for a record.
 *
 * Linkage only. Nothing here reads, derives, or compares a research number.
 * Every accessor returns an empty list or null on a checkout without research/.
 */

import {
  getExperiments,
  getResults,
  getTheories,
  type ExperimentRecord,
  type ResultRecord,
  type TheoryRecord,
} from "./repo.ts";

export interface ApproachRef {
  family: string;
  slug: string;
}

export interface ApproachRecords {
  theories: (TheoryRecord & { $id: string })[];
  experiments: (ExperimentRecord & { $id: string })[];
  results: (ResultRecord & { $id: string })[];
}

type Theory = TheoryRecord & { $id: string };
type Experiment = ExperimentRecord & { $id: string };
type Result = ResultRecord & { $id: string };

/**
 * Matches `approaches/<family>/<slug>` wherever it appears in a record,
 * including inside a longer path. Family and slug are lowercase directory
 * names, so `approaches/<family>/README.mdx` is not a match.
 */
const APPROACH_PATH = /approach\/([a-z0-9-]+)\/([a-z0-9-]+)(?![a-z0-9-])/g;

function refKey(ref: ApproachRef): string {
  return `${ref.family}/${ref.slug}`;
}

/** Every distinct approach reference in a string, in order of first appearance. */
export function approachRefsInText(text: string): ApproachRef[] {
  const seen = new Set<string>();
  const refs: ApproachRef[] = [];
  for (const match of text.matchAll(APPROACH_PATH)) {
    const ref = { family: match[1], slug: match[2] };
    const key = refKey(ref);
    if (seen.has(key)) continue;
    seen.add(key);
    refs.push(ref);
  }
  return refs;
}

/** Every distinct approach reference anywhere in a record's JSON. */
export function approachRefsInRecord(record: unknown): ApproachRef[] {
  return approachRefsInText(JSON.stringify(record));
}

interface RecordIndex {
  theories: Theory[];
  experiments: Experiment[];
  results: Result[];
  theoryById: Map<string, Theory>;
  experimentById: Map<string, Experiment>;
  resultById: Map<string, Result>;
  /** Refs named directly in each record's text, keyed by record id. */
  refsByRecord: Map<string, ApproachRef[]>;
}

function buildIndex(): RecordIndex {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const refsByRecord = new Map<string, ApproachRef[]>();
  const theoryById = new Map<string, Theory>();
  const experimentById = new Map<string, Experiment>();
  const resultById = new Map<string, Result>();
  for (const record of theories) {
    theoryById.set(record.$id, record);
    refsByRecord.set(record.$id, approachRefsInRecord(record));
  }
  for (const record of experiments) {
    experimentById.set(record.$id, record);
    refsByRecord.set(record.$id, approachRefsInRecord(record));
  }
  for (const record of results) {
    resultById.set(record.$id, record);
    refsByRecord.set(record.$id, approachRefsInRecord(record));
  }
  return { theories, experiments, results, theoryById, experimentById, resultById, refsByRecord };
}

function byId<T extends { $id: string }>(records: Iterable<T>): T[] {
  return [...records].sort((a, b) => a.$id.localeCompare(b.$id));
}

/**
 * The records that mention an approach directory, plus the records joined to
 * those through the record graph: an experiment's theories and results, a
 * result's experiment and theories, a theory's experiments and their results.
 */
export function recordsForApproach(family: string, slug: string): ApproachRecords {
  const index = buildIndex();
  const key = `${family}/${slug}`;
  const theories = new Map<string, Theory>();
  const experiments = new Map<string, Experiment>();
  const results = new Map<string, Result>();

  const mentions = (id: string) => (index.refsByRecord.get(id) ?? []).some((ref) => refKey(ref) === key);

  for (const experiment of index.experiments) {
    if (mentions(experiment.$id)) experiments.set(experiment.$id, experiment);
  }
  for (const theory of index.theories) {
    if (mentions(theory.$id)) theories.set(theory.$id, theory);
  }
  for (const result of index.results) {
    if (mentions(result.$id)) results.set(result.$id, result);
  }

  // Theories named by a linked experiment.
  for (const experiment of [...experiments.values()]) {
    for (const theoryId of experiment.theoryIds ?? []) {
      const theory = index.theoryById.get(theoryId);
      if (theory) theories.set(theory.$id, theory);
    }
  }
  // Experiments that test a linked theory, unless their own paths name only another directory.
  for (const experiment of index.experiments) {
    const sharesTheory = (experiment.theoryIds ?? []).some((id) => theories.has(id));
    if (sharesTheory && !experimentRefersOnlyElsewhere(index, experiment, key)) {
      experiments.set(experiment.$id, experiment);
    }
  }
  // Results recorded against a linked experiment.
  for (const result of index.results) {
    if (result.experimentId && experiments.has(result.experimentId)) results.set(result.$id, result);
  }
  // Experiments and theories behind a directly linked result.
  for (const result of [...results.values()]) {
    const experiment = result.experimentId ? index.experimentById.get(result.experimentId) : undefined;
    if (experiment) experiments.set(experiment.$id, experiment);
    for (const theoryId of result.theoryIds ?? []) {
      const theory = index.theoryById.get(theoryId);
      if (theory) theories.set(theory.$id, theory);
    }
  }

  return {
    theories: byId(theories.values()),
    experiments: byId(experiments.values()),
    results: byId(results.values()),
  };
}

/**
 * An experiment that shares a theory with the approach but whose own
 * candidate path names a different approach directory belongs to that other
 * directory, so it is not pulled in through the theory.
 */
function experimentRefersOnlyElsewhere(index: RecordIndex, experiment: Experiment, key: string): boolean {
  const refs = index.refsByRecord.get(experiment.$id) ?? [];
  if (refs.length === 0) return false;
  return !refs.some((ref) => refKey(ref) === key);
}

function primaryRefForExperiment(index: RecordIndex, experiment: Experiment): ApproachRef | null {
  const candidate = experiment.candidate?.entryPoint;
  if (typeof candidate === "string") {
    const [ref] = approachRefsInText(candidate);
    if (ref) return ref;
  }
  const [first] = index.refsByRecord.get(experiment.$id) ?? [];
  return first ?? null;
}

/**
 * The approach directory a record is about: an experiment's candidate entry
 * point, a result's experiment, a theory's first path reference. Null when the
 * record names no approach directory at all.
 */
export function approachForRecord(id: string): ApproachRef | null {
  const index = buildIndex();
  const experiment = index.experimentById.get(id);
  if (experiment) return primaryRefForExperiment(index, experiment);

  const result = index.resultById.get(id);
  if (result) {
    const parent = result.experimentId ? index.experimentById.get(result.experimentId) : undefined;
    if (parent) {
      const ref = primaryRefForExperiment(index, parent);
      if (ref) return ref;
    }
    const [first] = index.refsByRecord.get(id) ?? [];
    return first ?? null;
  }

  const theory = index.theoryById.get(id);
  if (theory) {
    // A theory is placed by the candidate of the first experiment that tests
    // it; evidence references may point at instruments (an engine, a corpus)
    // rather than the strategy the theory is about.
    for (const experiment of index.experiments) {
      if (!(experiment.theoryIds ?? []).includes(id)) continue;
      const ref = primaryRefForExperiment(index, experiment);
      if (ref) return ref;
    }
    const fromRefs = approachRefsInText((theory.evidenceRefs ?? []).join("\n"));
    if (fromRefs[0]) return fromRefs[0];
    const [first] = index.refsByRecord.get(id) ?? [];
    if (first) return first;
    for (const experiment of index.experiments) {
      if (!(experiment.theoryIds ?? []).includes(id)) continue;
      const ref = primaryRefForExperiment(index, experiment);
      if (ref) return ref;
    }
    return null;
  }
  return null;
}

/** Every result record, newest `recordedAt` first. */
export function listResults(): Result[] {
  return [...getResults()].sort((a, b) => {
    const at = typeof a.recordedAt === "string" ? a.recordedAt : "";
    const bt = typeof b.recordedAt === "string" ? b.recordedAt : "";
    if (at !== bt) return at < bt ? 1 : -1;
    return a.$id.localeCompare(b.$id);
  });
}

/** Approach references for every record id, for building cross-links in one pass. */
export function listRecordApproachRefs(): Map<string, ApproachRef[]> {
  return buildIndex().refsByRecord;
}
