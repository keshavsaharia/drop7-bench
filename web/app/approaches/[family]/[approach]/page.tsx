import Link from "next/link";
import { notFound } from "next/navigation";
import { readFileSync } from "node:fs";
import matter from "gray-matter";
import { Mdx } from "@/components/Mdx";
import { Markdown } from "@/components/Markdown";
import { Badge } from "@/components/Badge";
import { approachDocPath, approachOperationalNotes, listApproaches, listFamilies, REPO_ROOT } from "@/lib/repo";
import { relative as relativePath } from "node:path";

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
  const operationalNotes = approachOperationalNotes(family, approach);
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
            <Badge label={`evidence: ${frontmatter.evidence}`} />
          )}
          {typeof frontmatter.reads === "string" && (
            <Badge
              label={frontmatter.reads === "public" ? "public information" : String(frontmatter.reads)}
              className={frontmatter.reads === "public" ? "bg-emerald-900/60 text-emerald-200" : "bg-orange-900/60 text-orange-200"}
            />
          )}
          {frontmatter.draft === true && (
            <Badge label="draft — generated from records, not yet reviewed" className="bg-zinc-800 text-zinc-400" />
          )}
        </div>
        {typeof frontmatter.summary === "string" && (
          <p className="mt-2 max-w-3xl text-zinc-400">{frontmatter.summary}</p>
        )}
      </div>

      {isMdx ? (
        <Mdx source={parsed!.content} fromPath={relativePath(REPO_ROOT, docPath).replaceAll("\\", "/")} />
      ) : (
        <Markdown source={raw} fromPath={relativePath(REPO_ROOT, docPath).replaceAll("\\", "/")} />
      )}

      {operationalNotes && (
        <section className="rounded-xl border border-zinc-800 bg-zinc-950/40 p-4 text-sm text-zinc-400">
          This approach also keeps operational notes — build commands, gate
          commands, and seed leases — in{" "}
          <code className="text-xs">{operationalNotes}</code>, alongside the page
          above.
        </section>
      )}

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
