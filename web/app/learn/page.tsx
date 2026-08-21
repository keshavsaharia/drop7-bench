import Link from "next/link";
import { listConceptPages, listLearnPages } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default function LearnPage() {
  const pages = listLearnPages();
  const concepts = listConceptPages();
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-black text-zinc-50">Learn</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          Visual explanations of the game, the benchmark, the policy protocol,
          and the research method. These pages are MDX documents rendered from{" "}
          <code className="text-xs">web/content/learn/</code>.
        </p>
      </div>
      {concepts.length > 0 && (
        <section className="rounded-xl border border-sky-900/70 bg-sky-950/20 p-5">
          <div className="flex flex-wrap items-baseline justify-between gap-2">
            <h2 className="text-lg font-bold text-zinc-100">Concepts — the plain-English primer</h2>
            <Link href="/learn/concepts" className="text-sm text-sky-400 hover:text-sky-300">
              All concepts →
            </Link>
          </div>
          <p className="mt-1 text-sm text-zinc-400">
            The ideas every strategy here is built on, shown on real positions.
            Start after the rules; no background needed.
          </p>
          <ol className="mt-3 grid gap-2 sm:grid-cols-2">
            {concepts.map((page, index) => (
              <li key={page.slug}>
                <Link
                  href={`/learn/concepts/${page.slug}`}
                  className="block rounded-lg border border-zinc-800 bg-zinc-900/60 px-3 py-2 hover:border-sky-800"
                >
                  <span className="text-xs text-zinc-500">{index + 1}. </span>
                  <span className="text-sm font-semibold text-zinc-100">{page.title}</span>
                </Link>
              </li>
            ))}
          </ol>
        </section>
      )}
      <div className="grid gap-4 sm:grid-cols-2">
        {pages.map((page) => (
          <Link
            key={page.slug}
            href={`/learn/${page.slug}`}
            className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
          >
            <h2 className="font-bold text-zinc-100">{page.title}</h2>
            <p className="mt-1 text-sm text-zinc-400">{page.summary}</p>
          </Link>
        ))}
      </div>
    </div>
  );
}
