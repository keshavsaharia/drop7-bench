import "../research/research.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { Figure } from "@/components/Figure";
import { PageHeader } from "@/components/PageHeader";
import { ContributorChips, OutcomeCounts, TagChips } from "@/components/ResearchLog";
import { formatLogDate, listLogEntries } from "@/lib/log";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";

export const metadata = pageMetadata({
  title: "Research log",
  description:
    "A dated account of what was tried each day in the Drop7 million-point research program, including the things that did not work.",
  path: "/log",
});

export default function LogIndexPage() {
  const entries = listLogEntries();
  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Research log"
        lead="A dated account of what was tried, by whom, and what came of it. A day that ended in rejected ideas is written up in the same detail as a day that produced a result."
      />
      <div className="prose-drop7 log-intro">
        <p>
          The log is narrative. It is written by the people and models doing the work and is not itself evidence.
          Every number in an entry belongs to a record (a <Link href="/theories">theory</Link>, an{" "}
          <Link href="/experiments">experiment</Link> or its <Link href="/results">result</Link>), and those records,
          with their run validity, scientific outcome and evidence tier, are the authority.
        </p>
      </div>

      {entries.length > 0 && (
        <div className="log-figure">
          <Figure
            name="evidence-timeline"
            caption="What each logged day produced, counted from the outcome tallies in the entries' own frontmatter. Negative results outnumber positive ones; recording them is the log's job."
          />
        </div>
      )}

      {entries.length === 0 ? (
        <p className="record-empty">
          No log entries are present in this checkout. Add <code>web/content/log/YYYY-MM-DD.mdx</code> to start one.
        </p>
      ) : (
        <ol className="log-index">
          {entries.map((entry) => {
            const hasMeta = entry.contributors.length > 0 || entry.tags.length > 0 || entry.outcomes !== null;
            return (
              <li key={entry.date}>
                <Card
                  href={`/log/${entry.date}`}
                  heading="h2"
                  eyebrow={<time dateTime={entry.date}>{formatLogDate(entry.date)}</time>}
                  title={entry.title}
                  summary={entry.summary ?? undefined}
                >
                  {hasMeta && (
                    <div className="log-card-meta">
                      <ContributorChips contributors={entry.contributors} />
                      <OutcomeCounts outcomes={entry.outcomes} />
                      <TagChips tags={entry.tags} />
                    </div>
                  )}
                </Card>
              </li>
            );
          })}
        </ol>
      )}
    </div>
  );
}
