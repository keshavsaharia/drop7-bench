/**
 * Visual Drop7 components used by both the app pages and MDX documentation.
 * Board encoding follows the engine's serializeBoard format: one character per
 * cell, row-major from the top: "0" empty, "1"-"7" numbered, "8" solid gray,
 * "9" cracked gray.
 */

export const DISC_COLORS: Record<number, string> = {
  1: "#f97316",
  2: "#eab308",
  3: "#22c55e",
  4: "#06b6d4",
  5: "#3b82f6",
  6: "#a855f7",
  7: "#ec4899",
};

export function parseBoard(cells: string | readonly number[]): number[] {
  if (Array.isArray(cells)) return [...cells] as number[];
  return [...cells].map((char) => Number(char));
}

export function CellGlyph({ cell, x, y, s }: { cell: number; x: number; y: number; s: number }) {
  const pad = s * 0.08;
  if (cell === 0) {
    return (
      <rect
        x={x + pad}
        y={y + pad}
        width={s - pad * 2}
        height={s - pad * 2}
        rx={s * 0.12}
        fill="#111827"
      />
    );
  }
  if (cell === 8 || cell === 9) {
    const cracked = cell === 9;
    return (
      <g>
        <rect
          x={x + pad}
          y={y + pad}
          width={s - pad * 2}
          height={s - pad * 2}
          rx={s * 0.12}
          fill={cracked ? "#6b7280" : "#4b5563"}
          stroke="#9ca3af"
          strokeWidth={cracked ? 2 : 1}
        />
        {cracked && (
          <path
            d={`M ${x + s * 0.25} ${y + s * 0.2} L ${x + s * 0.5} ${y + s * 0.45} L ${x + s * 0.35} ${y + s * 0.6} L ${x + s * 0.6} ${y + s * 0.85}`}
            stroke="#d1d5db"
            strokeWidth={s * 0.04}
            fill="none"
            strokeLinecap="round"
          />
        )}
        {!cracked && (
          <text
            x={x + s / 2}
            y={y + s / 2}
            textAnchor="middle"
            dominantBaseline="central"
            fill="#9ca3af"
            fontSize={s * 0.4}
            fontWeight={700}
          >
            ?
          </text>
        )}
      </g>
    );
  }
  const color = DISC_COLORS[cell] ?? "#e5e7eb";
  return (
    <g>
      <circle cx={x + s / 2} cy={y + s / 2} r={s * 0.42} fill={color} />
      <circle
        cx={x + s / 2}
        cy={y + s / 2 - s * 0.06}
        r={s * 0.3}
        fill="#ffffff"
        opacity={0.18}
      />
      <text
        x={x + s / 2}
        y={y + s / 2}
        textAnchor="middle"
        dominantBaseline="central"
        fill="#ffffff"
        fontSize={s * 0.44}
        fontWeight={800}
      >
        {cell}
      </text>
    </g>
  );
}

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
  const board = parseBoard(cells);
  const s = 100;
  return (
    <figure className="inline-flex flex-col items-center gap-1">
      <svg
        width={size}
        height={size}
        viewBox={`0 0 ${7 * s} ${7 * s}`}
        role="img"
        aria-label={caption ?? "Drop7 board"}
        className="rounded-lg"
      >
        <rect x={0} y={0} width={7 * s} height={7 * s} fill="#030712" rx={12} />
        {board.map((cell, index) => {
          const x = (index % 7) * s;
          const y = Math.floor(index / 7) * s;
          return (
            <g key={index}>
              <CellGlyph cell={cell} x={x} y={y} s={s} />
              {highlight.includes(index) && (
                <rect
                  x={x + 4}
                  y={y + 4}
                  width={s - 8}
                  height={s - 8}
                  rx={10}
                  fill="none"
                  stroke="#facc15"
                  strokeWidth={5}
                />
              )}
            </g>
          );
        })}
      </svg>
      {caption && (
        <figcaption className="max-w-52 text-center text-xs text-zinc-400">
          {caption}
        </figcaption>
      )}
    </figure>
  );
}

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
    <svg
      width={size}
      height={size}
      viewBox="0 0 100 100"
      className="inline-block align-[-0.35em]"
      role="img"
      aria-label={solid ? "solid gray disc" : cracked ? "cracked gray disc" : `disc ${n}`}
    >
      <CellGlyph cell={cell} x={0} y={0} s={100} />
    </svg>
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
        <Board cells={before} highlight={highlightBefore} size={size} />
        <span className="text-xs font-medium uppercase tracking-wide text-zinc-500">
          {beforeLabel}
        </span>
      </div>
      <svg width="34" height="24" viewBox="0 0 34 24" className="shrink-0 text-zinc-500">
        <path d="M2 12h26m0 0-8-8m8 8-8 8" stroke="currentColor" strokeWidth="2.5" fill="none" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
      <div className="flex flex-col items-center gap-1">
        <Board cells={after} highlight={highlightAfter} size={size} />
        <span className="text-xs font-medium uppercase tracking-wide text-zinc-500">
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
