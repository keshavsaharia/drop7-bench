import "../../research/research.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { ContributorChips, OutcomeCounts, TagChips } from "@/components/ResearchLog";
import {
  formatLogDate,
  getLogEntryInfo,
  getLogNeighbors,
  listLogDates,
  loadLogEntry,
} from "@/lib/log";
import { pageMetadata } from "@/lib/metadata";

/**
 * Dates present at build time are prerendered; `dynamicParams` stays at its
 * default so an entry added afterwards still renders on request. A checkout
 * with no web/content/log/ prerenders nothing and builds normally.
 */
export async function generateStaticParams() {
  return listLogDates().map((date) => ({ date }));
}

type Params = Promise<{ date: string }>;

export async function generateMetadata({ params }: { params: Params }) {
  const { date } = await params;
  const info = getLogEntryInfo(date);
  return pageMetadata({
    title: info ? `${info.title} (${info.date})` : "Research log",
    description: info?.summary ?? undefined,
    path: `/log/${date}`,
  });
}

export default async function LogEntryPage({ params }: { params: Params }) {
  const { date } = await params;
  const doc = loadLogEntry(date);
  const info = getLogEntryInfo(date);
  if (!doc || !info) notFound();
  const { older, newer } = getLogNeighbors(date);

  return (
    <div>
      <PageHeader
        crumbs={[
          { href: "/research", label: "research" },
          { href: "/log", label: "log" },
        ]}
        title={info.title}
        lead={info.summary ?? undefined}
      >
        <time className="label" dateTime={info.date}>
          {formatLogDate(info.date)}
        </time>
        <ContributorChips contributors={info.contributors} />
        <OutcomeCounts outcomes={info.outcomes} />
        <TagChips tags={info.tags} />
      </PageHeader>

      <ArticleLayout>
        <Mdx source={doc.content} />

        <nav className="log-entry-nav" aria-label="Neighbouring entries">
          {older ? (
            <Link href={`/log/${older.date}`}>
              ← {formatLogDate(older.date)}: {older.title}
            </Link>
          ) : (
            <span>Earliest entry</span>
          )}
          {newer ? (
            <Link href={`/log/${newer.date}`}>
              {formatLogDate(newer.date)}: {newer.title} →
            </Link>
          ) : (
            <span>Latest entry</span>
          )}
        </nav>

        <p className="log-entry-foot">
          A log entry is a narrative written by the contributors listed above. Run validity, scientific outcome and
          evidence tier live with the <Link href="/experiments">experiment</Link> and{" "}
          <Link href="/results">result</Link> records the entry refers to.
        </p>
      </ArticleLayout>
    </div>
  );
}
