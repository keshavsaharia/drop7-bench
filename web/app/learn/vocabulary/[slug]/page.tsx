import "../../learn.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Button } from "@/components/Button";
import { LessonMotionToggle } from "@/components/LearnCards";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { LessonArt } from "@/components/technique-art/LessonArt";
import { pageMetadata } from "@/lib/metadata";
import { VOCABULARY_TOPICS, vocabularyTerms, vocabularyTopic } from "@/lib/vocabulary";

export const dynamic = "force-dynamic";
type Props = { params: Promise<{ slug: string }> };
export async function generateMetadata({ params }: Props) {
  const { slug } = await params;
  const topic = vocabularyTopic(slug);
  return pageMetadata({ title: topic?.title ?? "Vocabulary", description: topic?.summary, path: `/learn/vocabulary/${slug}` });
}

export default async function VocabularyTopicPage({ params }: Props) {
  const { slug } = await params;
  const topic = vocabularyTopic(slug);
  if (!topic) notFound();
  const terms = vocabularyTerms(slug);
  return <div className="vocabulary-page learn-motion-scope">
    <PageHeader crumbs={[{ href: "/learn", label: "learn the game" }, { href: "/learn/vocabulary", label: "vocabulary" }]} title={topic.title} lead={topic.summary} />
    <nav className="learn-topic-nav" aria-label="Vocabulary topics">
      {VOCABULARY_TOPICS.map((item) => <Link key={item.slug} href={`/learn/vocabulary/${item.slug}`} aria-current={item.slug === slug ? "page" : undefined}>{item.title}</Link>)}
    </nav>
    <ArticleLayout toc={terms.map((term) => ({ id: term.id, text: term.title, depth: 2 }))} tocTitle="Terms on this page"
      aside={<div className="aside-block"><span className="label">See it in action</span><p className="learn-aside-note"><Link href={`/learn/concepts/${topic.lesson}`}>Explore a related lesson →</Link></p></div>}>
      <div className="learn-tools"><LessonMotionToggle /></div>
      <div className="vocabulary-illustration"><LessonArt name={topic.art} mode="loop" /></div>
      {terms.length ? <dl className="vocabulary-definitions">{terms.map((term) => <div key={term.id} id={term.id} className="vocabulary-term">
        <dt><a href={`#${term.id}`} aria-label={`Link to ${term.title}`}>{term.title}<span aria-hidden="true">#</span></a></dt>
        <dd><Mdx source={term.meaning} fromPath="web/content/learn/glossary.mdx" /></dd>
      </div>)}</dl> : <p className="learn-empty">Definitions for this topic will appear here when available.</p>}
      <nav className="learn-nav" aria-label="Continue learning"><Button variant="ghost" href="/learn/vocabulary">← All vocabulary</Button><Button variant="secondary" href={`/learn/concepts/${topic.lesson}`}>Explore the concept →</Button></nav>
    </ArticleLayout>
  </div>;
}
