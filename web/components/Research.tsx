/**
 * Components for research pages: evidence labels and sentence-form summaries
 * of machine-readable records. The collapsible sections live in Reveal.tsx;
 * `TechnicalDetails` is re-exported from here for the pages that import it.
 *
 * Record summaries are built ONLY from the record's own fields; nothing is
 * computed, averaged, or inferred. Missing fields render as absent. Label
 * text and colour come from lib/labels.ts and the `.badge` block in
 * globals.css, so a `fail` outcome is neutral on every route.
 */

import Link from "next/link";
import { Badge } from "./Badge";
import { TechnicalDetails, TechnicalRecord } from "./Reveal";
import { readsSentence, tierSentence } from "@/lib/labels";
import {
  getExperiments,
  getResults,
  getTheories,
  type ExperimentRecord,
  type ResultRecord,
  type TheoryRecord,
} from "@/lib/repo";

export { TechnicalDetails };

const PANEL = "my-4 rounded-md border border-rule bg-surface p-4 text-small text-ink-1";
const LINK = "text-accent hover:underline";

export function EvidenceLabel({
  reads,
}: {
  status?: string;
  evidence?: string;
  reads?: string;
}) {
  // Approach routes now own the single compact label row. Keep only the
  // explanatory information-boundary sentence for privileged work so older
  // README files do not duplicate the header badges.
  if (!reads || reads === "public") return null;
  return (
    <div className="my-3 text-caption text-ink-3">
      <span>{readsSentence(reads)}</span>
    </div>
  );
}

export function TheorySummary({ id }: { id: string }) {
  const theory = getTheories().find((t) => t.theoryId === id) as TheoryRecord | undefined;
  if (!theory) return <Missing kind="theory" id={id} />;
  return (
    <div className={PANEL}>
      <div className="flex flex-wrap items-center gap-2">
        <Badge value="theory" dot={false} />
        <Badge kind="outcome" value={theory.assessment} />
        <Badge value={theory.lifecycle} />
        <Link href={`/theories/${theory.theoryId}`} className={LINK}>
          {theory.title}
        </Link>
      </div>
      <p className="mt-2">
        <span className="text-ink-3">Claim: </span>
        {theory.claim}
      </p>
      <p className="mt-1 text-ink-2">
        This theory is currently <strong className="text-ink">{theory.assessment}</strong> at the{" "}
        {tierSentence(theory.evidenceTier)} level.
      </p>
    </div>
  );
}

export function ExperimentSummary({ id }: { id: string }) {
  const experiment = getExperiments().find((e) => e.experimentId === id) as ExperimentRecord | undefined;
  if (!experiment) return <Missing kind="experiment" id={id} />;
  const results = getResults().filter((r) => r.experimentId === id);
  return (
    <div className={PANEL}>
      <div className="flex flex-wrap items-center gap-2">
        <Badge value="experiment" dot={false} />
        <Badge kind="status" value={experiment.lifecycle} />
        <Link href={`/experiments/${experiment.experimentId}`} className={LINK}>
          {experiment.title}
        </Link>
      </div>
      <p className="mt-2">
        It compares <strong className="text-ink">{experiment.candidate.name}</strong> against{" "}
        <strong className="text-ink">{experiment.comparator.name}</strong> at the{" "}
        {tierSentence(experiment.benchmarkTier)} level, using {experiment.data.role} data.
      </p>
      {results.length === 0 ? (
        <p className="mt-1 text-ink-3">No result has been recorded for it.</p>
      ) : (
        results.map((r) => <ResultSentence key={r.resultId} result={r} />)
      )}
    </div>
  );
}

function ResultSentence({ result }: { result: ResultRecord }) {
  return (
    <p className="mt-2">
      <Badge kind="validity" value={result.runValidity} />{" "}
      <Badge kind="outcome" value={result.scientificOutcome} />{" "}
      The run was <strong className="text-ink">{result.runValidity}</strong> and the outcome was{" "}
      <strong className="text-ink">{result.scientificOutcome}</strong> ({tierSentence(result.evidenceTier)}).{" "}
      <Link href={`/experiments/${result.experimentId}#${result.resultId}`} className={LINK}>
        Read the result
      </Link>
      .
    </p>
  );
}

export function ResultSummary({ id }: { id: string }) {
  const result = getResults().find((r) => r.resultId === id) as ResultRecord | undefined;
  if (!result) return <Missing kind="result" id={id} />;
  const passed = result.gateChecks.filter((g) => g.passed === true).length;
  const failed = result.gateChecks.filter((g) => g.passed === false).length;
  return (
    <div className={PANEL}>
      <div className="flex flex-wrap items-center gap-2">
        <Badge value="result" dot={false} />
        <Badge kind="validity" value={result.runValidity} />
        <Badge kind="outcome" value={result.scientificOutcome} />
        <Badge kind="tier" value={result.evidenceTier} />
        <span className="label">{result.resultId}</span>
      </div>
      <p className="mt-2">
        The run was <strong className="text-ink">{result.runValidity}</strong>; the outcome was{" "}
        <strong className="text-ink">{result.scientificOutcome}</strong>, at the{" "}
        {tierSentence(result.evidenceTier)} level.
        {result.gateChecks.length > 0 && (
          <>
            {" "}
            Of {result.gateChecks.length} preregistered checks, {passed} passed and {failed} failed.
          </>
        )}
      </p>
      <p className="mt-2 text-ink-2">{result.summary}</p>
      {result.limitations?.length > 0 && (
        <TechnicalRecord summary="Limitations recorded with the result" meta={result.resultId}>
          <ul className="list-disc pl-5">
            {result.limitations.map((l, i) => (
              <li key={i}>{l}</li>
            ))}
          </ul>
        </TechnicalRecord>
      )}
      <p className="mt-2">
        <Link href={`/experiments/${result.experimentId}#${result.resultId}`} className={LINK}>
          Full record →
        </Link>
      </p>
    </div>
  );
}

