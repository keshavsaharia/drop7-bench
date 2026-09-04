/**
 * The compact label set used on approach cards and approach headers.
 *
 * Visitor-facing approach labels deliberately show only final status
 * (completed or rejected) and retained evidence (reproduced or recorded).
 * Public-information access is the default and is therefore omitted. A
 * privileged information class remains visible because it changes how the
 * work may be interpreted.
 */
import { Badge } from "./Badge";

const VISIBLE_STATUSES = new Set(["completed", "rejected"]);
const VISIBLE_EVIDENCE = new Set(["reproduced", "ledger-recorded"]);

export interface ApproachBadgesProps {
  status?: string | null;
  evidence?: string | null;
  reads?: string | null;
  className?: string;
}

export function hasApproachBadges({ status, evidence, reads }: ApproachBadgesProps): boolean {
  return Boolean(
    (status && VISIBLE_STATUSES.has(status)) ||
      (evidence && VISIBLE_EVIDENCE.has(evidence)) ||
      (reads && reads !== "public"),
  );
}

export function ApproachBadges({ status, evidence, reads, className }: ApproachBadgesProps) {
  if (!hasApproachBadges({ status, evidence, reads })) return null;

  return (
    <span className={className ? `approach-badges ${className}` : "approach-badges"}>
      {status && VISIBLE_STATUSES.has(status) && <Badge kind="status" value={status} />}
      {evidence && VISIBLE_EVIDENCE.has(evidence) && (
        <Badge
          kind="evidence"
          value={evidence}
          label={evidence === "ledger-recorded" ? "recorded" : evidence}
        />
      )}
      {reads && reads !== "public" && <Badge kind="reads" value={reads} />}
    </span>
  );
}
