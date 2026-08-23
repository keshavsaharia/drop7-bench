"use client";

/**
 * Interactive look-ahead tree explorer. Everything it shows was precomputed by
 * the TypeScript engine (web/scripts/generate-concept-scenarios.ts); the
 * component only lets the reader walk the tree: pick a column, see the seven
 * possible next discs, pick one, see the best replies ranked. No number is
 * computed client-side beyond re-displaying the precomputed values.
 */

import { useState } from "react";

interface Reply { column: number; points: number; board: string; waves: number }
interface Branch { disc: number; replies: Reply[]; best: Reply | null }
interface ColumnNode {
  column: number;
  legal: boolean;
  points?: number;
  board?: string;
  waves?: number;
  branches?: Branch[];
  fair?: number;
  optimistic?: number;
  pessimistic?: number;
}
export interface TreeData {
  board: string;
  nextDisc: number;
  columns: ColumnNode[];
  choice: { greedy: number; fair: number; optimistic: number; pessimistic: number };
}

const DISC_COLORS: Record<number, string> = {
  1: "#f97316", 2: "#eab308", 3: "#22c55e", 4: "#06b6d4", 5: "#3b82f6", 6: "#a855f7", 7: "#ec4899",
};

function Cell({ v, s }: { v: number; s: number }) {
  if (v === 0) return <div style={{ width: s, height: s, borderRadius: 3, background: "#111827" }} />;
  if (v === 8 || v === 9)
    return (
      <div style={{ width: s, height: s, borderRadius: 3, background: v === 9 ? "#6b7280" : "#4b5563", display: "grid", placeItems: "center", fontSize: s * 0.5, color: "#d1d5db", fontWeight: 700 }}>
        {v === 9 ? "⟋" : "?"}
      </div>
    );
  return (
    <div style={{ width: s, height: s, borderRadius: "50%", background: DISC_COLORS[v], display: "grid", placeItems: "center", fontSize: s * 0.55, color: "#fff", fontWeight: 800 }}>
      {v}
    </div>
  );
}

function Board({ cells, s = 18, highlightColumn }: { cells: string; s?: number; highlightColumn?: number | null }) {
  const values = [...cells].map(Number);
  return (
    <div style={{ display: "grid", gridTemplateColumns: `repeat(7, ${s}px)`, gap: 2, padding: 4, background: "#030712", borderRadius: 6, width: "max-content" }}>
      {values.map((v, i) => (
        <div key={i} style={{ background: highlightColumn === i % 7 ? "rgba(250,204,21,0.12)" : "transparent", borderRadius: 3 }}>
          <Cell v={v} s={s} />
        </div>
      ))}
    </div>
  );
}

function fmt(n: number) {
  return Number.isInteger(n) ? n.toLocaleString("en-US") : n.toFixed(1);
}

