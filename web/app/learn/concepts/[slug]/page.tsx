import "../../learn.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { LessonMotionToggle } from "@/components/LearnCards";
import { LessonArt } from "@/components/technique-art/LessonArt";
import { LESSON_GUIDES } from "@/lib/lesson-guides";
import { vocabularyTopic } from "@/lib/vocabulary";
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
  const guide = LESSON_GUIDES[slug];
  const vocabulary = guide ? vocabularyTopic(guide.vocabulary) : null;
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
  ) : (
    <>
      {vocabulary && <div className="aside-block"><span className="label">Vocabulary</span><p className="learn-aside-note"><Link href={`/learn/vocabulary/${vocabulary.slug}`}>{vocabulary.title} →</Link></p></div>}
      <div className="aside-block"><span className="label">Explore the concepts</span>
        <ul className="learn-aside-list">{pages.map((page) => <li key={page.slug}><Link href={`/learn/concepts/${page.slug}`} aria-current={slug === page.slug ? "page" : undefined}>{page.title}</Link></li>)}</ul>
      </div>
    </>
  );

  return (
    <div className="concept-page learn-motion-scope">
      <PageHeader
        crumbs={[
          { href: "/learn", label: "learn the game" },
          { href: "/learn/concepts", label: "concepts" },
        ]}
        title={title}
        lead={summary}
      />
      {guide && <figure className="lesson-overview">
        <div className="lesson-overview-art"><LessonArt name={slug} mode="loop" /></div>
        <figcaption><span className="label">Concept in focus</span><p className="lesson-overview-idea">{guide.idea}</p><p className="lesson-overview-watch">{guide.watch}</p><LessonMotionToggle /></figcaption>
      </figure>}
      <ArticleLayout toc={toc} aside={aside}>
        <Mdx source={doc.content} fromPath={`web/content/learn/concepts/${slug}.mdx`} />
        <nav className="learn-nav learn-nav--cards" aria-label="Other concepts">
          {hidden ? <Button variant="ghost" href="/learn/concepts">← All concepts</Button> : <>
            <Card href={previous ? `/learn/concepts/${previous.slug}` : "/learn/rules"} eyebrow="Previous lesson" title={previous?.title ?? "How to play Drop7"} summary={previous?.summary ?? "Review the discs, clears, and rising board."} />
            <Card href={next ? `/learn/concepts/${next.slug}` : "/learn/techniques"} eyebrow="Continue learning" title={next?.title ?? "Techniques"} summary={next?.summary ?? "Explore the methods used to build a player."} />
          </>}
        </nav>
      </ArticleLayout>
    </div>
  );
}
