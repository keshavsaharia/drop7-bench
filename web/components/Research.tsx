/**
 * Components for research pages: evidence labels, collapsible technical
 * detail, and sentence-form summaries of machine-readable records.
 *
 * Record summaries are built ONLY from the record's own fields; nothing is
 * computed, averaged, or inferred. Missing fields render as absent.
 */

import Link from "next/link";
import { Badge } from "./Badge";
import {
  getExperiments,
  getResults,
  getTheories,
  type ExperimentRecord,
  type ResultRecord,
  type TheoryRecord,
} from "@/lib/repo";

const STATUS_STYLES: Record<string, string> = {
  completed: "bg-emerald-900/60 text-emerald-200",
  rejected: "bg-zinc-700/70 text-zinc-200",
  "runtime-paused": "bg-amber-900/60 text-amber-200",
  preregistered: "bg-sky-900/60 text-sky-200",
  "support-only": "bg-zinc-800 text-zinc-300",
  proposal: "bg-purple-900/60 text-purple-200",
  draft: "bg-zinc-800 text-zinc-400",
};

const READS_STYLES: Record<string, string> = {
  public: "bg-emerald-900/60 text-emerald-200",
  oracle: "bg-orange-900/60 text-orange-200",
  teacher: "bg-orange-900/60 text-orange-200",
  diagnostic: "bg-zinc-800 text-zinc-300",
};

const OUTCOME_STYLES: Record<string, string> = {
  pass: "bg-emerald-900/60 text-emerald-200",
  fail: "bg-zinc-700/70 text-zinc-200",
  inconclusive: "bg-amber-900/60 text-amber-200",
  "not-applicable": "bg-zinc-800 text-zinc-300",
};

const READS_TEXT: Record<string, string> = {
  public: "reads only what a player can see",
  oracle: "reads hidden values or the future — a teacher, never a policy",
  teacher: "reads hidden values or the future — a teacher, never a policy",
  diagnostic: "a measurement tool, not a policy",
};

export function EvidenceLabel({
  status,
  evidence,
  reads,
}: {
  status?: string;
  evidence?: string;
  reads?: string;
}) {
  return (
    <div className="my-3 flex flex-wrap items-center gap-2 text-xs text-zinc-500">
      {status && <Badge label={status} className={STATUS_STYLES[status] ?? "bg-zinc-800 text-zinc-300"} />}
      {evidence && <Badge label={`evidence: ${evidence}`} />}
      {reads && (
        <>
          <Badge label={reads} className={READS_STYLES[reads] ?? "bg-zinc-800 text-zinc-300"} />
          <span>{READS_TEXT[reads] ?? ""}</span>
        </>
      )}
    </div>
  );
}

export function TechnicalDetails({
  title = "The technical record",
  children,
}: {
  title?: string;
  children: React.ReactNode;
}) {
  return (
    <details className="my-4 rounded-xl border border-zinc-800 bg-zinc-950/40 px-4 py-3">
      <summary className="cursor-pointer text-sm font-semibold text-zinc-300">{title}</summary>
      <div className="mt-2 text-sm text-zinc-300 [&_p]:my-2 [&_table]:my-2">{children}</div>
    </details>
  );
}

function tierSentence(tier: string): string {
  const map: Record<string, string> = {
    proposal: "a proposal with no games played",
    mechanics: "mechanics checks only, no games played",
    pilot: "a pilot — a small run to find bugs and project cost, not a strength claim",
    development: "a development cohort — useful for deciding what to try next, not confirmation",
    "independently-replicated-development": "a development cohort that was independently replicated",
    "protected-validation": "the protected validation cohort",
    "final-confirmation": "the one-shot final cohort",
    CHECK: "mechanics checks only, no games played",
    PILOT: "a pilot — a small run to find bugs and project cost, not a strength claim",
    SCREEN: "a 32-game paired screen",
    STANDARD: "a 64-game paired development cohort",
    QUALIFY: "a 256-game qualification cohort",
    PROTECTED: "the protected validation cohort",
    FINAL: "the one-shot final cohort",
  };
  return map[tier] ?? tier;
}

export function TheorySummary({ id }: { id: string }) {
  const theory = getTheories().find((t) => t.theoryId === id) as TheoryRecord | undefined;
  if (!theory) return <Missing kind="theory" id={id} />;
  return (
    <div className="my-4 rounded-xl border border-zinc-800 bg-zinc-900/50 p-4 text-sm">
      <div className="flex flex-wrap items-center gap-2">
        <Badge label="theory" className="bg-purple-900/60 text-purple-200" />
        <Badge label={theory.assessment} />
        <Badge label={theory.lifecycle} />
        <Link href={`/theories/${theory.theoryId}`} className="text-sky-400 hover:text-sky-300">
          {theory.title}
        </Link>
      </div>
      <p className="mt-2 text-zinc-300">
        <span className="text-zinc-500">Claim: </span>
        {theory.claim}
      </p>
      <p className="mt-1 text-zinc-400">
        This theory is currently <strong className="text-zinc-200">{theory.assessment}</strong> at the{" "}
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
    <div className="my-4 rounded-xl border border-zinc-800 bg-zinc-900/50 p-4 text-sm">
      <div className="flex flex-wrap items-center gap-2">
        <Badge label="experiment" className="bg-sky-900/60 text-sky-200" />
        <Badge label={experiment.lifecycle} />
        <Link href={`/experiments/${experiment.experimentId}`} className="text-sky-400 hover:text-sky-300">
          {experiment.title}
        </Link>
      </div>
      <p className="mt-2 text-zinc-300">
        It compares <strong className="text-zinc-100">{experiment.candidate.name}</strong> against{" "}
        <strong className="text-zinc-100">{experiment.comparator.name}</strong> at the{" "}
        {tierSentence(experiment.benchmarkTier)} level, using {experiment.data.role} data.
      </p>
      {results.length === 0 ? (
        <p className="mt-1 text-zinc-500">No result has been recorded for it.</p>
      ) : (
        results.map((r) => <ResultSentence key={r.resultId} result={r} />)
      )}
    </div>
  );
}

