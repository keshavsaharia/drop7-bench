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
 * Styling follows the console's existing vocabulary (Research.tsx, Board.tsx):
 * zinc cards, emerald for positive, orange for negative, amber for unsettled,
 * sky for open, purple for proposals. A negative result is a completed
 * scientific contribution and is never styled as an error.
 *
 * Server components only.
 */

import { Badge } from "./Badge";

/* ---------------------------------------------------------------- accents */

export type FindingKind = "positive" | "negative" | "neutral" | "open";

interface Accent {
  rail: string;
  chip: string;
  label: string;
}

const FINDING_ACCENTS: Record<FindingKind, Accent> = {
  positive: { rail: "border-l-emerald-600", chip: "bg-emerald-900/60 text-emerald-200", label: "positive" },
  negative: { rail: "border-l-orange-700", chip: "bg-orange-900/60 text-orange-200", label: "negative" },
  neutral: { rail: "border-l-zinc-600", chip: "bg-zinc-800 text-zinc-300", label: "neutral" },
  open: { rail: "border-l-sky-700", chip: "bg-sky-900/60 text-sky-200", label: "open" },
};

const OUTCOME_CHIPS: Record<string, string> = {
  positive: "bg-emerald-900/60 text-emerald-200",
  negative: "bg-orange-900/60 text-orange-200",
  open: "bg-sky-900/60 text-sky-200",
  neutral: "bg-zinc-800 text-zinc-300",
  inconclusive: "bg-amber-900/60 text-amber-200",
};

/** Chip colour for an outcome tally key; unknown keys stay neutral zinc. */
export function outcomeChipClass(key: string): string {
  return OUTCOME_CHIPS[key] ?? "bg-zinc-800 text-zinc-300";
}

/* ---------------------------------------------------------------- Finding */

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
  const accent = FINDING_ACCENTS[kind] ?? FINDING_ACCENTS.neutral;
  return (
    <section className={`log-card my-4 rounded-xl border border-l-4 border-zinc-800 bg-zinc-900/50 p-4 ${accent.rail}`}>
      <header className="flex flex-wrap items-center gap-2">
        <Badge label={accent.label} className={accent.chip} />
        <h3 className="text-sm font-bold text-zinc-100">{title}</h3>
        {metric && (
          <span className="ml-auto rounded-md border border-zinc-700 bg-zinc-950/60 px-2 py-0.5 text-xs font-semibold tabular-nums text-zinc-200">
            {metric}
          </span>
        )}
      </header>
      {children && (
        <div className="log-body mt-2 text-sm leading-relaxed text-zinc-300">
          {children}
        </div>
      )}
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
    <section className="log-card my-4 rounded-xl border border-l-4 border-zinc-800 border-l-zinc-500 bg-zinc-900/50 p-4">
      <header className="flex flex-wrap items-center gap-2">
        <Badge label="dead end · recorded result" className="bg-zinc-700/70 text-zinc-100" />
        <h3 className="text-sm font-bold text-zinc-100">{title}</h3>
        {cost && (
          <span className="ml-auto rounded-md border border-zinc-700 bg-zinc-950/60 px-2 py-0.5 text-xs font-semibold tabular-nums text-zinc-300">
            cost: {cost}
          </span>
        )}
      </header>
      {children && (
        <div className="log-body mt-2 text-sm leading-relaxed text-zinc-300">
          {children}
        </div>
      )}
      {verdict && (
        <footer className="mt-3 flex flex-wrap items-center gap-2 border-t border-zinc-800 pt-2 text-xs text-zinc-500">
          <Badge label={VERDICT_LABEL[verdict] ?? verdict} />
          <span>{VERDICT_TEXT[verdict] ?? ""}</span>
        </footer>
      )}
    </section>
  );
}

/* -------------------------------------------------------------- Direction */

export type DirectionStatus = "proposed" | "running" | "blocked" | "closed";

