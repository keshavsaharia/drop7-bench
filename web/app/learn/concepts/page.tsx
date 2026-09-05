import "../learn.css";
import { LessonMotionToggle, ConceptCards } from "@/components/LearnCards";
import { PageHeader } from "@/components/PageHeader";
import { listConceptPages } from "@/lib/learn";
import { CONCEPTS_DESCRIPTION } from "@/lib/lesson-guides";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";
export const metadata = pageMetadata({ title: "Concepts", description: CONCEPTS_DESCRIPTION, path: "/learn/concepts" });

export default function ConceptsIndexPage() {
  const pages = listConceptPages().filter((page) => !page.hidden);
  return <div className="concepts-index learn-motion-scope">
    <PageHeader crumbs={[{ href: "/learn", label: "learn the game" }]} title="Concepts" lead={CONCEPTS_DESCRIPTION} />
    <div className="learn-tools"><LessonMotionToggle /></div>
      {pages.length ? <ConceptCards pages={pages} heading="h2" /> : <p className="learn-empty">Concept lessons will appear here when available.</p>}
  </div>;
}
