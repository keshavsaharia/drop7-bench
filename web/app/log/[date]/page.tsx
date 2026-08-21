import Link from "next/link";
import { notFound } from "next/navigation";
import { Mdx } from "@/components/Mdx";
import { ContributorChips, OutcomeCounts, TagChips } from "@/components/ResearchLog";
import {
  formatLogDate,
  getLogEntryInfo,
  getLogNeighbors,
  listLogDates,
  loadLogEntry,
} from "@/lib/log";

/**
 * Dates present at build time are prerendered; `dynamicParams` stays at its
 * default so an entry added afterwards still renders on request. A checkout
 * with no web/content/log/ prerenders nothing and builds normally.
 */
export async function generateStaticParams() {
  return listLogDates().map((date) => ({ date }));
}

export default async function LogEntryPage({
  params,
}: {
  params: Promise<{ date: string }>;
}) {
  const { date } = await params;
  const doc = loadLogEntry(date);
  const info = getLogEntryInfo(date);
  if (!doc || !info) notFound();
  const { older, newer } = getLogNeighbors(date);

  return (
    <div className="space-y-6">
      <div>
        <Link href="/log" className="text-sm text-sky-400 hover:text-sky-300">
          ← Research log
        </Link>
        <time
          dateTime={info.date}
          className="mt-2 block text-xs font-semibold uppercase tracking-wide text-zinc-500 tabular-nums"
        >
          {formatLogDate(info.date)}
        </time>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">{info.title}</h1>
        {info.summary && (
          <p className="mt-2 max-w-3xl text-sm leading-relaxed text-zinc-400">{info.summary}</p>
        )}
        {(info.contributors.length > 0 || info.outcomes || info.tags.length > 0) && (
          <div className="mt-3 flex flex-wrap items-center gap-x-4 gap-y-2 border-t border-zinc-800 pt-3">
            <ContributorChips contributors={info.contributors} />
            <OutcomeCounts outcomes={info.outcomes} />
            <TagChips tags={info.tags} />
          </div>
        )}
      </div>

      <Mdx source={doc.content} />

      <nav className="flex flex-wrap justify-between gap-3 border-t border-zinc-800 pt-4 text-sm">
        {older ? (
          <Link href={`/log/${older.date}`} className="text-sky-400 hover:text-sky-300">
            ← {formatLogDate(older.date)}: {older.title}
          </Link>
        ) : (
          <span className="text-zinc-600">Earliest entry</span>
        )}
        {newer ? (
          <Link href={`/log/${newer.date}`} className="text-sky-400 hover:text-sky-300">
            {formatLogDate(newer.date)}: {newer.title} →
          </Link>
        ) : (
          <span className="text-zinc-600">Latest entry</span>
        )}
      </nav>

      <p className="text-xs leading-relaxed text-zinc-600">
        Log entries are a narrative account written by the contributors listed
        above. They are not evidence records: run validity, scientific outcome
        and evidence tier live with the{" "}
        <Link href="/experiments" className="text-zinc-500 underline underline-offset-2 hover:text-zinc-300">
          experiment and result records
        </Link>{" "}
        an entry refers to.
      </p>
    </div>
  );
}
