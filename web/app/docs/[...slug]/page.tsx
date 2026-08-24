import Link from "next/link";
import { notFound } from "next/navigation";
import { existsSync, readFileSync } from "node:fs";
import { join, normalize } from "node:path";
import { Markdown } from "@/components/Markdown";
import { DOCS_DIR } from "@/lib/repo";

export const dynamic = "force-dynamic";

/** Renders a repository Markdown document (docs/**, README, AGENTS) in the app. */
export default async function RepoDocPage({
  params,
}: {
  params: Promise<{ slug: string[] }>;
}) {
  const { slug } = await params;
  const parts = [...slug];
  const last = parts.at(-1);
  if (last) parts[parts.length - 1] = last.replace(/\.mdx?$/i, "");
  const relative = `${parts.join("/")}.md`;
  const path = normalize(join(DOCS_DIR, relative));
  if (!path.startsWith(DOCS_DIR) || !existsSync(path)) notFound();
  const source = readFileSync(path, "utf8");

  return (
    <div className="space-y-6">
      <div>
        <Link href="/docs" className="text-sm text-sky-400 hover:text-sky-300">
          ← Docs
        </Link>
        <div className="mt-1 flex flex-wrap items-baseline justify-between gap-2">
          <span className="text-xs text-zinc-600">docs/{relative}</span>
        </div>
      </div>
      <Markdown source={source} fromPath={`docs/${relative}`} />
      <div className="mt-6 rounded-xl border border-sky-900/60 bg-sky-950/20 p-4 text-sm text-zinc-300">
        For a walkthrough with board animations, start at{" "}
        <Link href="/learn/rules" className="text-sky-400 hover:text-sky-300">
          how the game works
        </Link>{" "}
        and the{" "}
        <Link href="/learn/concepts" className="text-sky-400 hover:text-sky-300">
          concepts primer
        </Link>
        ; every term is defined in the{" "}
        <Link href="/learn/glossary" className="text-sky-400 hover:text-sky-300">
          glossary
        </Link>
        .
      </div>
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
