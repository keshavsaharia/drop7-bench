/**
 * Every route's title block: a mono breadcrumb, the h1, an optional one-
 * sentence lead, and an optional label row (badges, ids). Styled by the
 * `.page-*` block in globals.css.
 */
import Link from "next/link";
import type { ReactNode } from "react";

export interface Crumb {
  href: string;
  label: string;
}

export interface PageHeaderProps {
  /** Ancestors only; the current page is the title. */
  crumbs?: readonly Crumb[];
  title: ReactNode;
  lead?: ReactNode;
  /** The badge row. */
  children?: ReactNode;
  className?: string;
}

export function PageHeader({ crumbs = [], title, lead, children, className }: PageHeaderProps) {
  return (
    <header className={className ? `page-header ${className}` : "page-header"}>
      {crumbs.length > 0 && (
        <nav aria-label="Breadcrumb" className="page-crumbs label">
          <ol>
            {crumbs.map((crumb) => (
              <li key={crumb.href}>
                <Link href={crumb.href}>{crumb.label}</Link>
              </li>
            ))}
          </ol>
        </nav>
      )}
      <h1 className="page-title">{title}</h1>
      {lead && <p className="page-lead">{lead}</p>}
      {children && <div className="page-labels">{children}</div>}
    </header>
  );
}
