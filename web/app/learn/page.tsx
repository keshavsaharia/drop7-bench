import Link from "next/link";
import { listLearnPages } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default function LearnPage() {
  const pages = listLearnPages();
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
