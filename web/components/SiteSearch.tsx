"use client";
/**
 * Site search. The header button (and Command-K, or Control-K) opens a modal
 * dialog that filters every approach, technique, engine, concept, glossary
 * term, document, record and log entry the repository holds.
 *
 * The index is fetched from /api/search the first time the dialog opens, so
 * no page pays for it up front, and it is same-origin, so the console still
 * works with no network. Matching and ranking happen in the browser; nothing
 * here computes or displays a research number.
 *
 * Built on <dialog>, which supplies the focus trap, the backdrop and Escape.
 * Styled by the `.search-*` block in app/globals.css.
 */
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useCallback, useEffect, useId, useMemo, useRef, useState } from "react";

interface SearchEntry {
  kind: string;
  title: string;
  href: string;
  summary: string;
  tags: string[];
}

/** Reading order for the result groups: what a visitor most likely wants first. */
const KIND_ORDER = [
  "approach",
  "technique",
  "engine",
  "concept",
  "guide",
  "glossary",
  "doc",
  "theory",
  "experiment",
  "result",
  "log",
];

const KIND_LABEL: Record<string, string> = {
  approach: "Approaches",
  technique: "Techniques",
  engine: "Engines",
  concept: "Concepts",
  guide: "Guides",
  glossary: "Glossary",
  doc: "Documents",
  theory: "Theories",
  experiment: "Experiments",
  result: "Results",
  log: "Log",
};

const MAX_RESULTS = 40;

/**
 * Title matches outrank tag matches, which outrank summary matches, and an
 * earlier match outranks a later one. Every query word must appear somewhere.
 */
function score(entry: SearchEntry, words: string[]): number {
  const title = entry.title.toLowerCase();
  const summary = entry.summary.toLowerCase();
  const tags = entry.tags.join(" ").toLowerCase();
  let total = 0;
  for (const word of words) {
    const inTitle = title.indexOf(word);
    const inTags = tags.indexOf(word);
    const inSummary = summary.indexOf(word);
    if (inTitle === 0) total += 120;
    else if (inTitle > 0) total += 70 - Math.min(inTitle, 40);
    else if (inTags >= 0) total += 35;
    else if (inSummary >= 0) total += 20 - Math.min(inSummary / 20, 15);
    else return -1;
  }
  return total;
}

function SearchIcon() {
  return (
    <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="1.6" aria-hidden="true">
      <circle cx="7" cy="7" r="4.5" />
      <path d="M10.5 10.5L14 14" strokeLinecap="round" />
    </svg>
  );
}

