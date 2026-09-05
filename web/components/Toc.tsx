/**
 * "On this page": a server-rendered list of heading anchors. Pass the
 * headings from `extractHeadings` (web/lib/headings.ts); ids match the ones
 * rehype-slug writes into the rendered MDX. Depth 2 by default; `maxDepth`
 * 3 nests h3 entries. Styled by `.toc-nav` in globals.css.
 */
export interface TocItem {
  id: string;
  text: string;
  /** Heading level; defaults to 2. */
  depth?: number;
}

export interface TocProps {
  items: readonly TocItem[];
  title?: string;
  /** Deepest heading level to list. */
  maxDepth?: 2 | 3;
  /** Marks the current entry (aria-current). */
  activeId?: string;
}

export function Toc({ items, title = "On this page", maxDepth = 2, activeId }: TocProps) {
  const list = items.filter((item) => (item.depth ?? 2) >= 2 && (item.depth ?? 2) <= maxDepth);
  if (list.length === 0) return null;
  return (
    <nav className="toc-nav" aria-label={title}>
      <span className="label">{title}</span>
      <ol>
        {list.map((item) => (
          <li key={item.id} data-depth={item.depth ?? 2}>
            <a href={`#${item.id}`} aria-current={activeId === item.id ? "true" : undefined}>
              {item.text}
            </a>
          </li>
        ))}
      </ol>
    </nav>
  );
}
