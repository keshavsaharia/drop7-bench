import Link from "next/link";
import { Badge } from "@/components/Badge";
import { getExperiments, getResults, getTheories } from "@/lib/repo";

export const dynamic = "force-dynamic";

const ASSESSMENT_STYLES: Record<string, string> = {
  "supported-as-tested": "bg-emerald-900/60 text-emerald-200",
  "not-supported-as-tested": "bg-red-900/60 text-red-200",
  mixed: "bg-amber-900/60 text-amber-200",
  untested: "bg-zinc-800 text-zinc-300",
  superseded: "bg-zinc-700/60 text-zinc-300",
  "invalidated-by-methodological-error": "bg-purple-900/60 text-purple-200",
};

export default function TheoriesPage() {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();

  return (
    <div className="space-y-6">
      <div>
        <Link href="/research" className="text-sm text-sky-400 hover:text-sky-300">
          ← Research
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">Theories</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          Registered falsifiable claims from{" "}
          <code className="text-xs">research/theories/</code>. Each theory names
          its mechanism and the criteria that would refute it; experiments and
          results link back to these claims. Historical theories that predate
          the machine-readable registry live in the{" "}
          <Link href="/docs/research/history" className="text-sky-400 hover:text-sky-300">
            experiment ledger
          </Link>{" "}
          and on their{" "}
          <Link href="/approaches" className="text-sky-400 hover:text-sky-300">
            approach pages
          </Link>
          .
        </p>
      </div>

      {theories.length === 0 && (
        <p className="text-zinc-500">No machine-readable theories registered yet.</p>
      )}

      {theories.map((theory) => {
        const linkedExperiments = experiments.filter((experiment) =>
          experiment.theoryIds.includes(theory.theoryId),
        );
        const linkedResults = results.filter((result) =>
          result.theoryIds.includes(theory.theoryId),
        );
        return (
          <article
            key={theory.theoryId}
            className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5"
          >
            <div className="flex flex-wrap items-center gap-2">
              <Badge
                label={theory.assessment}
                className={ASSESSMENT_STYLES[theory.assessment]}
              />
              <Badge label={theory.lifecycle} />
              <Badge label={theory.evidenceTier} />
              <Badge label={theory.informationClass} />
              <span className="text-xs text-zinc-600">{theory.theoryId}</span>
            </div>
            <h2 className="mt-2 text-lg font-bold text-zinc-50">
              <Link href={`/theories/${theory.theoryId}`} className="hover:text-sky-300">{theory.title}</Link>
            </h2>
            <div className="mt-3 grid gap-4 md:grid-cols-2">
              <div>
                <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
                  Claim
                </h3>
                <p className="mt-1 text-sm text-zinc-300">{theory.claim}</p>
              </div>
              <div>
                <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
                  Mechanism
                </h3>
                <p className="mt-1 text-sm text-zinc-300">{theory.mechanism}</p>
              </div>
            </div>
            <div className="mt-3">
              <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
                Falsification criteria
              </h3>
              <ul className="mt-1 list-disc space-y-1 pl-5 text-sm text-zinc-300">
                {theory.falsificationCriteria.map((criterion, index) => (
                  <li key={index}>{criterion}</li>
                ))}
              </ul>
            </div>
            {(linkedExperiments.length > 0 || linkedResults.length > 0) && (
              <div className="mt-4 border-t border-zinc-800 pt-3 text-sm">
                <span className="text-zinc-500">Evidence: </span>
                {linkedExperiments.map((experiment) => (
                  <span key={experiment.experimentId} className="mr-3 text-zinc-300">
                    {experiment.experimentId} ({experiment.lifecycle})
                  </span>
                ))}
                {linkedResults.map((result) => (
                  <span key={result.resultId} className="mr-3">
                    <Badge
                      label={`result: ${result.scientificOutcome}`}
                      className={
                        result.scientificOutcome === "pass"
                          ? ASSESSMENT_STYLES["supported-as-tested"]
                          : result.scientificOutcome === "fail"
                            ? ASSESSMENT_STYLES["not-supported-as-tested"]
                            : ASSESSMENT_STYLES.mixed
                      }
                    />
                  </span>
                ))}
              </div>
            )}
          </article>
        );
      })}
    </div>
  );
}
