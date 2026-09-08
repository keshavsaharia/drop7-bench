"use client";

import { useMemo, useState } from "react";
import {
  Background,
  Controls,
  Handle,
  MarkerType,
  Position,
  ReactFlow,
} from "@xyflow/react";
import type { Node, NodeProps } from "@xyflow/react";
import { buildNavigationTree, landingPages } from "@/lib/analytics/navigation";
import type { FlowPoint, NavigationData } from "@/lib/analytics/navigation";
import type { AnalyticsAudience, AnalyticsRange } from "@/lib/analytics/types";
import {
  EmptyState,
  ErrorNotice,
  PanelHeading,
  percentage,
  QueryFootnote,
  useAnalyticsQuery,
} from "./shared";
import "@xyflow/react/dist/style.css";

type PageNode = Node<FlowPoint & Record<string, unknown>, "page">;
function NavigationNode({ data, selected }: NodeProps<PageNode>) {
  return (
    <div
      className={`analytics-flow-node analytics-flow-node--${data.kind}${selected ? " is-selected" : ""}`}
    >
      {data.depth > 0 && (
        <Handle type="target" position={Position.Left} isConnectable={false} />
      )}
      <span className="analytics-flow-step">
        {data.kind === "page"
          ? `Page ${data.pages.length}`
          : data.kind === "root"
            ? "Entry"
            : "Observed outcome"}
      </span>
      <strong title={data.label}>
        {data.label === "/" ? "/ · Home" : data.label}
      </strong>
      <span className="analytics-flow-count">
        {data.sessions.toLocaleString("en")} sessions
      </span>
      {data.kind !== "end" && data.kind !== "other" && (
        <Handle type="source" position={Position.Right} isConnectable={false} />
      )}
    </div>
  );
}
const nodeTypes = { page: NavigationNode };

export function AnalyticsNavigation({
  range,
  audience,
}: {
  range: AnalyticsRange;
  audience: AnalyticsAudience;
}) {
  const { data, error } = useAnalyticsQuery<NavigationData>({
    mode: "navigation",
    range,
    audience,
  });
  return (
    <section className="analytics-card analytics-navigation">
      <PanelHeading
        title="Navigation paths"
        description="Follow the next page people view, one step at a time."
      >
        <span className="analytics-tag">Interactive</span>
      </PanelHeading>
      {error ? (
        <ErrorNotice message={error} />
      ) : !data ? (
        <div className="analytics-flow-loading" role="status">
          Building navigation paths…
        </div>
      ) : !data.paths.length ? (
        <EmptyState label="No page sequences in this period. Paths appear once visits have been recorded." />
      ) : (
        <NavigationExplorer data={data} />
      )}
    </section>
  );
}

