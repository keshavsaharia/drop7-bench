/**
 * The reason-driven empty state every figure shares: one card with a title
 * line, one sentence saying what is absent and why, and an optional muted
 * "how it would be produced" line. Never a placeholder number, never a CLI
 * command (that goes in a details block on the page). Server-safe; no hooks.
 */
import type { ReactNode } from "react";
import type { FigureEvidence } from "@/lib/charts/spec";
import { EvidenceStrip } from "@/components/EvidenceStrip";

export type EmptyReason = "no-spec" | "invalid-spec" | "not-run" | "stopped" | "not-retained" | "no-record";

const TITLES: Record<EmptyReason, string> = {
  "no-spec": "No figure spec",
  "invalid-spec": "Figure spec cannot be rendered",
  "not-run": "Not run yet",
  stopped: "Stopped by decision",
  "not-retained": "Rows not retained",
  "no-record": "No recorded data",
};

function sentence(reason: EmptyReason, what?: string): string {
  switch (reason) {
    case "no-spec":
      return what ? `No spec exists for ${what}; nothing is drawn until one is written.` : "No spec exists for this figure; nothing is drawn until one is written.";
    case "invalid-spec":
      return what ? `The spec for ${what} did not pass validation, so no value from it is drawn.` : "The spec did not pass validation, so no value from it is drawn.";
    case "not-run":
      return what ? `${what} has not started, so there is nothing to plot.` : "This stage has not started, so there is nothing to plot.";
    case "stopped":
      return what ? `${what} was stopped by decision; the rows it produced before stopping are shown where they were retained.` : "This arm was stopped by decision.";
    case "not-retained":
      return what ? `The per-row artifact for ${what} was not retained; the recorded summary is shown instead.` : "The per-row artifact was not retained; the recorded summary is shown instead.";
    case "no-record":
      return what ? `No record carries ${what} yet.` : "No record carries this quantity yet.";
  }
}

export interface EmptyStateProps {
  reason: EmptyReason;
  /** The figure, stage or quantity that is absent. */
  what?: string;
  /** The validator message or another one-line explanation. */
  detail?: string;
  /** The artifact or script that would produce it (a path, not a command). */
  how?: string;
  evidence?: FigureEvidence;
  children?: ReactNode;
}

export function EmptyState({ reason, what, detail, how, evidence, children }: EmptyStateProps) {
  return (
    <div className="rchart-empty-state" role="note" data-reason={reason}>
      <p className="rchart-empty-title">{TITLES[reason]}</p>
      <p className="rchart-empty-text">{sentence(reason, what)}</p>
      {detail && <p className="rchart-empty-text rchart-empty-detail">{detail}</p>}
      {how && <p className="rchart-empty-how">Produced by {how}.</p>}
      {evidence && <EvidenceStrip {...evidence} />}
      {children}
    </div>
  );
}
