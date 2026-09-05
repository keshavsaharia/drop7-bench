/**
 * <StatRow columns?>…tiles…</StatRow>
 *
 * The grid stat tiles sit in: `repeat(auto-fit, minmax(180px, 1fr))` with a
 * 12 px gap, written as a scoped class in charts.css because utilities lose
 * inside .prose-drop7. `columns` picks a wider or narrower minimum.
 */
import type { ReactNode } from "react";

export function StatRow({ children, columns }: { children: ReactNode; columns?: 2 | 3 | 4 }) {
  return <div className={columns ? `stat-row is-columns-${columns}` : "stat-row"}>{children}</div>;
}