export function SiteSearch() {
  const dialogRef = useRef<HTMLDialogElement | null>(null);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const listId = useId();
  const router = useRouter();

  const [entries, setEntries] = useState<SearchEntry[] | null>(null);
  const [loading, setLoading] = useState(false);
  const [failed, setFailed] = useState(false);
  const [query, setQuery] = useState("");
  // The cursor is tied to the query that produced it, so a new query resets
  // it during render rather than in an effect.
  const [cursor, setCursor] = useState({ query: "", index: 0 });

  const load = useCallback(() => {
    if (entries !== null || loading) return;
    setLoading(true);
    fetch("/api/search")
      .then((response) => (response.ok ? response.json() : Promise.reject(new Error(String(response.status)))))
      .then((data: { entries: SearchEntry[] }) => setEntries(data.entries))
      .catch(() => setFailed(true))
      .finally(() => setLoading(false));
  }, [entries, loading]);

  const open = useCallback(() => {
    load();
    const dialog = dialogRef.current;
    if (dialog && !dialog.open) dialog.showModal();
    window.requestAnimationFrame(() => inputRef.current?.focus());
  }, [load]);

  const close = useCallback(() => {
    dialogRef.current?.close();
  }, []);

  // Command-K anywhere, except while the visitor is typing somewhere else.
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        open();
      }
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [open]);

  const results = useMemo(() => {
    const words = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (words.length === 0 || !entries) return [];
    return entries
      .map((entry) => ({ entry, rank: score(entry, words) }))
      .filter((row) => row.rank >= 0)
      .sort((a, b) => b.rank - a.rank || a.entry.title.localeCompare(b.entry.title))
      .slice(0, MAX_RESULTS)
      .map((row) => row.entry);
  }, [entries, query]);

  // Group in reading order, keeping each group's rank order.
  const groups = useMemo(() => {
    const byKind = new Map<string, SearchEntry[]>();
    for (const entry of results) {
      const list = byKind.get(entry.kind);
      if (list) list.push(entry);
      else byKind.set(entry.kind, [entry]);
    }
    const known = KIND_ORDER.filter((kind) => byKind.has(kind));
    const rest = [...byKind.keys()].filter((kind) => !KIND_ORDER.includes(kind));
    return [...known, ...rest].map((kind) => ({ kind, entries: byKind.get(kind) ?? [] }));
  }, [results]);

  // Flat order matches what the arrow keys walk.
  const flat = useMemo(() => groups.flatMap((group) => group.entries), [groups]);
  const active = cursor.query === query ? cursor.index : 0;
  const setActive = (next: number) => setCursor({ query, index: next });

  function onInputKeyDown(event: React.KeyboardEvent<HTMLInputElement>) {
    if (flat.length === 0) return;
    if (event.key === "ArrowDown") {
      event.preventDefault();
      setActive((active + 1) % flat.length);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      setActive((active - 1 + flat.length) % flat.length);
    } else if (event.key === "Enter") {
      event.preventDefault();
      const target = flat[active];
      if (target) {
        close();
        setQuery("");
        router.push(target.href);
      }
    }
  }

  return (
    <>
      <button type="button" className="search-open" onClick={open} aria-label="Search the site">
        <SearchIcon />
        <span className="search-open-text">Search</span>
        <kbd className="search-open-key" aria-hidden="true">
          ⌘K
        </kbd>
      </button>

      <dialog
        ref={dialogRef}
        className="search-dialog"
        aria-label="Search"
        onClose={() => setQuery("")}
        onClick={(event) => {
          // A click on the backdrop lands on the dialog element itself.
          if (event.target === dialogRef.current) close();
        }}
      >
        <div className="search-panel">
          <div className="search-field">
            <SearchIcon />
            <input
              ref={inputRef}
              type="search"
              className="search-input"
              placeholder="Approaches, techniques, records, documents"
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              onKeyDown={onInputKeyDown}
              aria-controls={listId}
              aria-describedby={`${listId}-status`}
              autoComplete="off"
              spellCheck={false}
            />
            <button type="button" className="search-close" onClick={close}>
              Close
            </button>
          </div>

          <p id={`${listId}-status`} className="search-status" role="status">
            {failed
              ? "The search index could not be loaded."
              : loading && entries === null
                ? "Loading the index."
                : query.trim() === ""
                  ? "Type to search every page, record and glossary term."
                  : flat.length === 0
                    ? "Nothing matches that."
                    : `${flat.length} ${flat.length === 1 ? "match" : "matches"}`}
          </p>

          <div className="search-results" id={listId}>
            {groups.map((group) => (
              <section key={group.kind} className="search-group">
                <span className="label">{KIND_LABEL[group.kind] ?? group.kind}</span>
                <ul>
                  {group.entries.map((entry) => {
                    const index = flat.indexOf(entry);
                    return (
                      <li key={`${entry.kind}-${entry.href}-${entry.title}`}>
                        <Link
                          href={entry.href}
                          className="search-hit"
                          data-active={index === active ? "true" : undefined}
                          onClick={() => {
                            close();
                            setQuery("");
                          }}
                          onMouseEnter={() => setActive(index)}
                        >
                          <span className="search-hit-title">{entry.title}</span>
                          {entry.summary && <span className="search-hit-summary">{entry.summary}</span>}
                        </Link>
                      </li>
                    );
                  })}
                </ul>
              </section>
            ))}
          </div>
        </div>
      </dialog>
    </>
  );
}
