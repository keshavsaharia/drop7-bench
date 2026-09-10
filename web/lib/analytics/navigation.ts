import type { DashboardData } from "./types.ts";

export interface NavigationPath {
  pages: string[];
  sessions: number;
  endedSessions: number;
}
export interface NavigationData {
  paths: NavigationPath[];
  totalSessions: number;
  representedSessions: number;
  query: DashboardData["query"];
}
export interface FlowPoint {
  id: string;
  label: string;
  kind: "page" | "root" | "end" | "other";
  sessions: number;
  parentSessions: number;
  pages: string[];
  depth: number;
  x: number;
  y: number;
}
export interface FlowLink {
  source: string;
  target: string;
  sessions: number;
  percentage: number;
}
interface Branch {
  label: string;
  kind: FlowPoint["kind"];
  sessions: number;
  pages: string[];
  children: Map<string, Branch>;
}

export function landingPages(
  paths: NavigationPath[],
): { page: string; sessions: number }[] {
  const totals = new Map<string, number>();
  for (const path of paths)
    totals.set(path.pages[0], (totals.get(path.pages[0]) ?? 0) + path.sessions);
  return [...totals]
    .map(([page, sessions]) => ({ page, sessions }))
    .sort((a, b) => b.sessions - a.sessions || a.page.localeCompare(b.page));
}

/** Keep the complete prefix as node identity: / → /learn differs from /play → /learn. */
export function buildNavigationTree(
  paths: NavigationPath[],
  landing: string,
  maxDepth = 3,
  breadth = 3,
) {
  const root: Branch = {
    label: "All landing pages",
    kind: "root",
    sessions: 0,
    pages: [],
    children: new Map(),
  };
  for (const path of paths) {
    if (landing && path.pages[0] !== landing) continue;
    root.sessions += path.sessions;
    let branch = root;
    for (const [index, page] of path.pages.entries()) {
      const key = JSON.stringify(["page", page]);
      if (!branch.children.has(key))
        branch.children.set(key, {
          label: page,
          kind: "page",
          sessions: 0,
          pages: path.pages.slice(0, index + 1),
          children: new Map(),
        });
      branch = branch.children.get(key)!;
      branch.sessions += path.sessions;
    }
    if (path.endedSessions) {
      const key = "end";
      if (!branch.children.has(key))
        branch.children.set(key, {
          label: "No next page",
          kind: "end",
          sessions: 0,
          pages: path.pages,
          children: new Map(),
        });
      branch.children.get(key)!.sessions += path.endedSessions;
    }
  }
  const nodes: FlowPoint[] = [];
  const edges: FlowLink[] = [];
  let nextRow = 0;
  function visit(branch: Branch, parent: FlowPoint | null, depth: number) {
    const point: FlowPoint = {
      id: `flow-${nodes.length}`,
      label: branch.label,
      kind: branch.kind,
      sessions: branch.sessions,
      parentSessions: parent?.sessions ?? branch.sessions,
      pages: branch.pages,
      depth,
      x: depth * 350,
      y: 0,
    };
    nodes.push(point);
    if (parent)
      edges.push({
        source: parent.id,
        target: point.id,
        sessions: point.sessions,
        percentage: parent.sessions
          ? (point.sessions / parent.sessions) * 100
          : 0,
      });
    let children: Branch[] = [];
    if (branch.pages.length < maxDepth && branch.children.size) {
      const ranked = [...branch.children.values()].sort(
        (a, b) => b.sessions - a.sessions || a.label.localeCompare(b.label),
      );
      children = ranked.slice(0, breadth);
      const rest = ranked.slice(breadth);
      if (rest.length)
        children.push({
          label: `${rest.length} other destinations`,
          kind: "other",
          sessions: rest.reduce((sum, child) => sum + child.sessions, 0),
          pages: branch.pages,
          children: new Map(),
        });
    }
    const rows = children.map((child) => visit(child, point, depth + 1));
    point.y = rows.length
      ? (rows[0] + rows[rows.length - 1]) / 2
      : nextRow++ * 112;
    return point.y;
  }
  if (root.sessions)
    visit(landing ? [...root.children.values()][0] : root, null, 0);
  return { nodes, edges };
}
