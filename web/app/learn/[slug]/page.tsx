import Link from "next/link";
import { notFound } from "next/navigation";
import { Mdx } from "@/components/Mdx";
import { listLearnPages, loadLearnPage } from "@/lib/learn";

export const dynamic = "force-dynamic";

export default async function LearnDocPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const { slug } = await params;
  const doc = loadLearnPage(slug);
  if (!doc) notFound();
  const pages = listLearnPages();
  const index = pages.findIndex((page) => page.slug === slug);
  const next = pages[index + 1];

  return (
    <div className="space-y-6">
      <div>
        <Link href="/learn" className="text-sm text-sky-400 hover:text-sky-300">
          ← Learn
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">
          {(doc.data.title as string) ?? slug}
        </h1>
      </div>
      <Mdx source={doc.content} />
      {next && (
        <div className="border-t border-zinc-800 pt-4">
          <Link
            href={`/learn/${next.slug}`}
            className="text-sm text-sky-400 hover:text-sky-300"
          >
            Next: {next.title} →
          </Link>
        </div>
      )}
    </div>
  );
}
