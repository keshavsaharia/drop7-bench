import Link from "next/link";
import { notFound } from "next/navigation";
import { Badge } from "@/components/Badge";
import { Mdx } from "@/components/Mdx";
import { ResultNarrative } from "@/components/Research";
import { loadRecordOverlay } from "@/lib/research";
import { getExperiments, getResults, getTheories } from "@/lib/repo";

export const dynamic = "force-dynamic";

export default async function ExperimentPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const experiment = getExperiments().find((e) => e.experimentId === id);
  if (!experiment) notFound();
  const overlay = loadRecordOverlay(id);
  const results = getResults().filter((r) => r.experimentId === id);
  const theories = getTheories().filter((t) => experiment.theoryIds.includes(t.theoryId));

  return (
    <div className="space-y-8">
      <div>
        <Link href="/experiments" className="text-sm text-sky-400 hover:text-sky-300">
          ← Experiments
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">{experiment.title}</h1>
        <div className="mt-2 flex flex-wrap items-center gap-2">
          <Badge label={experiment.lifecycle} className="bg-sky-900/60 text-sky-200" />
          <Badge label={`tier ${experiment.benchmarkTier}`} />
          <Badge label={experiment.classification} />
          <Badge label={experiment.data.role} />
          <Badge label={experiment.informationBoundary} />
          <span className="text-xs text-zinc-600">{experiment.experimentId}</span>
        </div>
      </div>

      {overlay ? (
        <Mdx source={overlay.content} />
      ) : (
        <div className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-4 text-sm text-zinc-400">
          No explanation has been written for this experiment yet. Add{" "}
          <code className="text-xs">web/content/research/{experiment.experimentId}.mdx</code> and it will appear
          here. The registered protocol is shown below.
        </div>
      )}

      <section className="rounded-xl border border-zinc-800 p-5 text-sm">
        <h2 className="text-sm font-semibold uppercase tracking-wide text-zinc-500">The registered protocol</h2>
        <p className="mt-2 text-zinc-300">{experiment.hypothesis}</p>
        <div className="mt-3 grid gap-3 md:grid-cols-2">
          <div className="rounded-lg bg-zinc-950/60 p-3">
            <span className="text-xs uppercase tracking-wide text-zinc-500">Candidate</span>
            <div className="font-semibold text-zinc-100">{experiment.candidate.name}</div>
            {experiment.candidate.entryPoint && <code className="text-xs text-zinc-500">{experiment.candidate.entryPoint}</code>}
          </div>
          <div className="rounded-lg bg-zinc-950/60 p-3">
            <span className="text-xs uppercase tracking-wide text-zinc-500">Comparator</span>
            <div className="font-semibold text-zinc-100">{experiment.comparator.name}</div>
            {experiment.comparator.entryPoint && <code className="text-xs text-zinc-500">{experiment.comparator.entryPoint}</code>}
          </div>
        </div>
        <div className="mt-3 grid gap-3 md:grid-cols-2">
          <div>
            <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Primary metric</h3>
            <p className="mt-1 text-zinc-300">{experiment.metrics.primary}</p>
            <p className="mt-1 text-zinc-500">Statistical unit: {experiment.metrics.statisticalUnit}</p>
          </div>
          <div>
            <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Pass criteria, fixed in advance</h3>
            <ul className="mt-1 list-disc space-y-1 pl-5 text-zinc-300">
              {experiment.gate.passCriteria.map((c, i) => (
                <li key={i}>{c}</li>
              ))}
            </ul>
            <p className="mt-1 text-zinc-500">On pass: {experiment.gate.passAction}</p>
            <p className="text-zinc-500">On fail: {experiment.gate.failureAction}</p>
          </div>
        </div>
        <div className="mt-3">
          <h3 className="text-xs font-semibold uppercase tracking-wide text-zinc-500">Data and reuse</h3>
          <p className="mt-1 text-zinc-400">{experiment.data.reuseDisclosure}</p>
          <p className="mt-1 text-xs text-zinc-600">seed leases: {experiment.data.seedLeaseRefs.join(", ") || "none"}</p>
        </div>
        {theories.length > 0 && (
          <div className="mt-3">
            <span className="text-zinc-500">Tests theory: </span>
            {theories.map((t) => (
              <Link key={t.theoryId} href={`/theories/${t.theoryId}`} className="mr-3 text-sky-400 hover:text-sky-300">
                {t.title}
              </Link>
            ))}
          </div>
        )}
      </section>

      <section id="results">
        <h2 className="text-lg font-bold text-zinc-100">What happened</h2>
        {results.length === 0 ? (
          <p className="mt-1 text-sm text-zinc-500">No result has been recorded for this experiment.</p>
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
