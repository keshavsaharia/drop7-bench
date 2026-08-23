import Link from "next/link";
import { notFound } from "next/navigation";
import { readFileSync } from "node:fs";
import matter from "gray-matter";
import { Mdx } from "@/components/Mdx";
import { Badge } from "@/components/Badge";
import { familyDocPath, listApproaches, listFamilies } from "@/lib/repo";

export const dynamic = "force-dynamic";

export default async function FamilyPage({
  params,
}: {
  params: Promise<{ family: string }>;
}) {
  const { family } = await params;
  if (!listFamilies().includes(family)) notFound();
  const approaches = listApproaches(family);
  const docPath = familyDocPath(family);
  const doc = docPath ? matter(readFileSync(docPath, "utf8")) : null;

  return (
    <div className="space-y-8">
      <div>
        <Link href="/approaches" className="text-sm text-sky-400 hover:text-sky-300">
          ← All families
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">
          {typeof doc?.data.title === "string" ? doc.data.title : family}
        </h1>
        {typeof doc?.data.summary === "string" && (
          <p className="mt-2 max-w-3xl text-zinc-400">{doc.data.summary}</p>
        )}
      </div>

      {doc && <Mdx source={doc.content} fromPath={`approaches/${family}/README.mdx`} />}

      <section>
        <h2 className="mb-3 text-lg font-bold text-zinc-100">
          Approaches in this family
        </h2>
        <div className="grid gap-3 sm:grid-cols-2">
          {approaches.map((approach) => (
            <Link
              key={approach.slug}
              href={`/approaches/${family}/${approach.slug}`}
              className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-4 hover:border-sky-800"
            >
              <div className="flex items-start justify-between gap-2">
                <span className="font-semibold text-zinc-100">{approach.title}</span>
                {approach.draft ? (
                  <Badge label="draft" className="bg-zinc-800 text-zinc-400" />
                ) : approach.hasDocs ? (
                  <Badge label="written" className="bg-emerald-900/60 text-emerald-200" />
                ) : (
                  <Badge label="no docs" />
                )}
              </div>
              {approach.summary ? (
                <p className="mt-1 text-sm text-zinc-400">{approach.summary}</p>
              ) : (
                <div className="mt-1 text-xs text-zinc-500">
                  {approach.sourceFiles.filter((f) => !f.endsWith(".md") && !f.endsWith(".mdx")).join(", ") ||
                    "notes only"}
                </div>
              )}
              {approach.status && <div className="mt-2 text-xs text-zinc-500">{approach.status}</div>}
            </Link>
          ))}
        </div>
      </section>
    </div>
  );
}
