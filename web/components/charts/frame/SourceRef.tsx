/**
 * A source record reference: a link when the console has a route for it
 * (experiments, theories, docs, log days) or when the caller resolved one
 * (a result record's experiment page), otherwise the id in code. Server-safe
 * and hook-free, so the table view can be passed into a client frame.
 */
import Link from "next/link";
import { sourceHref } from "@/lib/charts/provenance";

export function SourceRef({ id, field, href }: { id: string; field?: string; href?: string | null }) {
  const target = href === undefined ? sourceHref(id) : href;
  return (
    <>
      {target ? (
        <Link href={target} className="rchart-source">
          {id}
        </Link>
      ) : (
        <code className="rchart-source">{id}</code>
      )}
      {field && (
        <span className="rchart-data-field" title={field}>
          {" "}
          {field}
        </span>
      )}
    </>
  );
}
