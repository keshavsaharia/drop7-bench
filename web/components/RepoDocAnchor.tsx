import Link from "next/link";
import type { ComponentPropsWithoutRef } from "react";
import { rewriteRepoDocHref } from "@/lib/doc-links";

type Props = ComponentPropsWithoutRef<"a"> & {
  fromPath?: string;
  node?: unknown;
};

/** Anchor that maps repository `docs/*.md` hrefs onto `/docs/<slug>`. */
export function RepoDocAnchor({ href, fromPath, children, ...rest }: Props) {
  // react-markdown passes the hast node on every element; it is not a DOM prop.
  const { node, ...props } = rest as typeof rest & { node?: unknown };
  void node;
  const next = rewriteRepoDocHref(href ?? "", fromPath);
  if (next.startsWith("/") && !next.startsWith("//")) {
    return (
      <Link href={next} className={props.className}>
        {children}
      </Link>
    );
  }
  return (
    <a href={next || undefined} {...props}>
      {children}
    </a>
  );
}
