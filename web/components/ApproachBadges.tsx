/**
 * The compact label set used on approach cards and approach headers.
 *
 * Visitor-facing approach labels deliberately show only final status
 * (completed or rejected) and retained evidence (reproduced or recorded).
 * Information-access metadata stays in the records and prose, rather than in
 * this compact label row.
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

export function hasApproachBadges({ status, evidence }: ApproachBadgesProps): boolean {
  return Boolean(
    (status && VISIBLE_STATUSES.has(status)) ||
      (evidence && VISIBLE_EVIDENCE.has(evidence)),
  );
}

export function ApproachBadges({ status, evidence, className }: ApproachBadgesProps) {
  if (!hasApproachBadges({ status, evidence })) return null;

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
    </span>
  );
}
