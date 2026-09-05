"use client";

/**
 * The one client island on the approaches index: a text field that hides
 * every `[data-search]` card whose text does not contain the query, hides a
 * `.approach-group` once all of its cards are hidden, and shows the page's
 * `[data-search-empty]` sentence when nothing is left. Without JavaScript it
 * is an inert text field; the technique, status and reads filters are links.
 */
import { useId, useState } from "react";

export interface FilterSearchProps {
  /** Selector of the element that holds the groups and cards. */
  scope?: string;
  placeholder?: string;
}

export function FilterSearch({ scope = ".approaches-groups", placeholder = "Filter by title" }: FilterSearchProps) {
  const id = useId();
  const [shown, setShown] = useState<number | null>(null);

  function apply(query: string) {
    const root: ParentNode = document.querySelector(scope) ?? document;
    const needle = query.trim().toLowerCase();
    let visible = 0;
    root.querySelectorAll<HTMLElement>("[data-search]").forEach((card) => {
      const hit = needle === "" || (card.dataset.search ?? "").includes(needle);
      card.hidden = !hit;
      if (hit) visible += 1;
    });
    root.querySelectorAll<HTMLElement>(".approach-group").forEach((group) => {
      group.hidden = group.querySelector("[data-search]:not([hidden])") === null;
    });
    const empty = document.querySelector<HTMLElement>("[data-search-empty]");
    if (empty) empty.hidden = visible > 0;
    setShown(needle === "" ? null : visible);
  }

  return (
    <form className="approaches-search" role="search" onSubmit={(event) => event.preventDefault()}>
      <label htmlFor={id} className="label">
        Search
      </label>
      <input
        id={id}
        type="search"
        placeholder={placeholder}
        autoComplete="off"
        onChange={(event) => apply(event.target.value)}
      />
      {shown !== null && (
        <span className="approaches-search-count" aria-live="polite">
          {shown} shown
        </span>
      )}
    </form>
  );
}
