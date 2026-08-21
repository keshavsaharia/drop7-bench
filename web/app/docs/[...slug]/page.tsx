import Link from "next/link";
import { notFound } from "next/navigation";
import { existsSync, readFileSync } from "node:fs";
import { join, normalize } from "node:path";
import { Markdown } from "@/components/Markdown";
import { DOCS_DIR, REPO_ROOT } from "@/lib/repo";

export const dynamic = "force-dynamic";

/** Renders a repository Markdown document (docs/**, README, AGENTS) in the app. */
export default async function RepoDocPage({
  params,
}: {
  params: Promise<{ slug: string[] }>;
}) {
  const { slug } = await params;
  const relative = `${slug.join("/")}.md`;
  const path = normalize(join(DOCS_DIR, relative));
  if (!path.startsWith(DOCS_DIR) || !existsSync(path)) notFound();
  const source = readFileSync(path, "utf8");
  const title = source.match(/^#\s+(.+)$/m)?.[1] ?? slug.join("/");

  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-baseline justify-between gap-2">
        <h1 className="text-2xl font-black text-zinc-50">{title}</h1>
        <span className="text-xs text-zinc-600">docs/{relative}</span>
      </div>
      <Markdown source={source} />
      <div className="border-t border-zinc-800 pt-4 text-sm text-zinc-500">
        <Link href="/docs/research/status" className="text-sky-400 hover:text-sky-300">
          Status
        </Link>
        {" · "}
        <Link href="/docs/methodology" className="text-sky-400 hover:text-sky-300">
          Methodology
        </Link>
        {" · "}
        <Link href="/docs/benchmarks" className="text-sky-400 hover:text-sky-300">
          Benchmark contract
        </Link>
        {" · "}
        <Link href="/docs/strategies" className="text-sky-400 hover:text-sky-300">
          Strategy landscape
        </Link>
        {" · "}
        <Link href="/docs/research/experiment-index" className="text-sky-400 hover:text-sky-300">
          Experiment index
        </Link>
        {" · "}
        <Link href="/docs/research/history" className="text-sky-400 hover:text-sky-300">
          Full ledger
        </Link>
        {" · "}
        <Link href="/docs/d7p-protocol" className="text-sky-400 hover:text-sky-300">
          D7P protocol
        </Link>
      </div>
    </div>
  );
}
