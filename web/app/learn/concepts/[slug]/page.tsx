import Link from "next/link";
import { notFound } from "next/navigation";
import { Mdx } from "@/components/Mdx";
import { listConceptPages, loadConceptPage } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default async function ConceptPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const { slug } = await params;
  const doc = loadConceptPage(slug);
  if (!doc) notFound();
  const pages = listConceptPages();
  const index = pages.findIndex((page) => page.slug === slug);
  const previous = index > 0 ? pages[index - 1] : null;
  const next = pages[index + 1];

  return (
    <div className="space-y-6">
      <div>
        <Link href="/learn/concepts" className="text-sm text-sky-400 hover:text-sky-300">
          ← Concepts
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">
          {(doc.data.title as string) ?? slug}
        </h1>
        {typeof doc.data.summary === "string" && (
          <p className="mt-1 max-w-3xl text-sm text-zinc-400">{doc.data.summary}</p>
        )}
      </div>
      <Mdx source={doc.content} />
      <div className="flex flex-wrap justify-between gap-3 border-t border-zinc-800 pt-4 text-sm">
        {previous ? (
          <Link href={`/learn/concepts/${previous.slug}`} className="text-sky-400 hover:text-sky-300">
            ← {previous.title}
          </Link>
        ) : (
          <span />
        )}
        {next && (
          <Link href={`/learn/concepts/${next.slug}`} className="text-sky-400 hover:text-sky-300">
            Next: {next.title} →
          </Link>
        )}
      </div>
    </div>
  );
}
