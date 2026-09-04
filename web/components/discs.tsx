/**
 * Disc and cell primitives shared by every Drop7 visual in the console.
 *
 * Cell encoding follows the engine's `serializeBoard`: one value per cell,
 * row-major from the top — 0 empty, 1–7 a numbered disc, 8 a solid gray disc,
 * 9 a cracked gray disc. Two renderers draw the same disc: `DiscFace` for DOM
 * layouts (the board component, the game) and `CellGlyph` for the SVG figure
 * kits in Rules.tsx, Engine.tsx and Concepts*.tsx.
 *
 * Colours are the `--color-disc-*` tokens from globals.css. `DISC_STYLES`
 * keeps the same values as literal hex for the one renderer that cannot
 * resolve a CSS variable: the Satori social-image renderer in
 * lib/social-image.tsx. Everything drawn in a browser uses `DISC_VARS`.
 */
import type { CSSProperties } from "react";

export const EMPTY_CELL = 0;
export const SOLID_CELL = 8;
export const CRACKED_CELL = 9;

/** Literal palette for Satori (next/og), which cannot read CSS variables. Mirrors --color-disc-* verbatim. */
export const DISC_STYLES: Record<number, { bg: string; fg: string }> = {
  1: { bg: "#218a57", fg: "#ffffff" },
  2: { bg: "#d7b33f", fg: "#17130a" },
  3: { bg: "#d7742e", fg: "#ffffff" },
  4: { bg: "#c4443e", fg: "#ffffff" },
  5: { bg: "#9e4c8b", fg: "#ffffff" },
  6: { bg: "#238391", fg: "#ffffff" },
  7: { bg: "#405db0", fg: "#ffffff" },
};

/** Token references for browser rendering (DOM styles and SVG attributes). */
export const DISC_VARS: Record<number, { bg: string; fg: string }> = Object.fromEntries(
  [1, 2, 3, 4, 5, 6, 7].map((value) => [
    value,
    { bg: `var(--color-disc-${value})`, fg: `var(--color-disc-${value}-fg)` },
  ]),
);

/** Background colour per numbered disc, for charts and legends (token references). */
export const DISC_COLORS: Record<number, string> = Object.fromEntries(
  Object.entries(DISC_VARS).map(([value, style]) => [value, style.bg]),
);

const FALLBACK_DISC = { bg: "var(--color-ink-4)", fg: "var(--color-accent-fg)" };
const GRAY_RING = "var(--color-disc-gray)";
const GRAY_CORE = "var(--color-disc-gray-core)";
const EMPTY_FILL = "var(--color-cell)";

/** Ten ring segments, 36° apart, starting at 18° so a gap sits at the top. */
const CRACKED_SEGMENT_ROTATIONS = Array.from({ length: 10 }, (_, index) => 18 + index * 36);

export function parseBoard(cells: string | readonly number[]): number[] {
  if (Array.isArray(cells)) return [...cells] as number[];
  return [...(cells as string)].map((char) => Number(char));
}

export function cellLabel(cell: number): string {
  if (cell === EMPTY_CELL) return "empty";
  if (cell === SOLID_CELL) return "solid gray disc";
  if (cell === CRACKED_CELL) return "cracked gray disc";
  return `disc ${cell}`;
}

function SolidDiscArt() {
  return (
    <svg aria-hidden="true" className="absolute inset-0 size-full" viewBox="0 0 100 100">
      <circle cx="50" cy="50" fill={GRAY_RING} r="48" />
      <circle cx="50" cy="50" fill={GRAY_CORE} r="35" />
      <circle cx="50" cy="50" fill={GRAY_RING} r="29.5" />
    </svg>
  );
}

