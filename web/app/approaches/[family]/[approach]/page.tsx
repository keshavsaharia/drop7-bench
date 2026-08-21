import Link from "next/link";
import { notFound } from "next/navigation";
import { readFileSync } from "node:fs";
import matter from "gray-matter";
import { Mdx } from "@/components/Mdx";
import { Markdown } from "@/components/Markdown";
import { Badge } from "@/components/Badge";
import { approachDocPath, listApproaches, listFamilies } from "@/lib/repo";

export const dynamic = "force-dynamic";

export default async function ApproachPage({
  params,
}: {
  params: Promise<{ family: string; approach: string }>;
}) {
  const { family, approach } = await params;
  if (!listFamilies().includes(family)) notFound();
  const entry = listApproaches(family).find((a) => a.slug === approach);
  if (!entry) notFound();

  const docPath = approachDocPath(family, approach);
  if (!docPath) {
    return (
      <div className="space-y-4">
        <Link
          href={`/approaches/${family}`}
          className="text-sm text-sky-400 hover:text-sky-300"
        >
          ← {family}
        </Link>
        <h1 className="text-2xl font-black text-zinc-50">{approach}</h1>
        <p className="text-sm text-zinc-400">
          This approach has no documentation yet. Add a{" "}
          <code className="text-xs">README.mdx</code> to{" "}
          <code className="text-xs">
            approaches/{family}/{approach}/
          </code>{" "}
          and it will appear here.
        </p>
        <div className="rounded-xl border border-zinc-800 p-4">
          <h2 className="text-sm font-semibold text-zinc-200">Source files</h2>
          <ul className="mt-2 space-y-1 text-sm text-zinc-400">
            {entry.sourceFiles.map((file) => (
              <li key={file}>
                <code className="text-xs">{file}</code>
              </li>
            ))}
          </ul>
        </div>
      </div>
    );
  }

  const raw = readFileSync(docPath, "utf8");
  const isMdx = docPath.endsWith(".mdx");
  const parsed = isMdx ? matter(raw) : null;
  const frontmatter = parsed?.data ?? {};

  return (
    <div className="space-y-6">
      <div>
        <Link
          href={`/approaches/${family}`}
          className="text-sm text-sky-400 hover:text-sky-300"
        >
          ← {family}
        </Link>
        <div className="mt-1 flex flex-wrap items-center gap-2">
          <h1 className="text-2xl font-black text-zinc-50">
            {(frontmatter.title as string) ?? approach}
          </h1>
          {typeof frontmatter.status === "string" && (
            <Badge label={frontmatter.status} className="bg-sky-900/60 text-sky-200" />
          )}
          {typeof frontmatter.evidence === "string" && (
            <Badge label={frontmatter.evidence} />
          )}
        </div>
      </div>

      {isMdx ? <Mdx source={parsed!.content} /> : <Markdown source={raw} />}

      <section className="rounded-xl border border-zinc-800 p-4">
        <h2 className="text-sm font-semibold text-zinc-200">Source files</h2>
        <ul className="mt-2 flex flex-wrap gap-2 text-sm text-zinc-400">
          {entry.sourceFiles.map((file) => (
            <li key={file} className="rounded bg-zinc-900 px-2 py-0.5">
              <code className="text-xs">{file}</code>
            </li>
          ))}
        </ul>
      </section>
    </div>
  );
}
