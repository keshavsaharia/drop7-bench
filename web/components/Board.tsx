/**
 * Visual Drop7 components used by both the app pages and MDX documentation.
 * The position renderer is `Drop7Board`; the components here are the
 * long-standing MDX shorthands built on it, plus small layout helpers.
 * Board encoding follows the engine's serializeBoard format: one character per
 * cell, row-major from the top: "0" empty, "1"-"7" numbered, "8" solid gray,
 * "9" cracked gray.
 *
 * Styled by the `.fig*`, `.callout*`, `.stat*` and `.board-compare*` blocks in
 * globals.css, which win inside .prose-drop7.
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
    <figure className="fig fig-frame board-compare">
      <div className="board-compare-row">
        <div className="board-compare-item">
          <Drop7Board cells={before} highlight={highlightBefore} size={size} />
          <span className="label">{beforeLabel}</span>
        </div>
        <svg width="34" height="24" viewBox="0 0 34 24" className="board-compare-arrow" aria-hidden="true">
          <path d="M2 12h26m0 0-8-8m8 8-8 8" stroke="currentColor" strokeWidth="2.5" fill="none" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
        <div className="board-compare-item">
          <Drop7Board cells={after} highlight={highlightAfter} size={size} />
          <span className="label">{afterLabel}</span>
        </div>
      </div>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

/** A number with its label and its provenance, for dashboards and MDX. */
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
    <div className="stat">
      <span className="label">{label}</span>
      <span className="stat-value">{value}</span>
      {hint && <span className="stat-hint">{hint}</span>}
    </div>
  );
}

export type CalloutTone = "info" | "key" | "warn" | "success" | "oracle" | "note";

const CALLOUT_TONE_CLASS: Record<CalloutTone, string> = {
  info: "callout--key",
  key: "callout--key",
  warn: "callout--warn",
  success: "callout--success",
  oracle: "callout--oracle",
  note: "",
};

export function Callout({
  title,
  children,
  tone = "info",
}: {
  title?: string;
  children: React.ReactNode;
  tone?: CalloutTone;
}) {
  const toneClass = CALLOUT_TONE_CLASS[tone] ?? CALLOUT_TONE_CLASS.info;
  return (
    <aside className={toneClass ? `callout ${toneClass}` : "callout"}>
      {title && <span className="label">{title}</span>}
      <div className="callout-body">{children}</div>
    </aside>
  );
}
