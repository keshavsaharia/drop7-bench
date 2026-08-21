/**
 * Visual Drop7 components used by both the app pages and MDX documentation.
 * The position renderer is `Drop7Board`; the components here are the
 * long-standing MDX shorthands built on it, plus small layout helpers.
 * Board encoding follows the engine's serializeBoard format: one character per
 * cell, row-major from the top: "0" empty, "1"-"7" numbered, "8" solid gray,
 * "9" cracked gray.
 */
import { Drop7Board } from "./Drop7Board";
import { CellGlyph, DISC_COLORS, DiscFace, cellLabel, parseBoard } from "./discs";

export { CellGlyph, DISC_COLORS, parseBoard };

export function Board({
  cells,
  highlight = [],
  size = 260,
  caption,
}: {
  cells: string | readonly number[];
  /** Cell indexes (0-48, row-major from the top) to outline. */
  highlight?: readonly number[];
  size?: number;
  caption?: string;
}) {
  return <Drop7Board cells={cells} highlight={highlight} size={size} caption={caption} className="items-center text-center" />;
}

/** An inline disc for prose: `<Disc n={3} />`, `<Disc solid />`, `<Disc cracked />`. */
export function Disc({
  n,
  solid,
  cracked,
  size = 28,
}: {
  n?: number;
  solid?: boolean;
  cracked?: boolean;
  size?: number;
}) {
  const cell = solid ? 8 : cracked ? 9 : (n ?? 1);
  return (
    <span
      role="img"
      aria-label={cellLabel(cell)}
      className="inline-flex shrink-0 align-[-0.35em]"
      style={{ width: size, height: size, fontSize: size * 0.55 }}
    >
      <DiscFace cell={cell} />
    </span>
  );
}

export function BoardCompare({
  before,
  after,
  beforeLabel = "Before",
  afterLabel = "After",
  highlightBefore = [],
  highlightAfter = [],
  size = 200,
  caption,
}: {
  before: string | readonly number[];
  after: string | readonly number[];
  beforeLabel?: string;
  afterLabel?: string;
  highlightBefore?: readonly number[];
  highlightAfter?: readonly number[];
  size?: number;
  caption?: string;
}) {
  return (
    <div className="my-4 flex flex-wrap items-center justify-center gap-3 rounded-xl border border-zinc-800 bg-zinc-900/40 p-4">
      <div className="flex flex-col items-center gap-1">
        <Drop7Board cells={before} highlight={highlightBefore} size={size} />
        <span className="max-w-52 text-center text-xs font-medium uppercase tracking-wide text-zinc-500">
          {beforeLabel}
        </span>
      </div>
      <svg width="34" height="24" viewBox="0 0 34 24" className="shrink-0 text-zinc-500" aria-hidden="true">
        <path d="M2 12h26m0 0-8-8m8 8-8 8" stroke="currentColor" strokeWidth="2.5" fill="none" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
      <div className="flex flex-col items-center gap-1">
        <Drop7Board cells={after} highlight={highlightAfter} size={size} />
        <span className="max-w-52 text-center text-xs font-medium uppercase tracking-wide text-zinc-500">
          {afterLabel}
        </span>
      </div>
      {caption && (
        <p className="w-full text-center text-sm text-zinc-400">{caption}</p>
      )}
    </div>
  );
}

/** A numbered stat card used on dashboards and in MDX callouts. */
export function Stat({
  label,
  value,
  hint,
}: {
  label: string;
  value: string;
  hint?: string;
}) {
  return (
    <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-4 py-3">
      <div className="text-xs uppercase tracking-wide text-zinc-500">{label}</div>
      <div className="text-xl font-bold text-zinc-100">{value}</div>
      {hint && <div className="text-xs text-zinc-500">{hint}</div>}
    </div>
  );
}

export function Callout({
  title,
  children,
  tone = "info",
}: {
  title: string;
  children: React.ReactNode;
  tone?: "info" | "warn" | "success";
}) {
  const colors = {
    info: "border-sky-800 bg-sky-950/40 text-sky-100",
    warn: "border-amber-800 bg-amber-950/40 text-amber-100",
    success: "border-emerald-800 bg-emerald-950/40 text-emerald-100",
  }[tone];
  return (
    <aside className={`my-4 rounded-xl border px-4 py-3 ${colors}`}>
      <div className="mb-1 text-sm font-semibold">{title}</div>
      <div className="text-sm leading-relaxed [&_p]:my-1">{children}</div>
    </aside>
  );
}
