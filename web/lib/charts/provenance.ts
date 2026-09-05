/**
 * Where a figure's `sourceRecord` lives and where a page embeds a figure.
 * Pure helpers shared by web/scripts/check-figures.mjs, the Figure frame and
 * the stat tile; the caller supplies file existence so this module stays free
 * of Node imports.
 */
import type { FigureSpec } from "./spec.ts";

/** Candidate repository-relative paths for a source id, in lookup order. */
export function sourceCandidates(id: string): string[] {
  if (/^RS-/.test(id)) return [`research/results/${id}.json`];
  if (/^EX-/.test(id)) return [`research/experiments/${id}.json`];
  if (/^TH-/.test(id)) return [`research/theories/${id}.json`];
  if (/^RUN-/.test(id)) return [`research/runs/${id}.json`];
  if (/^docs\/.+\.md$/.test(id) || /^web\/content\/log\/\d{4}-\d{2}-\d{2}\.mdx$/.test(id)) return [id];
  return [];
}

/** The repository path a source id resolves to, or null when no candidate exists. */
export function resolveSource(id: string, exists: (relativePath: string) => boolean): string | null {
  for (const candidate of sourceCandidates(id)) if (exists(candidate)) return candidate;
  return null;
}

/** Where a source record can be opened in the console, if anywhere (RS-/RUN- records have no route of their own). */
export function sourceHref(id: string): string | null {
  if (id.startsWith("EX-")) return `/experiments/${id}`;
  if (id.startsWith("TH-")) return `/theories/${id}`;
  if (/^docs\/.+\.md$/.test(id)) return `/${id.replace(/\.md$/, "")}`;
  const log = /^web\/content\/log\/(\d{4}-\d{2}-\d{2})\.mdx$/.exec(id);
  if (log) return `/log/${log[1]}`;
  return null;
}

export interface EmbedRef {
  kind: "figure" | "diagram";
  name: string;
  line: number;
  form: "fence" | "tag";
}

/** Mirror of parseEmbedFence in web/components/Markdown.tsx, on raw text. */
export function* fenceRefs(text: string): Generator<EmbedRef> {
  const re = /^(`{3,}|~{3,})[ \t]*(figure|diagram)\b[ \t]*([^\n]*)\n([\s\S]*?)^\1[ \t]*$/gm;
  for (const m of text.matchAll(re)) {
    const [, , kind, meta, body] = m;
    const line = text.slice(0, m.index).split("\n").length;
    const lines = body
      .split("\n")
      .map((l) => l.trim())
      .filter(Boolean);
    const name = meta.trim().split(/\s+/)[0] || lines[0] || "";
    yield { kind: kind as "figure" | "diagram", name, line, form: "fence" };
  }
}

/**
 * JSX embeds: <Figure name=…>, <Diagram name=…>, <FigureGrid names={[…]}>,
 * and the stat tile's `trend={{ figure: "…" }}` / `figure="…"` references.
 */
export function* tagRefs(text: string): Generator<EmbedRef> {
  const single = /<(Figure|Diagram|Sparkline|StatTile)\b[^>]*?\b(?:name|figure)=["']([^"']*)["']/g;
  for (const m of text.matchAll(single)) {
    const line = text.slice(0, m.index).split("\n").length;
    yield { kind: m[1] === "Diagram" ? "diagram" : "figure", name: m[2], line, form: "tag" };
  }
  const trend = /\btrend=\{\{[^}]*?\bfigure:\s*["']([^"']*)["']/g;
  for (const m of text.matchAll(trend)) {
    const line = text.slice(0, m.index).split("\n").length;
    yield { kind: "figure", name: m[1], line, form: "tag" };
  }
  const grid = /<FigureGrid\b[^>]*?\bnames=\{\[([^\]]*)\]\}/g;
  for (const m of text.matchAll(grid)) {
    const line = text.slice(0, m.index).split("\n").length;
    for (const n of m[1].matchAll(/["']([^"']+)["']/g)) yield { kind: "figure", name: n[1], line, form: "tag" };
  }
}

/** Every embed in a document; fences in Markdown and MDX, tags in MDX and TSX. */
export function embedRefs(text: string, extension: "md" | "mdx" | "tsx"): EmbedRef[] {
  const out: EmbedRef[] = [];
  if (extension !== "tsx") out.push(...fenceRefs(text));
  if (extension !== "md") out.push(...tagRefs(text));
  return out;
}

/** Spec names no scanned page embeds. */
export function findOrphans(specNames: readonly string[], embedded: ReadonlySet<string>): string[] {
  return specNames.filter((name) => !embedded.has(name)).sort();
}

/**
 * A `bar` spec that is really a paired-delta chart: every point carries a
 * bound and the values cross zero. The checker nudges these toward
 * `kind: "delta"` once that kind ships.
 */
export function looksLikeSignedDelta(spec: FigureSpec): boolean {
  if (spec.kind !== "bar") return false;
  const points = spec.series.flatMap((s) => s.points);
  if (points.length === 0) return false;
  const bounded = points.every((p) => p.lo !== undefined || p.hi !== undefined);
  const values = points.flatMap((p) => [p.y, ...(p.lo !== undefined ? [p.lo] : []), ...(p.hi !== undefined ? [p.hi] : [])]);
  const crossesZero = Math.min(...values) < 0 && Math.max(...values) > 0;
  return bounded && crossesZero;
}
