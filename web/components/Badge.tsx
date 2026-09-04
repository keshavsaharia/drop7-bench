/**
 * The record label chip. `<Badge kind="status" value="rejected" />` renders
 * `<span class="badge" data-kind="status" data-value="rejected">rejected</span>`;
 * the colour for each kind/value pair is defined once in globals.css and an
 * unknown value falls through to the neutral look. `rejected`, `fail` and
 * `not-supported-as-tested` are completed contributions and are never styled
 * as errors; only run validity `invalid` uses the danger colour.
 *
 * The old `label` / `className` props still work and render as kind="plain".
 * With both `value` and `label`, `label` is the text shown and `value` picks
 * the colour (the log's "3 negative" tallies).
 */
import { badgeText, type BadgeKind } from "@/lib/labels";

export type { BadgeKind };

export interface BadgeProps {
  kind?: BadgeKind;
  /** The recorded vocabulary token, shown as written (with a kind prefix where the kind needs one). */
  value?: string;
  /** Display text. Alone, renders as kind="plain"; with `value`, overrides the text while `value` keeps the colour. */
  label?: string;
  /** Legacy: extra classes. Colour utilities passed here are overridden by the token styles. */
  className?: string;
  title?: string;
  /** A leading dot; on by default for status, outcome and validity. */
  dot?: boolean;
}

const DOTTED: ReadonlySet<BadgeKind> = new Set<BadgeKind>(["status", "outcome", "validity"]);

export function Badge({ kind, value, label, className, title, dot }: BadgeProps) {
  const resolvedKind: BadgeKind = kind ?? "plain";
  const text = label ?? (value !== undefined ? badgeText(resolvedKind, value) : "");
  const showDot = dot ?? DOTTED.has(resolvedKind);
  return (
    <span
      className={className ? `badge ${className}` : "badge"}
      data-kind={resolvedKind}
      data-value={value}
      title={title}
    >
      {showDot && <span className="badge-dot" aria-hidden="true" />}
      {text}
    </span>
  );
}
