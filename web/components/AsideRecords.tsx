/**
 * The "Records" block in the aside of an approach or engine page.
 *
 * A directory that has been worked on for a while accumulates dozens of
 * records, so the block shows each one as a small card carrying the title the
 * record was filed under and nothing else, split into theories, experiments
 * and results. Five cards of each kind are shown; the rest sit behind a
 * "Show all" toggle, and the whole block scrolls inside itself so a long list
 * never pushes the family and source blocks off the page.
 *
 * A result record has no title of its own, so it borrows the title of the
 * experiment it reports on, which is the name the work was registered under.
 * Nothing here counts, derives or reformats a research value.
 *
 * Server component: the toggle is a <details>, so it needs no JavaScript.
 */
import Link from "next/link";
import type { ApproachRecords } from "@/lib/records";
import { getExperiments, type ExperimentRecord } from "@/lib/repo";

/** Cards shown before the "Show all" toggle takes over. */
const VISIBLE = 5;

interface RecordLink {
  key: string;
  title: string;
  href: string;
}

function PlusIcon() {
  return (
    <svg
      className="aside-more-icon"
      viewBox="0 0 16 16"
      width="12"
      height="12"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d="M8 3.5v9" className="aside-more-bar" />
      <path d="M3.5 8h9" />
    </svg>
  );
}

function RecordCards({ items }: { items: readonly RecordLink[] }) {
  return (
    <ul className="aside-cards">
      {items.map((item) => (
        <li key={item.key}>
          <Link href={item.href} className="aside-card">
            <span className="aside-card-text">{item.title}</span>
          </Link>
        </li>
      ))}
    </ul>
  );
}

function RecordGroup({ label, items }: { label: string; items: readonly RecordLink[] }) {
  if (items.length === 0) return null;
  const shown = items.slice(0, VISIBLE);
  const rest = items.slice(VISIBLE);
  return (
    <div className="aside-group">
      <span className="label aside-group-label">
        {label}
        <span className="aside-group-count">{items.length}</span>
      </span>
      <RecordCards items={shown} />
      {rest.length > 0 && (
        <details className="aside-more">
          <summary>
            <PlusIcon />
            <span className="aside-more-open">Show all {items.length}</span>
            <span className="aside-more-close">Show fewer</span>
          </summary>
          <RecordCards items={rest} />
        </details>
      )}
    </div>
  );
}

/** The recorded title of an experiment, or its id when the checkout has no record for it. */
function experimentTitle(id: string, experiments: readonly (ExperimentRecord & { $id: string })[]): string {
  const record = experiments.find((candidate) => candidate.experimentId === id || candidate.$id === id);
  return record?.title ?? id;
}

export function AsideRecords({ records }: { records: ApproachRecords }) {
  const total = records.theories.length + records.experiments.length + records.results.length;
  if (total === 0) {
    return (
      <div className="aside-block">
        <span className="label">Records</span>
        <p className="aside-text">No linked records</p>
      </div>
    );
  }

  const experiments = getExperiments() as (ExperimentRecord & { $id: string })[];

  const theories: RecordLink[] = records.theories.map((theory) => ({
    key: theory.$id,
    title: theory.title ?? theory.theoryId ?? theory.$id,
    href: `/theories/${theory.theoryId ?? theory.$id}`,
  }));
  const experimentLinks: RecordLink[] = records.experiments.map((experiment) => ({
    key: experiment.$id,
    title: experiment.title ?? experiment.experimentId ?? experiment.$id,
    href: `/experiments/${experiment.experimentId ?? experiment.$id}`,
  }));
  // Two results of one experiment would otherwise show the same title, so a
  // repeated title carries the date the result was recorded.
  const resultTitles = records.results.map((result) => experimentTitle(result.experimentId, experiments));
  const results: RecordLink[] = records.results.map((result, index) => {
    const title = resultTitles[index];
    const repeated = resultTitles.indexOf(title) !== resultTitles.lastIndexOf(title);
    const recorded = typeof result.recordedAt === "string" ? result.recordedAt.slice(0, 10) : null;
    return {
      key: result.$id,
      title: repeated && recorded ? `${title} (${recorded})` : title,
      href: `/results/${result.resultId ?? result.$id}`,
    };
  });

  return (
    <div className="aside-block">
      <span className="label aside-group-label">
        Records
        <span className="aside-group-count">{total}</span>
      </span>
      <div className="aside-records">
        <RecordGroup label="Theories" items={theories} />
        <RecordGroup label="Experiments" items={experimentLinks} />
        <RecordGroup label="Results" items={results} />
      </div>
    </div>
  );
}
