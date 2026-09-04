import "../techniques.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Button } from "@/components/Button";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { Badge } from "@/components/Badge";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { extractHeadings } from "@/lib/headings";
import { listTechniquePages, loadTechniquePage } from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";
import { listApproaches, listApproachesByTechnique, type ApproachEntry } from "@/lib/repo";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = loadTechniquePage(slug);
  const path = `/learn/techniques/${slug}`;
  if (!doc) return pageMetadata({ title: "Technique", path });
  return pageMetadata({
    title: typeof doc.data.title === "string" ? doc.data.title : slug,
    description: typeof doc.data.summary === "string" ? doc.data.summary : undefined,
    path,
  });
}

/** Resolve the frontmatter's `family/slug` list to approach entries that exist. */
function listedApproaches(paths: unknown, technique: string | null): ApproachEntry[] {
  const fromFrontmatter = Array.isArray(paths)
    ? paths
        .map((path) => String(path).split("/"))
        .filter((parts) => parts.length === 2)
        .map(([family, slug]) => listApproaches(family).find((entry) => entry.slug === slug) ?? null)
        .filter((entry): entry is ApproachEntry => entry !== null)
    : [];
  if (fromFrontmatter.length > 0) return fromFrontmatter;
  return technique ? listApproachesByTechnique(technique) : [];
}

export default async function TechniquePage({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = loadTechniquePage(slug);
  if (!doc) notFound();

  const technique = typeof doc.data.technique === "string" ? doc.data.technique : slug;
  const catalogue = getTechnique(technique);
  const title = typeof doc.data.title === "string" ? doc.data.title : catalogue?.title ?? slug;
  const summary = typeof doc.data.summary === "string" ? doc.data.summary : catalogue?.oneLine;
  const toc = extractHeadings(doc.content, { minDepth: 2, maxDepth: 2 });
  const approaches = listedApproaches(doc.data.approaches, catalogue?.slug ?? null);

  const pages = listTechniquePages();
  const index = pages.findIndex((page) => page.slug === slug);
  const previous = index > 0 ? pages[index - 1] : null;
  const next = index >= 0 && index < pages.length - 1 ? pages[index + 1] : null;

  const aside = (
    <>
      <div className="aside-block">
        <span className="label">Approaches that use it</span>
        {approaches.length === 0 ? (
          <p className="aside-empty">None documented in this checkout.</p>
        ) : (
          <ul className="aside-list">
            {approaches.map((entry) => (
              <li key={`${entry.family}/${entry.slug}`}>
                <Link href={`/approaches/${entry.family}/${entry.slug}`}>{entry.title}</Link>
                {entry.status && <Badge kind="status" value={entry.status} dot={false} />}
              </li>
            ))}
          </ul>
        )}
      </div>
      {catalogue && (
        <div className="aside-block">
          <span className="label">Group page</span>
          <p>
            <Link href={`/approaches/technique/${catalogue.slug}`}>
              {catalogue.title} approaches
            </Link>
          </p>
        </div>
      )}
    </>
  );

  return (
    <div className="technique-page">
      <PageHeader
        crumbs={[
          { href: "/learn", label: "learn" },
          { href: "/learn/techniques", label: "techniques" },
        ]}
        title={title}
        lead={summary}
      >
        <Badge label="technique primer" />
      </PageHeader>
      <div className="technique-hero fig-frame" aria-hidden="true">
        <TechniqueArt name={technique} mode="loop" title={title} />
      </div>
      <ArticleLayout toc={toc} aside={aside}>
        <Mdx source={doc.content} fromPath={`web/content/learn/techniques/${slug}.mdx`} />
        <nav className="technique-nav" aria-label="Other techniques">
          {previous ? (
            <Button variant="ghost" href={`/learn/techniques/${previous.slug}`}>
              ← {previous.title}
            </Button>
          ) : (
            <span />
          )}
          {next && (
            <Button variant="ghost" href={`/learn/techniques/${next.slug}`}>
              {next.title} →
            </Button>
          )}
        </nav>
      </ArticleLayout>
    </div>
  );
}
