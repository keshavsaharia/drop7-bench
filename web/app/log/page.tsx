import Link from "next/link";
import { ContributorChips, OutcomeCounts, TagChips } from "@/components/ResearchLog";
import { formatLogDate, listLogEntries } from "@/lib/log";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Research log · Drop7 Research Console",
  description:
    "A dated, plain-English account of what was tried each day in the Drop7 million-point research program, including the things that did not work.",
};

export default function LogIndexPage() {
  const entries = listLogEntries();
  return (
    <div className="space-y-6">
      <div>
        <Link href="/research" className="text-sm text-sky-400 hover:text-sky-300">
          ← Research
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">Research log</h1>
        <p className="mt-2 max-w-3xl text-sm leading-relaxed text-zinc-400">
          A dated account of what was actually tried, by whom, and what came of
          it. Most days end with more rejected ideas than accepted ones, and
          those days are written up in the same detail as the good ones: a
          failed experiment rejects the configuration it tested, and that is a
          finished piece of work, not a wasted night.
        </p>
        <p className="mt-2 max-w-3xl text-sm leading-relaxed text-zinc-400">
          The log is narrative. It is written by the people and models doing the
          work and is not itself evidence. Every number in an entry belongs to a
          record — a{" "}
          <Link href="/theories" className="text-sky-400 hover:text-sky-300">
            theory
          </Link>
          , an{" "}
          <Link href="/experiments" className="text-sky-400 hover:text-sky-300">
            experiment
          </Link>{" "}
          or its result — and those records, with their run validity, scientific
          outcome and evidence tier, are the authority. Entries live in{" "}
          <code className="text-xs">web/content/log/</code>.
        </p>
      </div>

      {entries.length === 0 ? (
        <div className="rounded-xl border border-zinc-800 bg-zinc-950/40 p-6 text-sm text-zinc-500">
          No log entries are present in this checkout. Add{" "}
          <code className="text-xs">web/content/log/YYYY-MM-DD.mdx</code> to
          start one.
        </div>
      ) : (
        <ol className="space-y-3">
          {entries.map((entry) => (
            <li key={entry.date}>
              <Link
                href={`/log/${entry.date}`}
                className="block rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
              >
                <div className="flex flex-wrap items-baseline gap-x-3 gap-y-1">
                  <time
                    dateTime={entry.date}
                    className="text-xs font-semibold uppercase tracking-wide text-zinc-500 tabular-nums"
                  >
                    {formatLogDate(entry.date)}
                  </time>
                  {entry.outcomes && (
                    <span className="ml-auto">
                      <OutcomeCounts outcomes={entry.outcomes} />
                    </span>
                  )}
                </div>
                <h2 className="mt-1 font-bold text-zinc-100">{entry.title}</h2>
                {entry.summary && (
                  <p className="mt-1 max-w-3xl text-sm leading-relaxed text-zinc-400">
                    {entry.summary}
                  </p>
                )}
                {(entry.contributors.length > 0 || entry.tags.length > 0) && (
                  <div className="mt-3 flex flex-wrap items-center gap-x-4 gap-y-2">
                    <ContributorChips contributors={entry.contributors} />
                    <TagChips tags={entry.tags} />
                  </div>
                )}
              </Link>
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}
