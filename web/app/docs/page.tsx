import Link from "next/link";
import { RESEARCH_DOCS } from "@/lib/docs";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Docs · Drop7 Research",
  description:
    "Repository documents for the Drop7 million-point research program: status, methodology, benchmarks, and the experiment ledger.",
};

export default function DocsIndexPage() {
  return (
    <div className="space-y-6">
      <div>
        <Link href="/learn" className="text-sm text-sky-400 hover:text-sky-300">
          ← Learn
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">Docs</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          These documents live in{" "}
          <code className="text-xs">docs/</code>. For a walkthrough with board
          animations, start at{" "}
          <Link href="/learn/rules" className="text-sky-400 hover:text-sky-300">
            how the game works
          </Link>{" "}
          and the{" "}
          <Link href="/learn/concepts" className="text-sky-400 hover:text-sky-300">
            concepts primer
          </Link>
          .
        </p>
      </div>
      <div className="grid gap-4 sm:grid-cols-2">
        {RESEARCH_DOCS.map((doc) => (
          <Link
            key={doc.href}
            href={doc.href}
            className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
          >
            <h2 className="font-bold text-zinc-100">{doc.title}</h2>
            <p className="mt-1 text-sm text-zinc-400">{doc.summary}</p>
          </Link>
        ))}
      </div>
    </div>
  );
}
