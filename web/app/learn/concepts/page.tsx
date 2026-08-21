import Link from "next/link";
import { listConceptPages } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default function ConceptsIndexPage() {
  const pages = listConceptPages();
  return (
    <div className="space-y-6">
      <div>
        <Link href="/learn" className="text-sm text-sky-400 hover:text-sky-300">
          ← Learn
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">Concepts</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          The ideas behind every strategy in this repository, explained in plain
          language with animations computed by the rules engine. Read these in
          order and the research pages will make sense.
        </p>
      </div>
      <ol className="grid gap-4 sm:grid-cols-2">
        {pages.map((page, index) => (
          <li key={page.slug}>
            <Link
              href={`/learn/concepts/${page.slug}`}
              className="block h-full rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
            >
              <div className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
                Concept {index + 1}
              </div>
              <h2 className="mt-1 font-bold text-zinc-100">{page.title}</h2>
              <p className="mt-1 text-sm text-zinc-400">{page.summary}</p>
            </Link>
          </li>
        ))}
      </ol>
    </div>
  );
}
