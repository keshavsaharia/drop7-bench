/**
 * <Figure name="score-vs-depth" caption="…" />
 *
 * Renders a research figure from its spec, web/content/figures/<name>.json,
 * through the chart kit (web/components/charts/). The spec is validated
 * here on the server; every plotted value carries a `sourceRecord` or the
 * spec is refused and an EmptyState says why. This component computes no
 * number: the chart draws the values as written and the table view
 * (frame/TableView.tsx, behind the frame's Chart/Table toggle) lists them as
 * written. Under the caption, a "Notes and sources" block carries the spec's
 * notes and links every source record the console can open.
 *
 * Server component; the chart is a client component that receives the
 * parsed spec as a prop. Styled by charts.css plus the `.fig*` frame blocks
 * in globals.css.
 */
import { readRepoFile } from "@/lib/repo";
import { sourceRecords, validateFigureSpec, type FigureSpec } from "@/lib/charts/spec";
import { EmptyState } from "./charts/frame/EmptyState";
import { SourceRef } from "./charts/frame/SourceRef";
import { TableView } from "./charts/frame/TableView";
import { ResearchChart } from "./charts/ResearchChart";
import { sourceLinks } from "./charts/sources.server";

const NAME = /^[a-z0-9-]+$/;

export function specPath(name: string): string {
  return `web/content/figures/${name}.json`;
}

export function loadSpec(name: string): { spec: FigureSpec | null; error: string | null; exists: boolean } {
  if (!NAME.test(name)) return { spec: null, error: "not a valid figure name (lowercase letters, digits and hyphens)", exists: false };
  const raw = readRepoFile(specPath(name));
  if (!raw) return { spec: null, error: null, exists: false };
  try {
    return { spec: validateFigureSpec(JSON.parse(raw), name), error: null, exists: true };
  } catch (error) {
    return { spec: null, error: error instanceof Error ? error.message : String(error), exists: true };
  }
}

export interface FigureProps {
  name: string;
  caption?: string;
  /** Break out to the wide column (default); off inside a FigureGrid. */
  wide?: boolean;
  compact?: boolean;
}

export function Figure({ name, caption, wide = true, compact }: FigureProps) {
  const { spec, error, exists } = loadSpec(name);
  const frameClass = `fig fig-frame research-fig rchart-figure${wide ? " fig--wide" : ""}`;

  if (!spec) {
    return (
      <figure className={`${frameClass} research-fig-missing`}>
        {exists || error ? <EmptyState reason="invalid-spec" what={name} detail={error ?? undefined} how={specPath(name)} /> : <EmptyState reason="no-spec" what={name} how={specPath(name)} />}
        {caption && <figcaption>{caption}</figcaption>}
      </figure>
    );
  }

  const sources = sourceRecords(spec);
  const links = sourceLinks(sources);
  const id = `fig-${name}`;

  return (
    <figure className={frameClass}>
      <ResearchChart spec={spec} id={id} compact={compact} table={<TableView spec={spec} links={links} id={`${id}-table`} />} />
      {caption && <figcaption>{caption}</figcaption>}
      <details className="rchart-data">
        <summary>Notes and sources</summary>
        {spec.notes && <p className="rchart-data-notes">{spec.notes}</p>}
        <ul className="rchart-data-sources">
          {sources.map((source) => (
            <li key={source}>
              <SourceRef id={source} href={links[source] ?? null} />
            </li>
          ))}
        </ul>
        <p className="rchart-data-spec">
          Spec: <code>{specPath(name)}</code> · {sources.length} source record{sources.length === 1 ? "" : "s"} · kind {spec.kind}
        </p>
      </details>
    </figure>
  );
}