export function TreeExplorer({ data }: { data: TreeData }) {
  const [column, setColumn] = useState<number | null>(null);
  const [disc, setDisc] = useState<number | null>(null);
  const node = column === null ? null : data.columns.find((c) => c.column === column) ?? null;
  const branch = node && disc !== null ? node.branches?.find((b) => b.disc === disc) ?? null : null;

  const btn = (active: boolean): React.CSSProperties => ({
    border: `1px solid ${active ? "#facc15" : "#27272a"}`,
    background: active ? "rgba(250,204,21,0.12)" : "#18181b",
    color: "#e4e4e7",
    borderRadius: 8,
    padding: "6px 8px",
    cursor: "pointer",
    font: "inherit",
    fontSize: 12,
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    gap: 4,
  });

  return (
    <div className="engine-fig" style={{ fontSize: 13, color: "#d4d4d8" }}>
      <div style={{ display: "flex", flexWrap: "wrap", gap: 16, alignItems: "flex-start" }}>
        <div>
          <div style={{ fontSize: 11, color: "#71717a", marginBottom: 4 }}>
            the position · next disc is a {data.nextDisc}
          </div>
          <Board cells={data.board} s={22} highlightColumn={column} />
        </div>
        <div style={{ flex: 1, minWidth: 260 }}>
          <div style={{ fontSize: 11, color: "#71717a", marginBottom: 6 }}>
            1 · choose a column for the {data.nextDisc}
          </div>
          <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
            {data.columns.map((c) => (
              <button
                key={c.column}
                type="button"
                disabled={!c.legal}
                onClick={() => {
                  setColumn(c.column);
                  setDisc(null);
                }}
                style={{ ...btn(column === c.column), opacity: c.legal ? 1 : 0.35 }}
                aria-pressed={column === c.column}
              >
                <span style={{ color: "#a1a1aa" }}>col {c.column + 1}</span>
                {c.legal && c.board ? <Board cells={c.board} s={8} /> : <span>full</span>}
                {c.legal && (
                  <span style={{ fontWeight: 700, color: "#fafafa" }}>+{fmt(c.points ?? 0)} now</span>
                )}
              </button>
            ))}
          </div>
          {node && node.legal && (
            <div style={{ marginTop: 10, fontSize: 12, color: "#a1a1aa" }}>
              Column {node.column + 1} scores <strong style={{ color: "#fafafa" }}>+{fmt(node.points ?? 0)}</strong> immediately.
              Looking one move further and averaging over the next disc, its fair value is{" "}
              <strong style={{ color: "#fafafa" }}>{fmt(node.fair ?? 0)}</strong>
              {" "}(optimistic {fmt(node.optimistic ?? 0)}, pessimistic {fmt(node.pessimistic ?? 0)}).
            </div>
          )}
        </div>
      </div>

      {node && node.legal && node.branches && (
        <div style={{ marginTop: 14, borderTop: "1px solid #27272a", paddingTop: 10 }}>
          <div style={{ fontSize: 11, color: "#71717a", marginBottom: 6 }}>
            2 · chance: which disc comes next? (each is one in seven) — click one
          </div>
          <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
            {node.branches.map((b) => (
              <button key={b.disc} type="button" onClick={() => setDisc(b.disc)} style={btn(disc === b.disc)} aria-pressed={disc === b.disc}>
                <Cell v={b.disc} s={22} />
                <span style={{ color: "#a1a1aa" }}>best reply</span>
                <span style={{ fontWeight: 700, color: "#fafafa" }}>+{fmt(b.best?.points ?? 0)}</span>
              </button>
            ))}
          </div>
          <div style={{ marginTop: 8, fontSize: 12, color: "#a1a1aa" }}>
            average of the seven best replies ={" "}
            <strong style={{ color: "#fafafa" }}>
              {fmt(node.branches.reduce((a, b) => a + (b.best?.points ?? 0), 0) / node.branches.length)}
            </strong>
            ; fair value = {fmt(node.points ?? 0)} + that = <strong style={{ color: "#fafafa" }}>{fmt(node.fair ?? 0)}</strong>
          </div>
        </div>
      )}

      {branch && (
        <div style={{ marginTop: 14, borderTop: "1px solid #27272a", paddingTop: 10 }}>
          <div style={{ fontSize: 11, color: "#71717a", marginBottom: 6 }}>
            3 · choice again: if a {branch.disc} comes, every legal reply, best first
          </div>
          <div style={{ display: "flex", flexWrap: "wrap", gap: 8 }}>
            {[...branch.replies]
              .sort((a, b) => b.points - a.points)
              .map((r, i) => (
                <div key={r.column} style={{ border: `1px solid ${i === 0 ? "#199e70" : "#27272a"}`, borderRadius: 8, padding: 6, background: "#18181b", display: "flex", flexDirection: "column", alignItems: "center", gap: 4, fontSize: 12 }}>
                  <span style={{ color: "#a1a1aa" }}>col {r.column + 1}</span>
                  <Board cells={r.board} s={8} />
                  <span style={{ fontWeight: 700, color: i === 0 ? "#34d399" : "#fafafa" }}>+{fmt(r.points)}</span>
                  <span style={{ color: "#71717a" }}>{r.waves === 0 ? "no clear" : `${r.waves} wave${r.waves === 1 ? "" : "s"}`}</span>
                </div>
              ))}
          </div>
          <div style={{ marginTop: 8, fontSize: 12, color: "#71717a" }}>
            A deeper search would now repeat steps 2 and 3 from each of these boards — and that is why the tree grows by 49× per move.
          </div>
        </div>
      )}
      <div style={{ marginTop: 10, fontSize: 11, color: "#71717a" }}>
        Every board and number here was computed in advance by the rules engine with a points-only evaluator; the explorer only lets you walk the tree.
      </div>
    </div>
  );
}
