import "../../learn.css";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Button } from "@/components/Button";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { extractHeadings } from "@/lib/headings";
import { listConceptPages, loadConceptPage, type LearnPageInfo } from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = loadConceptPage(slug);
  const path = `/learn/concepts/${slug}`;
  if (!doc) return pageMetadata({ title: "Concept", path });
  return pageMetadata({
    title: typeof doc.data.title === "string" ? doc.data.title : slug,
    description: typeof doc.data.summary === "string" ? doc.data.summary : undefined,
    path,
  });
}

/** Concept pages in reading order, leaving out any whose frontmatter says `hidden: true`. */
function visibleConceptPages(): LearnPageInfo[] {
  return listConceptPages().filter(
    (page) => loadConceptPage(page.slug)?.data.hidden !== true,
  );
}

export default async function ConceptPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const { slug } = await params;
  const doc = loadConceptPage(slug);
  if (!doc) notFound();

  const title = typeof doc.data.title === "string" ? doc.data.title : slug;
  const summary = typeof doc.data.summary === "string" ? doc.data.summary : undefined;
  const hidden = doc.data.hidden === true;
  const toc = extractHeadings(doc.content, { minDepth: 2, maxDepth: 2 });

  // A hidden page keeps its URL but sits outside the reading order, so it
  // gets a way back to the list instead of a previous and next.
  const pages = visibleConceptPages();
  const index = pages.findIndex((page) => page.slug === slug);
  const previous = index > 0 ? pages[index - 1] : null;
  const next = index >= 0 && index < pages.length - 1 ? pages[index + 1] : null;

  const aside = hidden ? (
    <div className="aside-block">
      <span className="label">About this page</span>
      <p className="learn-aside-note">
        A registered proposal rather than one of the concepts. It stays at this address and
        is linked from the research pages.
      </p>
    </div>
  ) : undefined;

  return (
    <div className="concept-page">
      <PageHeader
        crumbs={[
          { href: "/learn", label: "learn" },
          { href: "/learn/concepts", label: "concepts" },
        ]}
        title={title}
        lead={summary}
      />
      <ArticleLayout toc={toc} aside={aside}>
        <Mdx source={doc.content} fromPath={`web/content/learn/concepts/${slug}.mdx`} />
        <nav className="learn-nav" aria-label="Other concepts">
          {hidden ? (
            <Button variant="ghost" href="/learn/concepts">
              ← All concepts
            </Button>
          ) : (
            <>
              {previous ? (
                <Button variant="ghost" href={`/learn/concepts/${previous.slug}`}>
                  ← {previous.title}
                </Button>
              ) : (
                <Button variant="ghost" href="/learn/rules">
                  ← How Drop7 works
                </Button>
              )}
              {next ? (
                <Button variant="ghost" href={`/learn/concepts/${next.slug}`}>
                  {next.title} →
                </Button>
              ) : (
                <Button variant="ghost" href="/learn/techniques">
                  Techniques →
                </Button>
              )}
            </>
          )}
        </nav>
      </ArticleLayout>
    </div>
  );
}
