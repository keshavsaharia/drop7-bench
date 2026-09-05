/**
 * MDX components for the daily research log (/log).
 *
 * These are presentation only. Every number, label and tally rendered here is
 * a string the entry's author wrote in the MDX: nothing is parsed into a
 * quantity, averaged, rounded, totalled or inferred. The single piece of logic
 * that looks at a value is `deltaTone`, which reads the leading "+" or "-"/"—"
 * of a delta cell to pick an accent colour; it reports the sign the author
 * already wrote and makes no judgement about whether that sign is good.
 *
 * Colour comes from the tokens through the `.log-*`, `.badge` and `.chip`
 * blocks in globals.css. A negative result is a completed scientific
 * contribution and is never styled as an error.
 *
 * Server components only.
 */

import { Badge } from "./Badge";

/* ---------------------------------------------------------------- Finding */

export type FindingKind = "positive" | "negative" | "neutral" | "open";

const FINDING_KINDS: ReadonlySet<string> = new Set(["positive", "negative", "neutral", "open"]);

/**
 * The workhorse card: one result from the day's work.
 *
 * <Finding title="Depth 5 loses to depth 4" kind="negative" metric="−237,182 points">
 */
export function Finding({
  title,
  kind = "neutral",
  metric,
  children,
}: {
  title: string;
  kind?: FindingKind;
  /** Short pre-formatted string shown as a badge in the header. */
  metric?: string;
  children?: React.ReactNode;
}) {
  const resolved: FindingKind = FINDING_KINDS.has(kind) ? kind : "neutral";
  return (
    <section className={`log-card log-card--${resolved}`}>
      <header className="log-card-head">
        <Badge kind="outcome" value={resolved} />
        <h3>{title}</h3>
        {metric && <span className="log-card-metric">{metric}</span>}
      </header>
      {children && <div className="log-body">{children}</div>}
    </section>
  );
}

/* ---------------------------------------------------------------- DeadEnd */

const VERDICT_TEXT: Record<string, string> = {
  closed: "The direction is closed: no further configurations of it are planned.",
  "configuration-rejected":
    "Only the exact configuration tested is rejected. The idea behind it is untouched.",
};

const VERDICT_LABEL: Record<string, string> = {
  closed: "direction closed",
  "configuration-rejected": "configuration rejected",
};

/**
 * A thing that was tried and did not work — a recorded result, not a failure
 * note. `cost` is the author's own string ("36 GPU-hours"); `verdict`
 * distinguishes a closed direction from a single rejected configuration.
 */
export function DeadEnd({
  title,
  cost,
  verdict,
  children,
}: {
  title: string;
  cost?: string;
  verdict?: "closed" | "configuration-rejected";
  children?: React.ReactNode;
}) {
  return (
    <section className="log-card log-card--negative">
      <header className="log-card-head">
        <Badge value="dead end · recorded result" />
        <h3>{title}</h3>
        {cost && <span className="log-card-metric">cost: {cost}</span>}
      </header>
      {children && <div className="log-body">{children}</div>}
      {verdict && (
        <footer className="log-card-foot">
          <Badge value={VERDICT_LABEL[verdict] ?? verdict} />
          <span>{VERDICT_TEXT[verdict] ?? ""}</span>
        </footer>
      )}
    </section>
  );
}

/* -------------------------------------------------------------- Direction */

export type DirectionStatus = "proposed" | "running" | "blocked" | "closed";

const DIRECTION_STATUSES: ReadonlySet<string> = new Set(["proposed", "running", "blocked", "closed"]);

/** A research direction: new, running, stuck, or shut. */
export function Direction({
  title,
  status = "proposed",
  owner,
  children,
}: {
  title: string;
  status?: DirectionStatus;
  /** Who is carrying it, as written by the author ("OpenCode / kimi-k3"). */
  owner?: string;
  children?: React.ReactNode;
}) {
  const resolved: DirectionStatus = DIRECTION_STATUSES.has(status) ? status : "proposed";
  return (
    <section className={`log-card log-card--${resolved}`}>
      <header className="log-card-head">
        <Badge kind="status" value={resolved} />
        <h3>{title}</h3>
        {owner && <span className="log-card-owner">owner: {owner}</span>}
      </header>
      {children && <div className="log-body">{children}</div>}
    </section>
  );
}

/* --------------------------------------------------------------- ArmTable */

export interface ArmColumn {
  key: string;
  label?: string;
  /** Right-align and use tabular numerals. */
  numeric?: boolean;
  /** Colour the cell by the leading sign the author wrote. */
  delta?: boolean;
}

export type ArmRow = Record<string, string | number | boolean | null | undefined>;

function columnKey(column: ArmColumn | string): string {
  return typeof column === "string" ? column : column.key;
}

function columnLabel(column: ArmColumn | string): string {
  return typeof column === "string" ? column : column.label ?? column.key;
}

