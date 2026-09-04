/**
 * The compact index of research records (/theories, /experiments,
 * /results): one row per record with its title, status badge(s), tier, date
 * and the approach directory it is about. Rows sort newest first by the
 * record's own date, on the server. Nothing is counted, computed or
 * summarised; the full record is on its page. Server component.
 */
import Link from "next/link";
import type { ReactNode } from "react";
import { Badge } from "./Badge";
import { approachLink, recordHref, type RecordKind } from "./RecordAside";
import { approachForRecord, type ApproachRef } from "@/lib/records";
import type { ExperimentRecord, ResultRecord, TheoryRecord } from "@/lib/repo";

type Stamped<T> = T & { $id: string };

export type RecordTableProps =
  | { kind: "theory"; records: readonly Stamped<TheoryRecord>[] }
  | { kind: "experiment"; records: readonly Stamped<ExperimentRecord>[] }
  | {
      kind: "result";
      records: readonly Stamped<ResultRecord>[];
      /** Result rows take their title from the experiment they were recorded against. */
      experiments: readonly Stamped<ExperimentRecord>[];
    };

interface Row {
  id: string;
  href: string;
  title: string;
  /** Shown under the title when the title alone does not identify the record. */
  subtitle: string | null;
  status: ReactNode;
  tier: string;
  /** The record's own timestamp, as recorded. */
  date: string;
  approach: ApproachRef | null;
}

/**
 * The first sentence of a recorded text: up to the first terminal mark that
 * is followed by a capital or an opening bracket. Falls back to the whole
 * text when no such boundary exists.
 */
export function firstSentence(text: string): string {
  const trimmed = text.trim();
  const match = /^[\s\S]*?[.!?](?=["')\]]*(?:\s+[A-Z(\["']|$))/.exec(trimmed);
  return match ? match[0] : trimmed;
}

/** The calendar date of an ISO timestamp, or the value unchanged when it has another shape. */
export function calendarDate(value: string): string {
  const match = /^\d{4}-\d{2}-\d{2}/.exec(value);
  return match ? match[0] : value;
}

function rowsFor(props: RecordTableProps): Row[] {
  switch (props.kind) {
    case "theory":
      return props.records.map((theory) => ({
        id: theory.theoryId,
        href: recordHref("theory", theory.theoryId),
        title: theory.title,
        subtitle: null,
        status: <Badge kind="outcome" value={theory.assessment} />,
        tier: theory.evidenceTier,
        date: theory.createdAt,
        approach: approachForRecord(theory.theoryId),
      }));
    case "experiment":
      return props.records.map((experiment) => ({
        id: experiment.experimentId,
        href: recordHref("experiment", experiment.experimentId),
        title: experiment.title,
        subtitle: null,
        status: <Badge kind="status" value={experiment.lifecycle} />,
        tier: experiment.benchmarkTier,
        date: experiment.createdAt,
        approach: approachForRecord(experiment.experimentId),
      }));
    case "result": {
      const titles = new Map(props.experiments.map((e) => [e.experimentId, e.title]));
      return props.records.map((result) => ({
        id: result.resultId,
        href: recordHref("result", result.resultId),
        title: titles.get(result.experimentId) ?? result.experimentId,
        subtitle: result.resultId,
        status: (
          <>
            <Badge kind="validity" value={result.runValidity} />
            <Badge kind="outcome" value={result.scientificOutcome} />
          </>
        ),
        tier: result.evidenceTier,
        date: result.recordedAt,
        approach: approachForRecord(result.resultId),
      }));
    }
  }
}

function byDateDescending(a: Row, b: Row): number {
  if (a.date !== b.date) return a.date < b.date ? 1 : -1;
  return a.id.localeCompare(b.id);
}

export function RecordTable(props: RecordTableProps) {
  const rows = rowsFor(props).sort(byDateDescending);
  const kind: RecordKind = props.kind;
  return (
    <div className="record-table-wrap">
      <table className="data-table record-table" aria-label={`${kind} records`}>
        <thead>
          <tr>
            <th scope="col">Title</th>
            <th scope="col">Status</th>
            <th scope="col">Tier</th>
            <th scope="col">Date</th>
            <th scope="col">Approach</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => {
            const link = row.approach ? approachLink(row.approach) : null;
            return (
              <tr key={row.id}>
                <td className="record-table-title">
                  <Link href={row.href}>{row.title}</Link>
                  {row.subtitle && <div className="record-table-id">{row.subtitle}</div>}
                </td>
                <td>
                  <div className="record-table-badges">{row.status}</div>
                </td>
                <td>
                  <Badge kind="tier" value={row.tier} />
                </td>
                <td>
                  <time dateTime={row.date} title={row.date}>
                    {calendarDate(row.date)}
                  </time>
                </td>
                <td>
                  {link ? (
                    <Link className="record-approach" href={link.href}>
                      {link.label}
                    </Link>
                  ) : (
                    <span className="record-dash" aria-label="none">
                      –
                    </span>
                  )}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
