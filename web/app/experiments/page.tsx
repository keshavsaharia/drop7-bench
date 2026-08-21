import Link from "next/link";
import { Badge } from "@/components/Badge";
import { RecordedMetrics } from "@/components/Research";
import { getExperiments, getResults } from "@/lib/repo";

export const dynamic = "force-dynamic";

const OUTCOME_STYLES: Record<string, string> = {
  pass: "bg-emerald-900/60 text-emerald-200",
  fail: "bg-red-900/60 text-red-200",
  inconclusive: "bg-amber-900/60 text-amber-200",
  "not-applicable": "bg-zinc-800 text-zinc-300",
};

export default function ExperimentsPage() {
  const experiments = getExperiments();
  const results = getResults();

  return (
    <div className="space-y-6">
      <div>
        <Link href="/research" className="text-sm text-sky-400 hover:text-sky-300">
          ← Research
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">Experiments</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          Preregistered experiment protocols from{" "}
          <code className="text-xs">research/experiments/</code> and their
          recorded results. For the full historical inventory of every approach
          — including the ledger-recorded runs that predate the registry — see
          the{" "}
          <Link href="/approaches" className="text-sky-400 hover:text-sky-300">
            approach pages
          </Link>{" "}
          and the{" "}
          <Link href="/docs/research/experiment-index" className="text-sky-400 hover:text-sky-300">
            experiment index
          </Link>
          .
        </p>
      </div>

      {experiments.map((experiment) => {
        const experimentResults = results.filter(
          (result) => result.experimentId === experiment.experimentId,
        );
        return (
          <article
            key={experiment.experimentId}
            className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5"
          >
            <div className="flex flex-wrap items-center gap-2">
              <Badge label={experiment.lifecycle} className="bg-sky-900/60 text-sky-200" />
              <Badge label={`tier ${experiment.benchmarkTier}`} />
              <Badge label={experiment.classification} />
              <Badge label={experiment.data.role} />
              <span className="text-xs text-zinc-600">{experiment.experimentId}</span>
            </div>
            <h2 className="mt-2 text-lg font-bold text-zinc-50">
              <Link href={`/experiments/${experiment.experimentId}`} className="hover:text-sky-300">{experiment.title}</Link>
            </h2>
            <p className="mt-2 text-sm text-zinc-300">{experiment.hypothesis}</p>
            <div className="mt-3 grid gap-3 text-sm md:grid-cols-2">
              <div className="rounded-lg bg-zinc-950/60 p-3">
                <span className="text-xs uppercase tracking-wide text-zinc-500">Candidate</span>
                <div className="font-semibold text-zinc-100">{experiment.candidate.name}</div>
                {experiment.candidate.entryPoint && (
                  <code className="text-xs text-zinc-500">{experiment.candidate.entryPoint}</code>
                )}
              </div>
              <div className="rounded-lg bg-zinc-950/60 p-3">
                <span className="text-xs uppercase tracking-wide text-zinc-500">Comparator</span>
                <div className="font-semibold text-zinc-100">{experiment.comparator.name}</div>
                {experiment.comparator.entryPoint && (
                  <code className="text-xs text-zinc-500">{experiment.comparator.entryPoint}</code>
                )}
              </div>
            </div>

            {experimentResults.map((result) => (
              <div
                key={result.resultId}
                className="mt-4 rounded-lg border border-zinc-800 bg-zinc-950/40 p-4"
              >
                <div className="flex flex-wrap items-center gap-2">
                  <Badge
                    label={`outcome: ${result.scientificOutcome}`}
                    className={OUTCOME_STYLES[result.scientificOutcome]}
                  />
                  <Badge label={`run: ${result.runValidity}`} />
                  <Badge label={result.evidenceTier} />
                  <span className="text-xs text-zinc-600">{result.resultId}</span>
                </div>
                <p className="mt-2 text-sm text-zinc-300">{result.summary}</p>
                <div className="mt-3 overflow-x-auto">
                  <table className="w-full border-collapse text-xs">
                    <thead>
                      <tr className="text-left text-zinc-500">
                        <th className="py-1 pr-3">Gate criterion</th>
                        <th className="py-1 pr-3">Verdict</th>
                        <th className="py-1">Observed</th>
                      </tr>
                    </thead>
                    <tbody>
                      {result.gateChecks.map((check, index) => (
                        <tr key={index} className="border-t border-zinc-800/60">
                          <td className="py-1 pr-3 text-zinc-300">{check.criterion}</td>
                          <td className="py-1 pr-3">
                            {check.passed === null ? (
                              <span className="text-zinc-500">n/a</span>
                            ) : check.passed ? (
                              <span className="font-semibold text-emerald-400">pass</span>
                            ) : (
                              <span className="font-semibold text-red-400">fail</span>
                            )}
                          </td>
                          <td className="py-1 text-zinc-400">{check.observed}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
                {Object.keys(result.metrics).length > 0 && (
                  <details className="mt-3">
                    <summary className="cursor-pointer text-xs text-zinc-500 hover:text-zinc-300">
                      Raw metrics ({Object.keys(result.metrics).length})
                    </summary>
                    <RecordedMetrics metrics={result.metrics} />
                  </details>
                )}
              </div>
            ))}
          </article>
        );
      })}
    </div>
  );
}
