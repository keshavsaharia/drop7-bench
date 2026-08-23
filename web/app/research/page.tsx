import Link from "next/link";
import { listLogEntries } from "@/lib/log";
import {
  getExperiments,
  getResults,
  getTheories,
  listApproaches,
  listFamilies,
} from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Research · Drop7",
  description:
    "Approaches, theories, experiments, and the daily research log for the Drop7 million-point program.",
};

export default function ResearchPage() {
  const families = listFamilies();
  const approachCount = families.reduce(
    (sum, family) => sum + listApproaches(family).length,
    0,
  );
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const logEntries = listLogEntries();
  const latestLog = logEntries[0] ?? null;

  const sections = [
    {
      href: "/approaches",
      title: "Approaches",
      text: "Every established strategy family, and the underlying approaches that are being investigated within each one.",
      hint:
        families.length === 0
          ? "none in this checkout"
          : `${approachCount} approaches · ${families.length} families`,
    },
    {
      href: "/theories",
      title: "Theories",
      text: "Registered falsifiable claims - each names a mechanism and the criteria that would refute it.",
      hint:
        theories.length === 0
          ? "none in this checkout"
          : `${theories.length} registered`,
    },
    {
      href: "/experiments",
      title: "Experiments",
      text: "Preregistered protocols and their recorded results, with run validity and evidence tier carried through.",
      hint:
        experiments.length === 0
          ? "none in this checkout"
          : `${experiments.length} protocols · ${results.length} results`,
    },
    {
      href: "/log",
      title: "Log",
      text: "A dated, plain-English account of what was tried each day — including the things that did not work.",
      hint: latestLog
        ? `latest: ${latestLog.date}`
        : "none in this checkout",
    },
  ];

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-black text-zinc-50">Research</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          The working record of the goal to build a strategy that scores an average of one million points per game. 
          Approaches are the strategies that were implemented; theories are the claims those
          strategies rest on; experiments are the tests that were preregistered;
          the log has daily narratives.
        </p>
      </div>
      <div className="grid gap-4 sm:grid-cols-2">
        {sections.map((section) => (
          <Link
            key={section.href}
            href={section.href}
            className="group rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-sky-800"
          >
            <h2 className="font-bold text-zinc-100 group-hover:text-sky-300">
              {section.title}
            </h2>
            <p className="mt-1 text-sm text-zinc-400">{section.text}</p>
            <p className="mt-3 text-xs text-zinc-600">{section.hint}</p>
          </Link>
        ))}
      </div>
    </div>
  );
}