const DIRECTION_ACCENTS: Record<DirectionStatus, Accent> = {
  proposed: { rail: "border-l-purple-700", chip: "bg-purple-900/60 text-purple-200", label: "proposed" },
  running: { rail: "border-l-sky-700", chip: "bg-sky-900/60 text-sky-200", label: "running" },
  blocked: { rail: "border-l-amber-700", chip: "bg-amber-900/60 text-amber-200", label: "blocked" },
  closed: { rail: "border-l-zinc-600", chip: "bg-zinc-800 text-zinc-300", label: "closed" },
};

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
  const accent = DIRECTION_ACCENTS[status] ?? DIRECTION_ACCENTS.proposed;
  return (
    <section className={`log-card my-4 rounded-xl border border-l-4 border-zinc-800 bg-zinc-950/50 p-4 ${accent.rail}`}>
      <header className="flex flex-wrap items-center gap-2">
        <Badge label={accent.label} className={accent.chip} />
        <h3 className="text-sm font-bold text-zinc-100">{title}</h3>
        {owner && <span className="ml-auto text-xs text-zinc-500">owner: {owner}</span>}
      </header>
      {children && (
        <div className="log-body mt-2 text-sm leading-relaxed text-zinc-300">
          {children}
        </div>
      )}
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

const DELTA_TONE_CLASS: Record<string, string> = {
  up: "text-emerald-300",
  down: "text-orange-300",
  flat: "text-zinc-300",
};

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
        <table>
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
                  const tone = text && spec?.delta ? DELTA_TONE_CLASS[deltaTone(text)] : undefined;
                  return (
                    <td key={key} className={spec?.numeric ? "num" : undefined}>
                      {text === null ? (
                        <span className="text-zinc-600">—</span>
                      ) : (
                        <span className={tone}>{text}</span>
                      )}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {caption && <figcaption className="engine-caption">{caption}</figcaption>}
    </figure>
  );
}

/* --------------------------------------------------------------- Timeline */

export interface TimelineEntry {
  time: string;
  text: string;
  kind?: "positive" | "negative" | "neutral";
}

const MARKER_CLASS: Record<string, string> = {
  positive: "bg-emerald-500",
  negative: "bg-orange-500",
  neutral: "bg-zinc-500",
};

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
    <figure className="log-timeline my-4 rounded-xl border border-zinc-800 bg-zinc-950/40 px-4 py-3">
      <ol>
        {list.map((entry, index) => (
          <li key={index} className="relative flex gap-3 py-1.5">
            <span className="w-14 shrink-0 pt-px text-right text-xs tabular-nums text-zinc-500">
              {entry.time}
            </span>
            <span className="relative flex w-3 shrink-0 justify-center">
              <span
                aria-hidden="true"
                className={`mt-1.5 h-2 w-2 shrink-0 rounded-full ${
                  MARKER_CLASS[entry.kind ?? "neutral"] ?? MARKER_CLASS.neutral
                }`}
              />
              {index < list.length - 1 && (
                <span aria-hidden="true" className="absolute top-3.5 bottom-[-0.5rem] w-px bg-zinc-800" />
              )}
            </span>
            <span className="text-sm leading-relaxed text-zinc-300">{entry.text}</span>
          </li>
        ))}
      </ol>
      {caption && <figcaption className="engine-caption">{caption}</figcaption>}
    </figure>
  );
}

/* --------------------------------------------------------------- LogQuote */

/** The day's one-sentence takeaway, attributed to whoever wrote it. */
export function LogQuote({ who, children }: { who?: string; children: React.ReactNode }) {
  return (
    <figure className="log-quote my-5 border-l-4 border-sky-800 bg-sky-950/20 px-5 py-4">
      <blockquote className="log-body text-base leading-relaxed text-sky-50">
        {children}
      </blockquote>
      {who && <figcaption className="mt-2 text-xs text-sky-300/80">— {who}</figcaption>}
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
        <span
          key={who}
          className="rounded-full border border-zinc-700 bg-zinc-900 px-2 py-0.5 text-[11px] text-zinc-300"
        >
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
        <span key={tag} className="rounded bg-zinc-800/80 px-1.5 py-0.5 text-[11px] text-zinc-400">
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
        <Badge key={key} label={`${outcomes[key]} ${key}`} className={outcomeChipClass(key)} />
      ))}
    </div>
  );
}
