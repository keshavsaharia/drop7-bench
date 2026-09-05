/**
 * The site search index, built from the repository on request and fetched once
 * by the header's search dialog the first time a visitor opens it. Serving it
 * from a route rather than embedding it in every page keeps the index off the
 * critical path; it is same-origin, so the console still works offline.
 */
import { buildSearchIndex } from "@/lib/search-index";

export const dynamic = "force-dynamic";

export function GET() {
  return Response.json(
    { entries: buildSearchIndex() },
    { headers: { "cache-control": "no-store" } },
  );
}