function CrackedDiscArt() {
  return (
    <svg aria-hidden="true" className="absolute inset-0 size-full" viewBox="0 0 100 100">
      <g fill={GRAY_RING}>
        <circle cx="50" cy="50" r="29.5" />
        {CRACKED_SEGMENT_ROTATIONS.map((rotation) => (
          <path
            key={rotation}
            d="M39.9 3.1A48 48 0 0 1 60.1 3.1l-2.8 12.8a35 35 0 0 0-14.6 0Z"
            transform={`rotate(${rotation} 50 50)`}
          />
        ))}
      </g>
    </svg>
  );
}

/**
 * One disc as a DOM element that fills its container (`size-full` unless a
 * className overrides it). Renders nothing for an empty cell. The number's
 * font size is inherited, so the container decides it.
 */
export function DiscFace({
  cell,
  className = "size-full",
  style,
}: {
  cell: number;
  className?: string;
  style?: CSSProperties;
}) {
  if (cell === EMPTY_CELL) return null;
  if (cell === SOLID_CELL || cell === CRACKED_CELL) {
    return (
      <span
        className={`relative block shrink-0 overflow-hidden rounded-full ${className}`}
        style={{ backgroundColor: GRAY_CORE, ...style }}
        aria-hidden="true"
      >
        {cell === CRACKED_CELL ? <CrackedDiscArt /> : <SolidDiscArt />}
      </span>
    );
  }
  const palette = DISC_VARS[cell] ?? FALLBACK_DISC;
  return (
    <span
      className={`flex shrink-0 items-center justify-center rounded-full border border-black/15 font-mono font-bold leading-none shadow-[inset_0_-0.14em_0_rgba(0,0,0,0.22)] ${className}`}
      style={{ backgroundColor: palette.bg, color: palette.fg, ...style }}
      aria-hidden="true"
    >
      {cell}
    </span>
  );
}

/** The same disc inside an SVG, drawn in a cell of side `s` whose corner is (x, y). */
export function CellGlyph({ cell, x, y, s }: { cell: number; x: number; y: number; s: number }) {
  const pad = s * 0.08;
  if (cell === EMPTY_CELL) {
    return (
      <rect x={x + pad} y={y + pad} width={s - pad * 2} height={s - pad * 2} rx={s * 0.12} fill={EMPTY_FILL} />
    );
  }
  const scale = (s * 0.84) / 100;
  const origin = `translate(${x + s * 0.08} ${y + s * 0.08}) scale(${scale})`;
  if (cell === SOLID_CELL) {
    return (
      <g transform={origin}>
        <circle cx="50" cy="50" r="48" fill={GRAY_RING} />
        <circle cx="50" cy="50" r="35" fill={GRAY_CORE} />
        <circle cx="50" cy="50" r="29.5" fill={GRAY_RING} />
      </g>
    );
  }
  if (cell === CRACKED_CELL) {
    return (
      <g transform={origin}>
        <circle cx="50" cy="50" r="48" fill={GRAY_CORE} />
        <g fill={GRAY_RING}>
          <circle cx="50" cy="50" r="29.5" />
          {CRACKED_SEGMENT_ROTATIONS.map((rotation) => (
            <path
              key={rotation}
              d="M39.9 3.1A48 48 0 0 1 60.1 3.1l-2.8 12.8a35 35 0 0 0-14.6 0Z"
              transform={`rotate(${rotation} 50 50)`}
            />
          ))}
        </g>
      </g>
    );
  }
  const palette = DISC_VARS[cell] ?? FALLBACK_DISC;
  return (
    <g transform={origin}>
      <circle cx="50" cy="50" r="48" fill={palette.bg} stroke="rgba(0,0,0,0.18)" strokeWidth="2" />
      <path d="M14 62a36 36 0 0 0 72 0a48 48 0 0 1-72 0Z" fill="rgba(0,0,0,0.2)" />
      <text
        x="50"
        y="52"
        textAnchor="middle"
        dominantBaseline="central"
        fill={palette.fg}
        fontSize="52"
        fontWeight={700}
        fontFamily="var(--font-mono)"
      >
        {cell}
      </text>
    </g>
  );
}
