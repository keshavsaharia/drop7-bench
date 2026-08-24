import Link from "next/link";
import { notFound } from "next/navigation";
import { Badge } from "@/components/Badge";
import { Mdx } from "@/components/Mdx";
import { ResultNarrative } from "@/components/Research";
import { loadRecordOverlay } from "@/lib/research";
import { getExperiments, getResults, getTheories } from "@/lib/repo";

export const dynamic = "force-dynamic";

export default async function TheoryPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const theory = getTheories().find((t) => t.theoryId === id);
  if (!theory) notFound();
  const overlay = loadRecordOverlay(id);
  const experiments = getExperiments().filter((e) => e.theoryIds.includes(id));
  const results = getResults().filter((r) => r.theoryIds.includes(id));
  const dependencies = theory.dependencies ?? [];

  return (
    <div className="space-y-8">
      <div>
        <Link href="/theories" className="text-sm text-sky-400 hover:text-sky-300">
          ← Theories
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">{theory.title}</h1>
        <div className="mt-2 flex flex-wrap items-center gap-2">
          <Badge label={theory.assessment} />
          <Badge label={theory.lifecycle} />
          <Badge label={`evidence: ${theory.evidenceTier}`} />
          <Badge label={theory.informationClass} />
          <span className="text-xs text-zinc-600">{theory.theoryId}</span>
        </div>
      </div>

      {overlay ? (
        <Mdx source={overlay.content} />
      ) : (
        <div className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-4 text-sm text-zinc-400">
          No explanation has been written for this theory yet. Add{" "}
          <code className="text-xs">web/content/research/{theory.theoryId}.mdx</code> and it will appear here.
          The registered record is shown below.
        </div>
      )}

      <section className="rounded-xl border border-zinc-800 p-5">
        <h2 className="text-sm font-semibold uppercase tracking-wide text-zinc-500">The registered record</h2>
        <div className="mt-3 grid gap-4 md:grid-cols-2 text-sm">
          <div>
            <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Claim</h3>
            <p className="mt-1 text-zinc-300">{theory.claim}</p>
          </div>
          <div>
            <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Mechanism</h3>
            <p className="mt-1 text-zinc-300">{theory.mechanism}</p>
          </div>
        </div>
        <div className="mt-4">
          <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">What would prove it wrong</h3>
          <ul className="mt-1 list-disc space-y-1 pl-5 text-sm text-zinc-300">
            {theory.falsificationCriteria.map((c, i) => (
              <li key={i}>{c}</li>
            ))}
          </ul>
        </div>
        {dependencies.length > 0 && (
          <div className="mt-4 text-sm">
            <span className="text-zinc-500">Builds on: </span>
            {dependencies.map((d) => (
              <Link key={d} href={`/theories/${d}`} className="mr-3 text-sky-400 hover:text-sky-300">
                {d}
              </Link>
            ))}
          </div>
        )}
        <div className="mt-2 text-xs text-zinc-600">
          registered {theory.createdAt} by {theory.createdBy.platform} / {theory.createdBy.model}
        </div>
      </section>

      <section>
        <h2 className="text-lg font-bold text-zinc-100">Experiments that test it</h2>
        {experiments.length === 0 ? (
          <p className="mt-1 text-sm text-zinc-500">None registered yet.</p>
        ) : (
          <ul className="mt-2 space-y-2">
            {experiments.map((e) => (
              <li key={e.experimentId} className="rounded-lg border border-zinc-800 bg-zinc-900/40 p-3 text-sm">
                <Link href={`/experiments/${e.experimentId}`} className="font-semibold text-sky-400 hover:text-sky-300">
                  {e.title}
                </Link>
                <div className="mt-1 text-zinc-400">
                  {e.candidate.name} vs {e.comparator.name} · {e.benchmarkTier} · {e.lifecycle}
                </div>
              </li>
            ))}
          </ul>
        )}
      </section>

      <section>
        <h2 className="text-lg font-bold text-zinc-100">Results recorded against it</h2>
        {results.length === 0 ? (
          <p className="mt-1 text-sm text-zinc-500">No results yet.</p>
        ) : (
          <div className="mt-2 space-y-3">
            {results.map((r) => (
              <ResultNarrative key={r.resultId} result={r} />
            ))}
          </div>
        )}
      </section>
    </div>
  );
}
