/**
 * The aside of a record page (/theories/[id], /experiments/[id],
 * /results/[id]): the approach directory the record is about, the records
 * linked to it, its dates and its id, each in an `.aside-block`. Every value
 * is the record's own; nothing is counted or derived. Server component.
 *
 * The file also exports the small pieces the three record pages share: the
 * route for a record kind, a link for a repository path, the definition-list
 * field, and the readers that pull optional fields out of a record's JSON
 * without inventing a value for a missing one.
 */
import Link from "next/link";
import type { ReactNode } from "react";
import { rewriteRepoDocHref } from "@/lib/doc-links";
import { approachForRecord, type ApproachRef } from "@/lib/records";
import { listAllApproaches } from "@/lib/repo";

export type RecordKind = "theory" | "experiment" | "result";

const RECORD_ROUTE: Record<RecordKind, string> = {
  theory: "/theories",
  experiment: "/experiments",
  result: "/results",
};

/** The research/ subdirectory a record kind is stored in. */
export const RECORD_DIR: Record<RecordKind, string> = {
  theory: "theories",
  experiment: "experiments",
  result: "results",
};

export function recordHref(kind: RecordKind, id: string): string {
  return `${RECORD_ROUTE[kind]}/${id}`;
}

/** The 16px bot mark used wherever a section is written for an agent. */
export function AgentMark() {
  return (
    <svg
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      aria-hidden="true"
      focusable="false"
    >
      <rect x="2" y="5" width="12" height="9" rx="2" />
      <circle cx="6" cy="9.5" r="1" fill="currentColor" stroke="none" />
      <circle cx="10" cy="9.5" r="1" fill="currentColor" stroke="none" />
      <path d="M8 5V2.5" />
      <circle cx="8" cy="2" r="1" fill="currentColor" stroke="none" />
      <path d="M6 12.5h4" />
    </svg>
  );
}

/* ------------------------------------------------------------ path links */

/**
 * The console route for a repository path named in a record, or null when
 * the console has no page for it. Result JSON is mapped here; every other
 * path goes through the shared rewriter used by rendered Markdown.
 */
export function repoPathHref(path: string): string | null {
  const result = /^research\/results\/([A-Za-z0-9-]+)\.json$/.exec(path);
  if (result) return `/results/${result[1]}`;
  const mapped = rewriteRepoDocHref(path);
  return mapped !== path && mapped.startsWith("/") ? mapped : null;
}

/** A repository path as recorded, linked when the console has a page for it. */
export function RepoRef({ path }: { path: string }) {
  const href = repoPathHref(path);
  return href ? (
    <Link href={href}>
      <code>{path}</code>
    </Link>
  ) : (
    <code>{path}</code>
  );
}

/** The approach page for a reference, with the directory's title when it has a README. */
export function approachLink(ref: ApproachRef): { href: string; label: string } {
  const entry = listAllApproaches().find((e) => e.family === ref.family && e.slug === ref.slug);
  return {
    href: `/approach/${ref.family}/${ref.slug}`,
    label: entry?.title ?? `${ref.family}/${ref.slug}`,
  };
}

/* ------------------------------------------------------- record fields */

/** One field of a record inside a `.record-dl` definition list. */
export function RecordField({
  label,
  wide = false,
  children,
}: {
  label: string;
  wide?: boolean;
  children: ReactNode;
}) {
  return (
    <div className={wide ? "record-dl-wide" : undefined}>
      <dt className="label">{label}</dt>
      <dd>{children}</dd>
    </div>
  );
}

/** A recorded list of strings, or the words "none recorded" when it is empty. */
export function RecordList({
  items,
  ordered = false,
  render,
}: {
  items: readonly string[];
  ordered?: boolean;
  render?: (item: string) => ReactNode;
}) {
  if (items.length === 0) return <span className="record-none">none recorded</span>;
  const rows = items.map((item, index) => <li key={index}>{render ? render(item) : item}</li>);
  return ordered ? <ol>{rows}</ol> : <ul>{rows}</ul>;
}

/* Readers for optional fields. Each returns the recorded value or a typed
 * "absent" so a page can say a field is missing rather than default it. */

export function asRecord(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === "object" && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : null;
}

export function stringList(value: unknown): string[] {
  return Array.isArray(value) ? value.filter((item): item is string => typeof item === "string") : [];
}

export function stringOrNull(value: unknown): string | null {
  return typeof value === "string" ? value : null;
}

export function boolOrNull(value: unknown): boolean | null {
  return typeof value === "boolean" ? value : null;
}

/** A recorded scalar shown as written: numbers unrounded, null as a dash. */
export function scalarText(value: unknown): string {
  if (value === null || value === undefined) return "–";
  if (typeof value === "number" || typeof value === "boolean" || typeof value === "string") return String(value);
  return JSON.stringify(value);
}

/* --------------------------------------------------------------- aside */

export interface RecordAsideProps {
  id: string;
  kind: RecordKind;
  /** Theory ids linked to this record. */
  theoryIds?: readonly string[];
  /** Experiment ids linked to this record. */
  experimentIds?: readonly string[];
  /** Result ids linked to this record. */
  resultIds?: readonly string[];
  /** Theories this theory builds on. */
  dependencyIds?: readonly string[];
  /** The record's own timestamps, shown as recorded. */
  dates: readonly { label: string; value: string }[];
}

function IdLinks({ kind, ids }: { kind: RecordKind; ids: readonly string[] }) {
  return (
    <ul>
      {ids.map((linked) => (
        <li key={linked}>
          <Link className="mono" href={recordHref(kind, linked)}>
            {linked}
          </Link>
        </li>
      ))}
    </ul>
  );
}

export function RecordAside({
  id,
  kind,
  theoryIds = [],
  experimentIds = [],
  resultIds = [],
  dependencyIds = [],
  dates,
}: RecordAsideProps) {
  const approach = approachForRecord(id);
  const link = approach ? approachLink(approach) : null;
  return (
    <div className="record-aside" data-kind={kind}>
      <div className="aside-block">
        <span className="label">Linked approach</span>
        {link ? <Link href={link.href}>{link.label}</Link> : <p>The record names no approach directory.</p>}
      </div>
      {dependencyIds.length > 0 && (
        <div className="aside-block">
          <span className="label">Builds on</span>
          <IdLinks kind="theory" ids={dependencyIds} />
        </div>
      )}
      {theoryIds.length > 0 && (
        <div className="aside-block">
          <span className="label">Theories</span>
          <IdLinks kind="theory" ids={theoryIds} />
        </div>
      )}
      {experimentIds.length > 0 && (
        <div className="aside-block">
          <span className="label">Experiments</span>
          <IdLinks kind="experiment" ids={experimentIds} />
        </div>
      )}
      {resultIds.length > 0 && (
        <div className="aside-block">
          <span className="label">Results</span>
          <IdLinks kind="result" ids={resultIds} />
        </div>
      )}
      {dates.length > 0 && (
        <div className="aside-block">
          <span className="label">Dates</span>
          <dl>
            {dates.map((date) => (
              <div key={date.label}>
                <dt className="label">{date.label}</dt>
                <dd>
                  <time dateTime={date.value}>{date.value}</time>
                </dd>
              </div>
            ))}
          </dl>
        </div>
      )}
      <div className="aside-block">
        <span className="label">Record id</span>
        <span className="mono">{id}</span>
      </div>
    </div>
  );
}
