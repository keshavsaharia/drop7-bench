import Link from "next/link";
import { RESEARCH_DOCS } from "@/lib/docs";
import { listConceptPages, listLearnPages } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default function LearnPage() {
  const pages = listLearnPages();
  const concepts = listConceptPages();
  const rules = pages.find((page) => page.slug === "rules");
  const guides = pages.filter((page) => page.slug !== "rules");

  return (
    <div className="space-y-8">
      <div>
        <h1 className="text-2xl font-black text-zinc-50">Learn</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          The game, the ideas behind every strategy here, and the repository
          documents the research is bound to. Start with the rules if you have
          not played; the concepts primer is next.
        </p>
      </div>

      {rules && (
        <Link
          href="/learn/rules"
          className="group block rounded-xl border border-sky-900/70 bg-sky-950/20 p-5 hover:border-sky-700"
        >
          <div className="text-xs font-semibold uppercase tracking-wide text-sky-300">
            The game
          </div>
          <h2 className="mt-1 text-lg font-bold text-zinc-50 group-hover:text-white">
            {rules.title}
          </h2>
          <p className="mt-1 text-sm text-zinc-400">{rules.summary}</p>
        </Link>
      )}

      {concepts.length > 0 && (
        <section>
          <div className="flex flex-wrap items-baseline justify-between gap-2">
            <h2 className="text-lg font-bold text-zinc-100">Concepts</h2>
            <Link href="/learn/concepts" className="text-sm text-sky-400 hover:text-sky-300">
              All concepts →
            </Link>
          </div>
          <p className="mt-1 text-sm text-zinc-400">
            The ideas every strategy here is built on, shown on real positions.
            Read these in order after the rules.
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

      {guides.length > 0 && (
        <section>
          <h2 className="text-lg font-bold text-zinc-100">Guides</h2>
          <p className="mt-1 text-sm text-zinc-400">
            Visual explanations of the benchmark, the policy protocol, and the
            vocabulary the rest of the console uses.
          </p>
          <div className="mt-3 grid gap-4 sm:grid-cols-2">
            {guides.map((page) => (
              <Link
                key={page.slug}
                href={`/learn/${page.slug}`}
                className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
              >
                <h3 className="font-bold text-zinc-100">{page.title}</h3>
                <p className="mt-1 text-sm text-zinc-400">{page.summary}</p>
              </Link>
            ))}
          </div>
        </section>
      )}

      <section>
        <div className="flex flex-wrap items-baseline justify-between gap-2">
          <h2 className="text-lg font-bold text-zinc-100">Docs</h2>
          <Link href="/docs" className="text-sm text-sky-400 hover:text-sky-300">
            All docs →
          </Link>
        </div>
        <p className="mt-1 text-sm text-zinc-400">
          Repository documents written for researchers. They live in{" "}
          <code className="text-xs">docs/</code> and are the authority for
          status, method, and historical record.
        </p>
        <div className="mt-3 grid gap-4 sm:grid-cols-2">
          {RESEARCH_DOCS.map((doc) => (
            <Link
              key={doc.href}
              href={doc.href}
              className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
            >
              <h3 className="font-bold text-zinc-100">{doc.title}</h3>
              <p className="mt-1 text-sm text-zinc-400">{doc.summary}</p>
            </Link>
          ))}
        </div>
      </section>
    </div>
  );
}