function NavigationExplorer({ data }: { data: NavigationData }) {
  const landings = useMemo(() => landingPages(data.paths), [data.paths]);
  const [landing, setLanding] = useState(
    landings.some((item) => item.page === "/") ? "/" : landings[0].page,
  );
  const [depth, setDepth] = useState(3);
  const [breadth, setBreadth] = useState(3);
  const [selected, setSelected] = useState<string | null>(null);
  const graph = useMemo(
    () => buildNavigationTree(data.paths, landing, depth, breadth),
    [data.paths, landing, depth, breadth],
  );
  const nodes: PageNode[] = graph.nodes.map((point) => ({
    id: point.id,
    type: "page",
    data: { ...point },
    position: { x: point.x, y: point.y },
    selected: selected === point.id,
    ariaLabel: `${point.label}, ${point.sessions} sessions, ${percentage(point.sessions, point.parentSessions)} of preceding step`,
  }));
  const edges = graph.edges.map((edge) => ({
    id: `${edge.source}-${edge.target}`,
    source: edge.source,
    target: edge.target,
    type: "smoothstep",
    label: `${edge.percentage.toFixed(1)}%`,
    markerEnd: { type: MarkerType.ArrowClosed, color: "var(--color-accent)" },
    style: {
      stroke: "var(--color-accent)",
      strokeWidth: 1 + edge.percentage / 35,
    },
    labelStyle: { fill: "var(--color-ink-1)", fontSize: 12, fontWeight: 600 },
    labelBgStyle: { fill: "var(--color-surface)" },
    labelBgPadding: [7, 4] as [number, number],
    labelBgBorderRadius: 4,
    ariaLabel: `${edge.sessions} sessions, ${edge.percentage.toFixed(1)} percent of preceding step`,
  }));
  const selectedPoint = graph.nodes.find((node) => node.id === selected);
  function resetSelection() {
    setSelected(null);
  }
  return (
    <>
      <div className="analytics-flow-toolbar">
        <label className="analytics-filter">
          <span>Landing page</span>
          <select
            aria-label="Landing page"
            value={landing}
            onChange={(e) => {
              setLanding(e.target.value);
              resetSelection();
            }}
          >
            <option value="">All landing pages</option>
            {landings.map(({ page, sessions }) => (
              <option key={page} value={page}>
                {page} ({sessions.toLocaleString("en")})
              </option>
            ))}
          </select>
        </label>
        <label className="analytics-filter">
          <span>Pages</span>
          <select
            aria-label="Pages"
            value={depth}
            onChange={(e) => {
              setDepth(Number(e.target.value));
              resetSelection();
            }}
          >
            {[2, 3, 4, 5, 6].map((n) => (
              <option key={n} value={n}>
                {n} pages
              </option>
            ))}
          </select>
        </label>
        <label className="analytics-filter">
          <span>Branches</span>
          <select
            aria-label="Branches"
            value={breadth}
            onChange={(e) => {
              setBreadth(Number(e.target.value));
              resetSelection();
            }}
          >
            <option value={3}>Top 3</option>
            <option value={5}>Top 5</option>
            <option value={8}>Top 8</option>
          </select>
        </label>
        <span className="analytics-flow-total">
          {graph.nodes[0]?.sessions.toLocaleString("en")} sessions in view
        </span>
      </div>
      <div
        className="analytics-flow-canvas"
        role="region"
        aria-label="Interactive navigation paths"
      >
        <ReactFlow
          key={`${landing}-${depth}-${breadth}`}
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          fitView
          fitViewOptions={{ padding: 0.12, minZoom: 0.45, maxZoom: 1 }}
          minZoom={0.08}
          maxZoom={1.8}
          nodesDraggable={false}
          nodesConnectable={false}
          edgesReconnectable={false}
          deleteKeyCode={null}
          zoomOnScroll={false}
          preventScrolling={false}
          onNodeClick={(_, node) => setSelected(node.id)}
          onPaneClick={resetSelection}
          colorMode="dark"
        >
          <Background gap={22} size={1} color="var(--color-rule-strong)" />
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
      <div className="analytics-flow-inspector" aria-live="polite">
        {selectedPoint ? (
          <>
            <strong>
              {selectedPoint.pages.join(" → ") || selectedPoint.label}
              {selectedPoint.kind === "end" || selectedPoint.kind === "other"
                ? ` → ${selectedPoint.label}`
                : ""}
            </strong>
            <span>
              {selectedPoint.sessions.toLocaleString("en")} sessions ·{" "}
              {percentage(selectedPoint.sessions, selectedPoint.parentSessions)}{" "}
              of the preceding step
            </span>
          </>
        ) : (
          <>
            <strong>Select a node to inspect its path</strong>
            <span>Drag to pan · Use the controls or pinch to zoom</span>
          </>
        )}
      </div>
      <div className="analytics-flow-notes">
        <p>
          Each edge is the share of sessions at the preceding node. “Other
          destinations” keeps the less common branches in that total. “No next
          page” means no later view was observed in the period.
        </p>
        <p>
          {data.representedSessions.toLocaleString("en")} of{" "}
          {data.totalSessions.toLocaleString("en")} inferred sessions
          represented (
          {percentage(data.representedSessions, data.totalSessions)}).{" "}
          {data.representedSessions < data.totalSessions
            ? "Only the 400 most frequent sequences are included; percentages describe this subset."
            : "All recorded sequences are represented."}
        </p>
        <details className="analytics-details">
          <summary>How to read this view</summary>
          <p>
            A new session starts after 30 minutes without a page view. The first
            page is the first one observed in the selected period, which can cut
            across an existing visit. The map shows up to six page views per
            session, including reloads and return visits.
          </p>
          <p>
            Sessions are inferred from the existing anonymous visitor hash.
            Shared networks and overlapping tabs can mix paths, and missed
            events can leave gaps. These are page-view sequences; clicks within
            a page are not collected. A branch at the page limit may continue
            further.
          </p>
        </details>
      </div>
      <details className="analytics-details analytics-flow-table">
        <summary>View paths as a table</summary>
        <div className="analytics-table-scroll">
          <table className="analytics-table">
            <thead>
              <tr>
                <th>Observed path</th>
                <th>Sessions</th>
                <th>Share of preceding step</th>
              </tr>
            </thead>
            <tbody>
              {graph.nodes.map((point) => (
                <tr key={point.id}>
                  <td>
                    {point.pages.join(" → ") || point.label}
                    {point.kind === "end" || point.kind === "other"
                      ? ` → ${point.label}`
                      : ""}
                  </td>
                  <td>{point.sessions.toLocaleString("en")}</td>
                  <td>{percentage(point.sessions, point.parentSessions)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </details>
      <QueryFootnote query={data.query} />
    </>
  );
}
