/**
 * Collapsible sections built on <details>, so they work with no JavaScript,
 * are keyboard-accessible and print when open. Two variants of one look:
 *
 *   <AgentContext summary="How to reproduce this run, and what an agent needs to extend it">
 *   <TechnicalRecord summary="Cohort, tier, numbers and the gate table" meta="EX-2026-…">
 *
 * `TechnicalDetails` remains as an alias of the technical variant so the
 * existing pages keep working; its old `title` prop becomes the summary line.
 * A link to `#<id>` opens the accordion it lives in through OpenOnHash.
 * Styled by the `.reveal*` block in globals.css.
 */
import type { ReactNode } from "react";

export type RevealVariant = "agent" | "technical";

export interface RevealProps {
  variant?: RevealVariant;
  /** The mono label; defaults to "Agent context" or "Technical record". */
  label?: string;
  /** One line saying what is inside. */
  summary?: string;
  /** Optional anchor; a link to `#id` opens the section. */
  id?: string;
  /** Optional right-hand chip, e.g. an EX-/RS- id. */
  meta?: string;
  /** Render open. */
  open?: boolean;
  /** Native exclusive-group name: only one open at a time. Use for technical records, not agent context. */
  name?: string;
  className?: string;
  children: ReactNode;
}

const DEFAULT_LABEL: Record<RevealVariant, string> = {
  agent: "Agent context",
  technical: "Technical record",
};

function BotIcon() {
  return (
    <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
      <rect x="2" y="5" width="12" height="9" rx="2" />
      <circle cx="6" cy="9.5" r="1" fill="currentColor" stroke="none" />
      <circle cx="10" cy="9.5" r="1" fill="currentColor" stroke="none" />
      <path d="M8 5V2.5" />
      <circle cx="8" cy="2" r="1" fill="currentColor" stroke="none" />
      <path d="M6 12.5h4" />
    </svg>
  );
}

function LedgerIcon() {
  return (
    <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
      <rect x="3" y="2" width="10" height="12" rx="1.5" />
      <path d="M5.5 6h5M5.5 9h5M5.5 12h3" />
    </svg>
  );
}

export function Reveal({
  variant = "agent",
  label,
  summary,
  id,
  meta,
  open,
  name,
  className,
  children,
}: RevealProps) {
  const classes = ["reveal", `reveal--${variant}`, className].filter(Boolean).join(" ");
  return (
    <details className={classes} id={id} open={open} name={name}>
      <summary className="reveal-summary">
        <span className="reveal-icon" aria-hidden="true">
          {variant === "agent" ? <BotIcon /> : <LedgerIcon />}
        </span>
        <span className="reveal-label">{label ?? DEFAULT_LABEL[variant]}</span>
        <span className="reveal-line">{summary}</span>
        {meta && <span className="reveal-meta">{meta}</span>}
        <span className="reveal-hint" aria-hidden="true">
          <span className="when-closed">open ↓</span>
          <span className="when-open">close ↑</span>
        </span>
      </summary>
      <div className="reveal-body">{children}</div>
    </details>
  );
}

export type RevealAliasProps = Omit<RevealProps, "variant">;

/** Context written for an agent: preregistration text, run notes, seed leases, reproduction steps. */
export function AgentContext(props: RevealAliasProps) {
  return <Reveal variant="agent" {...props} />;
}

/** The record behind a page's sentences: cohort, tier, numbers, gate table, IDs. */
export function TechnicalRecord(props: RevealAliasProps) {
  return <Reveal variant="technical" {...props} />;
}

const LEGACY_DEFAULT_TITLE = "The technical record";

/**
 * Backwards-compatible alias used by the existing approach and research
 * pages. The old `title` becomes the summary line, except the old default
 * title, which would only repeat the label.
 */
export function TechnicalDetails({
  title,
  summary,
  children,
  ...rest
}: RevealAliasProps & { title?: string }) {
  const line = summary ?? (title && title !== LEGACY_DEFAULT_TITLE ? title : undefined);
  return (
    <Reveal variant="technical" summary={line} {...rest}>
      {children}
    </Reveal>
  );
}