/** Display a recorded JSON value. Nested objects are listed, never rendered as React children. */
export function RecordedValue({ value }: { value: unknown }) {
  if (typeof value === "number") {
    if (!Number.isFinite(value)) return String(value);
    return Number.isInteger(value) ? value.toLocaleString() : value.toFixed(4);
  }
  if (typeof value === "string") return value;
  if (typeof value === "boolean") return value ? "true" : "false";
  if (value === null || value === undefined) return null;
  if (Array.isArray(value)) {
    if (value.length === 0) return "[]";
    return (
      <ol className="list-decimal pl-4 font-normal">
        {value.map((item, index) => (
          <li key={index}>
            <RecordedValue value={item} />
          </li>
        ))}
      </ol>
    );
  }
  if (typeof value === "object") {
    const entries = Object.entries(value as Record<string, unknown>);
    if (entries.length === 0) return "{}";
    return (
      <dl className="space-y-0.5 font-normal">
        {entries.map(([key, nested]) => (
          <div key={key} className="flex flex-wrap gap-x-2">
            <dt className="label">{key}</dt>
            <dd className="text-caption text-ink-1">
              <RecordedValue value={nested} />
            </dd>
          </div>
        ))}
      </dl>
    );
  }
  return String(value);
}

function isNestedRecordedValue(value: unknown): boolean {
  return value !== null && typeof value === "object";
}

export function RecordedMetrics({ metrics }: { metrics: Record<string, unknown> }) {
  const entries = Object.entries(metrics ?? {});
  if (entries.length === 0) return null;
  return (
    <div className="mt-2 grid grid-cols-2 gap-1 sm:grid-cols-3 lg:grid-cols-4">
      {entries.map(([key, value]) => (
        <div
          key={key}
          className={`rounded-sm bg-raised px-2 py-1 ${isNestedRecordedValue(value) ? "col-span-full" : ""}`}
        >
          <div className="label truncate" title={key}>
            {key}
          </div>
          <div className="text-caption font-semibold tabular-nums text-ink-1">
            <RecordedValue value={value} />
          </div>
        </div>
      ))}
    </div>
  );
}

function Missing({ kind, id }: { kind: string; id: string }) {
  return (
    <div className="my-4 rounded-md border border-dashed border-rule-strong p-3 text-caption text-ink-2">
      No {kind} record <code>{id}</code> is present in this checkout.
    </div>
  );
}

const GATE_MARK: Record<string, { glyph: string; className: string }> = {
  passed: { glyph: "✓", className: "text-status-completed" },
  failed: { glyph: "✕", className: "text-ink-2" },
  none: { glyph: "–", className: "text-ink-3" },
};

/** Sentence-form rendering of a result for the record pages. */
export function ResultNarrative({ result }: { result: ResultRecord }) {
  return (
    <div id={result.resultId} className="rounded-md border border-rule bg-surface p-4 text-small text-ink-1">
      <div className="flex flex-wrap items-center gap-2">
        <Badge kind="validity" value={result.runValidity} />
        <Badge kind="outcome" value={result.scientificOutcome} />
        <Badge kind="outcome" value={result.assessment} />
        <Badge kind="tier" value={result.evidenceTier} />
        <span className="label">{result.resultId}</span>
      </div>
      <p className="mt-3">{result.summary}</p>
      {result.gateChecks.length > 0 && (
        <div className="mt-3">
          <div className="label">What it had to pass</div>
          <ul className="mt-1 space-y-1">
            {result.gateChecks.map((g, i) => {
              const mark = GATE_MARK[g.passed === true ? "passed" : g.passed === false ? "failed" : "none"];
              return (
                <li key={i} className="flex gap-2">
                  <span className={mark.className}>{mark.glyph}</span>
                  <span>
                    {g.criterion}
                    <span className="text-ink-3"> — observed: {g.observed}</span>
                  </span>
                </li>
              );
            })}
          </ul>
        </div>
      )}
      {Object.keys(result.metrics ?? {}).length > 0 && (
        <TechnicalRecord summary="Recorded metrics" meta={result.resultId}>
          <RecordedMetrics metrics={result.metrics} />
        </TechnicalRecord>
      )}
      {result.limitations?.length > 0 && (
        <div className="mt-3">
          <div className="label">Limitations</div>
          <ul className="mt-1 list-disc pl-5 text-ink-2">
            {result.limitations.map((l, i) => (
              <li key={i}>{l}</li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
}
