/**
 * <EvidenceStrip status evidence reads tier validity outcome cohort />
 *
 * One row of record-vocabulary chips, each a <Badge kind value /> so the
 * colour map in globals.css (`.badge[data-kind][data-value]`) is the single
 * authority: `fail`, `rejected` and `not-supported-as-tested` stay neutral,
 * only run validity `invalid` uses the danger colour. Each chip's title is
 * the sentence form from web/lib/charts/evidence.ts. Unknown words render
 * as the raw word in the neutral look; nothing here invents a tier.
 *
 * Renders under a figure (from spec.evidence), inside every StatTile, and
 * at the top of approach pages. Server component; styled by charts.css.
 */
import { Badge } from "./Badge";
import { evidenceChips, type EvidenceInput } from "@/lib/charts/evidence";
import type { BadgeKind } from "@/lib/labels";

export type EvidenceStripProps = EvidenceInput & {
  /** A source record or path shown after the chips, in mono. */
  source?: string;
  className?: string;
};

const KIND: Record<string, BadgeKind> = {
  status: "status",
  evidence: "evidence",
  reads: "reads",
  tier: "tier",
  validity: "validity",
  outcome: "outcome",
  cohort: "plain",
};

export function EvidenceStrip({ status, evidence, reads, tier, validity, outcome, cohort, source, className }: EvidenceStripProps) {
  const chips = evidenceChips({ status, evidence, reads, tier, validity, outcome, cohort });
  if (chips.length === 0 && !source) return null;
  return (
    <ul className={className ? `evidence-strip ${className}` : "evidence-strip"}>
      {chips.map((chip) => (
        <li key={`${chip.field}-${chip.word}`} data-tone={chip.tone} data-known={chip.known ? "true" : "false"}>
          <Badge kind={KIND[chip.field] ?? "plain"} value={chip.word} title={chip.sentence} />
        </li>
      ))}
      {source && (
        <li className="evidence-strip-source">
          <code>{source}</code>
        </li>
      )}
    </ul>
  );
}
