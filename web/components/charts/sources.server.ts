/**
 * Server-only: where a source record can be opened in the console. Result
 * records (RS-) have no route of their own and render on their experiment's
 * page, so this reads the record to find the experiment id. Everything else
 * goes through the pure sourceHref in web/lib/charts/provenance.ts. Imported
 * by server components only (Figure, StatTile, FigureGrid); the client kit
 * receives the resulting map as a prop.
 */
import { readRepoFile } from "@/lib/repo";
import { sourceHref } from "@/lib/charts/provenance";

export function resultHref(id: string): string | null {
  if (!id.startsWith("RS-")) return null;
  const raw = readRepoFile(`research/results/${id}.json`);
  if (!raw) return null;
  try {
    const record = JSON.parse(raw) as { experimentId?: string };
    return record.experimentId ? `/experiments/${record.experimentId}#${id}` : null;
  } catch {
    return null;
  }
}

/** id -> console route, for every id that has one. */
export function sourceLinks(ids: Iterable<string>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const id of new Set(ids)) {
    const href = resultHref(id) ?? sourceHref(id);
    if (href) out[id] = href;
  }
  return out;
}

/** Split "RS-… · metrics.field" into its id and field. */
export function splitSource(source: string): { id: string; field?: string } {
  const [id, ...rest] = source.split(" · ");
  return rest.length ? { id: id.trim(), field: rest.join(" · ").trim() } : { id: id.trim() };
}