function ResultSentence({ result }: { result: ResultRecord }) {
  return (
    <p className="mt-1 text-zinc-300">
      <Badge
        label={`${result.runValidity} run · ${result.scientificOutcome}`}
        className={OUTCOME_STYLES[result.scientificOutcome] ?? "bg-zinc-800 text-zinc-300"}
      />{" "}
      The run was <strong className="text-zinc-100">{result.runValidity}</strong> and the outcome was{" "}
      <strong className="text-zinc-100">{result.scientificOutcome}</strong> ({tierSentence(result.evidenceTier)}).{" "}
      <Link href={`/experiments/${result.experimentId}#${result.resultId}`} className="text-sky-400 hover:text-sky-300">
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
    <div className="my-4 rounded-xl border border-zinc-800 bg-zinc-900/50 p-4 text-sm">
      <div className="flex flex-wrap items-center gap-2">
        <Badge label="result" />
        <Badge
          label={`${result.runValidity} · ${result.scientificOutcome}`}
          className={OUTCOME_STYLES[result.scientificOutcome] ?? "bg-zinc-800 text-zinc-300"}
        />
        <Badge label={result.evidenceTier} />
        <span className="text-xs text-zinc-600">{result.resultId}</span>
      </div>
      <p className="mt-2 text-zinc-300">
        The run was <strong className="text-zinc-100">{result.runValidity}</strong>; the outcome was{" "}
        <strong className="text-zinc-100">{result.scientificOutcome}</strong>, at the{" "}
        {tierSentence(result.evidenceTier)} level.
        {result.gateChecks.length > 0 && (
          <>
            {" "}
            Of {result.gateChecks.length} preregistered checks, {passed} passed and {failed} failed.
          </>
        )}
      </p>
      <p className="mt-2 text-zinc-400">{result.summary}</p>
      {result.limitations?.length > 0 && (
        <details className="mt-2">
          <summary className="cursor-pointer text-xs text-zinc-500">Limitations recorded with the result</summary>
          <ul className="mt-1 list-disc pl-5 text-zinc-400">
            {result.limitations.map((l, i) => (
              <li key={i}>{l}</li>
            ))}
          </ul>
        </details>
      )}
      <p className="mt-2">
        <Link href={`/experiments/${result.experimentId}#${result.resultId}`} className="text-sky-400 hover:text-sky-300">
          Full record →
        </Link>
      </p>
    </div>
  );
}

function Missing({ kind, id }: { kind: string; id: string }) {
  return (
    <div className="my-4 rounded-xl border border-zinc-800 bg-zinc-950/40 p-3 text-xs text-zinc-500">
      No {kind} record <code>{id}</code> is present in this checkout.
    </div>
  );
}

/** Sentence-form rendering of a result for the record pages. */
export function ResultNarrative({ result }: { result: ResultRecord }) {
  return (
    <div id={result.resultId} className="rounded-xl border border-zinc-800 bg-zinc-950/40 p-4 text-sm">
      <div className="flex flex-wrap items-center gap-2">
        <Badge
          label={`${result.runValidity} run · outcome: ${result.scientificOutcome}`}
          className={OUTCOME_STYLES[result.scientificOutcome] ?? "bg-zinc-800 text-zinc-300"}
        />
        <Badge label={result.assessment} />
        <Badge label={result.evidenceTier} />
        <span className="text-xs text-zinc-600">{result.resultId}</span>
      </div>
      <p className="mt-3 text-zinc-300">{result.summary}</p>
      {result.gateChecks.length > 0 && (
        <div className="mt-3">
          <div className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
            What it had to pass
          </div>
          <ul className="mt-1 space-y-1">
            {result.gateChecks.map((g, i) => (
              <li key={i} className="flex gap-2">
                <span className={g.passed === true ? "text-emerald-400" : g.passed === false ? "text-orange-400" : "text-zinc-500"}>
                  {g.passed === true ? "✓" : g.passed === false ? "✕" : "–"}
                </span>
                <span className="text-zinc-300">
                  {g.criterion}
                  <span className="text-zinc-500"> — observed: {g.observed}</span>
                </span>
              </li>
            ))}
          </ul>
        </div>
      )}
      {Object.keys(result.metrics ?? {}).length > 0 && (
        <details className="mt-3">
          <summary className="cursor-pointer text-xs text-zinc-500">Recorded metrics</summary>
          <table className="mt-1 text-xs">
            <tbody>
              {Object.entries(result.metrics).map(([k, v]) => (
                <tr key={k}>
                  <td className="pr-4 text-zinc-500">{k}</td>
                  <td className="text-zinc-200">{String(v)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </details>
      )}
      {result.limitations?.length > 0 && (
        <div className="mt-3">
          <div className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Limitations</div>
          <ul className="mt-1 list-disc pl-5 text-zinc-400">
            {result.limitations.map((l, i) => (
              <li key={i}>{l}</li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
}
