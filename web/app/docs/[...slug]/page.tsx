import Link from "next/link";
import { notFound } from "next/navigation";
import { existsSync, readFileSync } from "node:fs";
import { join, normalize } from "node:path";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Badge } from "@/components/Badge";
import { Markdown } from "@/components/Markdown";
import { PageHeader } from "@/components/PageHeader";
import { RESEARCH_DOCS } from "@/lib/docs";
import { extractHeadings } from "@/lib/headings";
import { pageMetadata } from "@/lib/metadata";
import { DOCS_DIR } from "@/lib/repo";

export const dynamic = "force-dynamic";

/** The first `# ` heading of a Markdown document, and the source without it. */
function splitTitle(source: string, fallback: string): { title: string; body: string } {
  const match = /^# (.+?)\s*$/m.exec(source);
  if (!match) return { title: fallback, body: source };
  return { title: match[1].trim(), body: source.replace(match[0], "").replace(/^\s+/, "") };
}

export async function generateMetadata({ params }: { params: Promise<{ slug: string[] }> }) {
  const { slug } = await params;
  const parts = slug.map((part, index) => (index === slug.length - 1 ? part.replace(/\.mdx?$/i, "") : part));
  return pageMetadata({
    title: parts.at(-1) ?? "Documents",
    path: `/docs/${parts.join("/")}`,
    // A catch-all segment cannot hold an `opengraph-image` file, so these
    // pages share the documents card instead of losing their preview.
    image: "/docs/opengraph-image",
  });
}

/** Renders a repository Markdown document (docs/**) in the console frame. */
export default async function RepoDocPage({ params }: { params: Promise<{ slug: string[] }> }) {
  const { slug } = await params;
  const parts = [...slug];
  const last = parts.at(-1);
  if (last) parts[parts.length - 1] = last.replace(/\.mdx?$/i, "");
  const relative = `${parts.join("/")}.md`;
  const path = normalize(join(DOCS_DIR, relative));
  if (!path.startsWith(DOCS_DIR) || !existsSync(path)) notFound();

  const source = readFileSync(path, "utf8");
  const { title, body } = splitTitle(source, parts.at(-1) ?? "Document");
  const toc = extractHeadings(body, { minDepth: 2, maxDepth: 2 });
  const agentFacing = parts[0] === "agents";
  const findings = parts[0] === "exploratory";

  const crumbs = [{ href: "/docs", label: "documents" }];
  if (parts.length > 1) {
    crumbs.push({ href: "/docs", label: parts.slice(0, -1).join("/") });
  }

  const aside = (
    <>
      <div className="aside-block">
        <span className="label">Source</span>
        <p className="aside-text">
          <code>docs/{relative}</code>
        </p>
      </div>
      <div className="aside-block">
        <span className="label">Reader documents</span>
        <ul className="aside-list">
          {RESEARCH_DOCS.map((doc) => (
            <li key={doc.href}>
              <Link href={doc.href}>{doc.title}</Link>
            </li>
          ))}
        </ul>
      </div>
    </>
  );

  return (
    <div>
      <PageHeader crumbs={crumbs} title={title}>
        {agentFacing && <Badge label="agent contract" />}
        {findings && <Badge label="finding or audit" />}
        <span className="label">docs/{relative}</span>
      </PageHeader>
      <ArticleLayout toc={toc} aside={aside}>
        <Markdown source={body} fromPath={`docs/${relative}`} />
      </ArticleLayout>
    </div>
  );
}
