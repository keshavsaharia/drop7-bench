import "../learn.css";
import { VocabularyIndex } from "@/components/VocabularyIndex";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Button } from "@/components/Button";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { extractHeadings } from "@/lib/headings";
import {
  listConceptPages,
  listLearnPages,
  loadConceptPage,
  loadLearnPage,
  type LearnPageInfo,
} from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = loadLearnPage(slug);
  const path = `/learn/${slug}`;
  if (!doc) return pageMetadata({ title: "Learn", path });
  return pageMetadata({
    title: typeof doc.data.title === "string" ? doc.data.title : slug,
    description: typeof doc.data.summary === "string" ? doc.data.summary : undefined,
    path,
  });
}

/** The two guides read in this order; the rules lead into the concepts instead. */
const GUIDE_SLUGS: readonly string[] = ["benchmarking", "protocol"];

interface NavLink {
  href: string;
  title: string;
}

/** Concept pages in reading order, leaving out any whose frontmatter says `hidden: true`. */
function visibleConceptPages(): LearnPageInfo[] {
  return listConceptPages().filter(
    (page) => loadConceptPage(page.slug)?.data.hidden !== true,
  );
}

function neighbours(slug: string): { previous: NavLink | null; next: NavLink | null } {
  if (slug === "rules") {
    const first = visibleConceptPages()[0];
    return {
      previous: null,
      next: first ? { href: `/learn/concepts/${first.slug}`, title: first.title } : null,
    };
  }
  const pages = listLearnPages();
  const guides = GUIDE_SLUGS.map((guide) => pages.find((page) => page.slug === guide)).filter(
    (page): page is LearnPageInfo => page !== undefined,
  );
  const index = guides.findIndex((page) => page.slug === slug);
  if (index < 0) return { previous: null, next: null };
  const toLink = (page: LearnPageInfo): NavLink => ({ href: `/learn/${page.slug}`, title: page.title });
  return {
    previous: index > 0 ? toLink(guides[index - 1]) : null,
    next: index < guides.length - 1 ? toLink(guides[index + 1]) : null,
  };
}

export default async function LearnDocPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const { slug } = await params;
  const doc = loadLearnPage(slug);
  if (!doc) notFound();
  if (slug === "glossary") return <VocabularyIndex />;

  const title = typeof doc.data.title === "string" ? doc.data.title : slug;
  const summary = typeof doc.data.summary === "string" ? doc.data.summary : undefined;
  const headings = extractHeadings(doc.content, { minDepth: 2, maxDepth: 2 });
  const { previous, next } = neighbours(slug);

  return (
    <div className="learn-doc">
      <PageHeader crumbs={[{ href: "/learn", label: "learn" }]} title={title} lead={summary}>
        {slug === "rules" && (
          <Button variant="secondary" href="/play">
            Play the game
          </Button>
        )}
      </PageHeader>
      <ArticleLayout toc={headings}>
        <Mdx source={doc.content} fromPath={`web/content/learn/${slug}.mdx`} />
        {(previous || next) && (
          <nav className="learn-nav" aria-label="Continue reading">
            {previous ? (
              <Button variant="ghost" href={previous.href}>
                ← {previous.title}
              </Button>
            ) : (
              <span />
            )}
            {next && (
              <Button variant="ghost" href={next.href}>
                {next.title} →
              </Button>
            )}
          </nav>
        )}
      </ArticleLayout>
    </div>
  );
}
