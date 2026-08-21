/**
 * Daily research log: web/content/log/YYYY-MM-DD.mdx
 *
 * Prose entries written by the humans and models working on the program, one
 * file per day. Like every other source in this console the directory is
 * optional: a checkout with no `web/content/log/` renders an empty log rather
 * than failing to build.
 *
 * These accessors read frontmatter and nothing else. They never derive a
 * statistic, fill a missing field, or reorder an author's numbers — the log is
 * narrative, and the authoritative records stay in research/.
 */

import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import matter from "gray-matter";

export const LOG_DIR = join(process.cwd(), "content", "log");

/** Entry filenames are exactly YYYY-MM-DD.mdx; the stem is the route param. */
const DATE_PATTERN = /^\d{4}-\d{2}-\d{2}$/;

export interface LogEntryInfo {
  /** Canonical date, taken from the filename: "2026-08-21". */
  date: string;
  title: string;
  /** Absent when the entry has no `summary` — never substituted. */
  summary: string | null;
  contributors: string[];
  tags: string[];
  /** Author-recorded outcome tallies, e.g. { negative: 4, positive: 1 }. */
  outcomes: Record<string, number> | null;
}

function isDateSlug(value: string): boolean {
  return DATE_PATTERN.test(value);
}

/** Frontmatter lists may be authored as a YAML list or a single scalar. */
function toStringList(value: unknown): string[] {
  if (Array.isArray(value)) {
    return value.map((item) => String(item).trim()).filter((item) => item.length > 0);
  }
  if (typeof value === "string" && value.trim().length > 0) return [value.trim()];
  return [];
}

/**
 * Outcome tallies are the author's own counts. Anything that is not a finite
 * number is dropped rather than coerced, so a malformed field renders as
 * absent instead of as zero.
 */
function toOutcomes(value: unknown): Record<string, number> | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const out: Record<string, number> = {};
  for (const [key, raw] of Object.entries(value as Record<string, unknown>)) {
    if (typeof raw === "number" && Number.isFinite(raw)) out[key] = raw;
  }
  return Object.keys(out).length > 0 ? out : null;
}

function toSummary(value: unknown): string | null {
  return typeof value === "string" && value.trim().length > 0 ? value.trim() : null;
}

function describe(date: string, data: Record<string, unknown>): LogEntryInfo {
  return {
    date,
    title: typeof data.title === "string" && data.title.trim().length > 0 ? data.title.trim() : date,
    summary: toSummary(data.summary),
    contributors: toStringList(data.contributors),
    tags: toStringList(data.tags),
    outcomes: toOutcomes(data.outcomes),
  };
}

/** Every entry, newest first. Empty when the directory is absent. */
export function listLogEntries(): LogEntryInfo[] {
  if (!existsSync(LOG_DIR)) return [];
  return readdirSync(LOG_DIR)
    .filter((file) => file.endsWith(".mdx"))
    .map((file) => file.replace(/\.mdx$/, ""))
    .filter(isDateSlug)
    .sort()
    .reverse()
    .map((date) => {
      const { data } = matter(readFileSync(join(LOG_DIR, `${date}.mdx`), "utf8"));
      return describe(date, data as Record<string, unknown>);
    });
}

/** Just the dates, newest first — for generateStaticParams. */
export function listLogDates(): string[] {
  return listLogEntries().map((entry) => entry.date);
}

/** The parsed file, or null when the date has no entry in this checkout. */
export function loadLogEntry(date: string) {
  if (!isDateSlug(date)) return null;
  const path = join(LOG_DIR, `${date}.mdx`);
  if (!existsSync(path)) return null;
  return matter(readFileSync(path, "utf8"));
}

/** Frontmatter of one entry in the same shape the index uses. */
export function getLogEntryInfo(date: string): LogEntryInfo | null {
  const doc = loadLogEntry(date);
  if (!doc) return null;
  return describe(date, doc.data as Record<string, unknown>);
}

/** Adjacent days: `older` is the previous entry, `newer` the following one. */
export function getLogNeighbors(date: string): {
  older: LogEntryInfo | null;
  newer: LogEntryInfo | null;
} {
  const entries = listLogEntries();
  const index = entries.findIndex((entry) => entry.date === date);
  if (index < 0) return { older: null, newer: null };
  return {
    older: entries[index + 1] ?? null,
    newer: index > 0 ? entries[index - 1] ?? null : null,
  };
}

/**
 * "Friday, 21 August 2026". Formatted in UTC so the day never shifts with the
 * reader's timezone.
 */
export function formatLogDate(date: string): string {
  if (!isDateSlug(date)) return date;
  const [year, month, day] = date.split("-").map(Number);
  return new Date(Date.UTC(year, month - 1, day)).toLocaleDateString("en-GB", {
    timeZone: "UTC",
    weekday: "long",
    day: "numeric",
    month: "long",
    year: "numeric",
  });
}
