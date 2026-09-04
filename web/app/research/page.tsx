import "./research.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { Markdown } from "@/components/Markdown";
import { PageHeader } from "@/components/PageHeader";
import { AgentMark } from "@/components/RecordAside";
import { listDocs } from "@/lib/docs";
import { listLogEntries } from "@/lib/log";
import { getExperiments, getResults, getTheories, listApproachesByKind, readRepoFile } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Research",
  description:
    "The working record of the Drop7 million-point program: theories, experiments, results, the daily log, diagnostics and documents.",
};

const STATUS_PATH = "docs/research/status.md";

/** The document's own h1 is dropped; the section heading names it. */
function withoutLeadingTitle(source: string): string {
  return source.replace(/^\s*# [^\n]*\n+/, "");
}

/** "24 records", "1 record", or the empty-checkout sentence. Counts files only. */
function countLabel(count: number, singular: string, plural: string): string {
  if (count === 0) return "none in this checkout";
  return `${count} ${count === 1 ? singular : plural}`;
}

export default function ResearchPage() {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const latestLog = listLogEntries()[0] ?? null;
  const diagnostics = listApproachesByKind("diagnostic");
  const documents = listDocs();
  const status = readRepoFile(STATUS_PATH);

  const cards = [
    {
      href: "/theories",
      title: "Theories",
      summary: "Registered falsifiable claims, each with a mechanism and the criteria that would refute it.",
      foot: countLabel(theories.length, "record", "records"),
    },
    {
      href: "/experiments",
      title: "Experiments",
      summary: "Preregistered protocols: candidate, comparator, cohort, metrics and gate, fixed before the data is read.",
      foot: countLabel(experiments.length, "record", "records"),
    },
    {
      href: "/results",
      title: "Results",
      summary: "Recorded outcomes, each carrying its run validity, scientific outcome and evidence tier.",
      foot: countLabel(results.length, "record", "records"),
    },
    {
      href: "/log",
      title: "Log",
      summary: "A dated account of what was tried each day, including what did not work.",
      foot: latestLog ? `latest entry ${latestLog.date}` : "none in this checkout",
    },
    {
      href: "/diagnostics",
      title: "Diagnostics",
      summary: "Measuring instruments, harnesses and model probes: the directories that measure rather than play.",
      foot: countLabel(diagnostics.length, "page", "pages"),
    },
    {
      href: "/docs",
      title: "Documents",
      summary: "The methodology, the benchmark contract, the ledger and the agent contracts the research is bound to.",
      foot: countLabel(documents.length, "document", "documents"),
    },
  ];

  return (
    <div>
      <PageHeader
        title="Research"
        lead="The working record of the program: what is claimed, what was tested, what was measured, and the daily log. Agent-facing detail sits behind the bot-icon accordions."
      />
      <div className="research-note label">
        <AgentMark />
        <span>This section holds the machine-readable records: theories, experiments and results as registered.</span>
      </div>

      <section className="research-section research-status" aria-labelledby="research-status-heading">
        <h2 id="research-status-heading" className="research-h2">
          Where the research stands
        </h2>
        {status ? (
          <Markdown source={withoutLeadingTitle(status)} fromPath={STATUS_PATH} />
        ) : (
          <p className="record-empty">The status document is not present in this checkout.</p>
        )}
        <p className="research-status-foot">
          <Link href="/docs/research/status">Open the status document</Link>
        </p>
      </section>

      <section className="research-section" aria-labelledby="research-records-heading">
        <h2 id="research-records-heading" className="research-h2">
          The record
        </h2>
        <ul className="research-cards">
          {cards.map((card) => (
            <li key={card.href}>
              <Card href={card.href} heading="h3" title={card.title} summary={card.summary} foot={card.foot} />
            </li>
          ))}
        </ul>
      </section>
    </div>
  );
}
