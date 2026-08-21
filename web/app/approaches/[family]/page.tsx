import Link from "next/link";
import { notFound } from "next/navigation";
import { readFileSync } from "node:fs";
import matter from "gray-matter";
import { Mdx } from "@/components/Mdx";
import { Badge } from "@/components/Badge";
import {
  approachDocPath,
  familyDocPath,
  listApproaches,
  listFamilies,
} from "@/lib/repo";

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
        <h1 className="mt-1 text-2xl font-black text-zinc-50">{family}</h1>
      </div>

      {doc && <Mdx source={doc.content} />}

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
              <div className="flex items-center justify-between gap-2">
                <span className="font-semibold text-zinc-100">{approach.slug}</span>
                {approach.hasDocs ? (
                  <Badge label="documented" className="bg-emerald-900/60 text-emerald-200" />
                ) : (
                  <Badge label="no docs" />
                )}
              </div>
              <div className="mt-1 text-xs text-zinc-500">
                {approach.sourceFiles.filter((f) => !f.endsWith(".md") && !f.endsWith(".mdx")).join(", ") ||
                  "notes only"}
              </div>
            </Link>
          ))}
        </div>
      </section>
    </div>
  );
}
