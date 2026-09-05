/**
 * The reading frame for approach, record and documentation pages: a prose
 * column with a sticky aside at ≥70rem (`minmax(0, --container-prose)
 * --container-aside`), and below that the aside collapses into a
 * `<details class="toc">` above the prose. The aside holds the table of
 * contents built from `toc` and anything passed as `aside` (a record's
 * fields, source links). OpenOnHash is mounted once here so links to an
 * anchor inside a Reveal open it. Styled by `.article*` in globals.css.
 */
import type { ReactNode } from "react";
import { OpenOnHash } from "./OpenOnHash";
import { Toc, type TocItem } from "./Toc";

export interface ArticleLayoutProps {
  children: ReactNode;
  /** Headings for the "On this page" list; from `extractHeadings`. */
  toc?: readonly TocItem[];
  tocTitle?: string;
  tocDepth?: 2 | 3;
  /** Extra aside content, rendered under the table of contents. Wrap groups in `.aside-block`. */
  aside?: ReactNode;
  className?: string;
}

export function ArticleLayout({
  children,
  toc,
  tocTitle = "On this page",
  tocDepth = 2,
  aside,
  className,
}: ArticleLayoutProps) {
  const hasToc = (toc?.length ?? 0) > 0;
  const hasAside = hasToc || aside !== undefined;
  const asideContent = (
    <>
      {hasToc && <Toc items={toc ?? []} title={tocTitle} maxDepth={tocDepth} />}
      {aside}
    </>
  );
  return (
    <div className={className ? `article ${className}` : "article"}>
      {hasAside && (
        <details className="toc article-toc-collapsed">
          <summary className="label">{tocTitle}</summary>
          <div className="toc-body">{asideContent}</div>
        </details>
      )}
      <div className="article-main">{children}</div>
      {hasAside && <aside className="article-aside">{asideContent}</aside>}
      <OpenOnHash />
    </div>
  );
}