/** Reads the leading sign of an already-written value. It parses no quantity. */
function deltaTone(value: string): "up" | "down" | "flat" {
  const text = value.trim();
  if (text.startsWith("+")) return "up";
  if (text.startsWith("-") || text.startsWith("−")) return "down";
  return "flat";
}

/**
 * A compact comparison of experiment arms. Every cell is a pre-formatted
 * string from the entry's author; a missing cell renders as an em dash rather
 * than as zero. Mark the baseline row with `highlight: true` on the row
 * itself. The column set is whatever `columns` says — nothing is hard-coded.
 */
export function ArmTable({
  caption,
  columns,
  rows,
}: {
  caption?: string;
  columns: readonly (ArmColumn | string)[];
  rows: readonly ArmRow[];
}) {
  const cols = columns ?? [];
  const body = rows ?? [];
  return (
    <figure className="arm-table">
      <div className="arm-table-scroll">
        <table className="data-table">
          <thead>
            <tr>
              {cols.map((column) => (
                <th
                  key={columnKey(column)}
                  className={typeof column !== "string" && column.numeric ? "num" : undefined}
                  scope="col"
                >
                  {columnLabel(column)}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {body.map((row, rowIndex) => (
              <tr key={rowIndex} className={row.highlight === true ? "is-highlight" : undefined}>
                {cols.map((column) => {
                  const key = columnKey(column);
                  const spec = typeof column === "string" ? undefined : column;
                  const raw = row[key];
                  // Absent (undefined/null) renders an em dash; an explicit
                  // empty string is an intentionally blank cell and stays blank.
                  const text = raw === undefined || raw === null ? null : String(raw);
                  const tone = text && spec?.delta ? `delta-${deltaTone(text)}` : undefined;
                  return (
                    <td key={key} className={spec?.numeric ? "num" : undefined}>
                      {text === null ? <span className="cell-absent">—</span> : <span className={tone}>{text}</span>}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {caption && <figcaption className="fig-caption">{caption}</figcaption>}
    </figure>
  );
}

/* --------------------------------------------------------------- Timeline */

export interface TimelineEntry {
  time: string;
  text: string;
  kind?: "positive" | "negative" | "neutral";
}

/** What happened when, over a night of runs. Times are the author's strings. */
export function Timeline({
  entries,
  caption,
}: {
  entries: readonly TimelineEntry[];
  caption?: string;
}) {
  const list = entries ?? [];
  if (list.length === 0) return null;
  return (
    <figure className="log-timeline">
      <ol>
        {list.map((entry, index) => (
          <li key={index}>
            <span className="log-timeline-time">{entry.time}</span>
            <span className="log-timeline-rail">
              <span aria-hidden="true" className={`log-timeline-dot log-timeline-dot--${entry.kind ?? "neutral"}`} />
              {index < list.length - 1 && <span aria-hidden="true" className="log-timeline-line" />}
            </span>
            <span className="log-timeline-text">{entry.text}</span>
          </li>
        ))}
      </ol>
      {caption && <figcaption className="fig-caption">{caption}</figcaption>}
    </figure>
  );
}

/* --------------------------------------------------------------- LogQuote */

/** The day's one-sentence takeaway, attributed to whoever wrote it. */
export function LogQuote({ who, children }: { who?: string; children: React.ReactNode }) {
  return (
    <figure className="log-quote">
      <blockquote className="log-body">{children}</blockquote>
      {who && <figcaption>— {who}</figcaption>}
    </figure>
  );
}

/* ------------------------------------------------- shared index furniture */

/** Contributor chips, exactly as the entry lists them. */
export function ContributorChips({ contributors }: { contributors: readonly string[] }) {
  if (contributors.length === 0) return null;
  return (
    <div className="flex flex-wrap items-center gap-1.5">
      {contributors.map((who) => (
        <span key={who} className="chip">
          {who}
        </span>
      ))}
    </div>
  );
}

/** Tag chips, exactly as the entry lists them. */
export function TagChips({ tags }: { tags: readonly string[] }) {
  if (tags.length === 0) return null;
  return (
    <div className="flex flex-wrap items-center gap-1.5">
      {tags.map((tag) => (
        <span key={tag} className="chip chip--tag">
          #{tag}
        </span>
      ))}
    </div>
  );
}

/** The author's own outcome tallies. Nothing is summed or completed here. */
export function OutcomeCounts({ outcomes }: { outcomes: Record<string, number> | null }) {
  if (!outcomes) return null;
  const keys = Object.keys(outcomes);
  if (keys.length === 0) return null;
  return (
    <div className="flex flex-wrap items-center gap-1.5">
      {keys.map((key) => (
        <Badge key={key} kind="outcome" value={key} label={`${outcomes[key]} ${key}`} />
      ))}
    </div>
  );
}
