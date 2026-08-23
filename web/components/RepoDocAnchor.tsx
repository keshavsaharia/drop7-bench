import Link from "next/link";
import type { ComponentPropsWithoutRef } from "react";
import { rewriteRepoDocHref } from "@/lib/doc-links";

type Props = ComponentPropsWithoutRef<"a"> & {
  fromPath?: string;
  node?: unknown;
};

/** Anchor that maps repository `docs/*.md` hrefs onto `/docs/<slug>`. */
export function RepoDocAnchor({ href, fromPath, children, node: _node, ...props }: Props) {
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
