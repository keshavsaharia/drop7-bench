import "../learn.css";
import { Card } from "@/components/Card";
import { PageHeader } from "@/components/PageHeader";
import { listConceptPages, loadConceptPage, type LearnPageInfo } from "@/lib/learn";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Concepts",
  description:
    "The ideas behind every Drop7 strategy on this site, in reading order, shown on positions the rules engine produced.",
};

/** Concept pages in reading order, leaving out any whose frontmatter says `hidden: true`. */
function visibleConceptPages(): LearnPageInfo[] {
  return listConceptPages().filter(
    (page) => loadConceptPage(page.slug)?.data.hidden !== true,
  );
}

export default function ConceptsIndexPage() {
  const pages = visibleConceptPages();
  return (
    <div className="concepts-index">
      <PageHeader
        crumbs={[{ href: "/learn", label: "learn" }]}
        title="Concepts"
        lead="The ideas behind every strategy on this site, shown on positions the rules engine produced. Read them in order; each page leans on the ones before it."
      />
      {pages.length === 0 ? (
        <p className="learn-empty">
          No concept pages are present in this checkout. They live in{" "}
          <code>web/content/learn/concepts/</code>.
        </p>
      ) : (
        <ol className="concept-list concept-list--index">
          {pages.map((page, index) => (
            <li key={page.slug}>
              <Card
                href={`/learn/concepts/${page.slug}`}
                heading="h2"
                eyebrow={`Concept ${String(index + 1).padStart(2, "0")}`}
                title={page.title}
                summary={page.summary}
              />
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}
