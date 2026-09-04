/**
 * `Drop7Board` renders one Drop7 position and nothing else: the 7×7 grid, an
 * optional incoming disc, and optional annotations a theory page needs to
 * explain a decision — highlighted cells, dimmed cells, a drop marker over a
 * column, and a per-column readout (labels, values, a relative bar).
 *
 * It is a server-safe component, so it works in MDX (`<Drop7Board cells="…" />`)
 * and inside client components alike. It never computes a game number: every
 * value it shows was passed in by the author or produced by the engine.
 *
 * Cell encoding: one digit per cell, row-major from the top — 0 empty, 1–7
 * numbered, 8 solid gray, 9 cracked gray (the engine's `serializeBoard`).
 * Colours are the tokens from globals.css through their Tailwind utilities.
 */
import type { CSSProperties, ReactNode } from "react";
import { DiscFace, cellLabel, parseBoard } from "./discs";
import styles from "./Drop7Board.module.css";

export interface ColumnNote {
  /** Short text over the value, e.g. "c3" or "best". */
  label?: string;
  /** Shown as given; numbers are formatted with thousands separators. */
  value?: number | string | null;
  /** Draws the note in the accent colour. */
  best?: boolean;
  /** Draws the note faded (an illegal or pruned column). */
  muted?: boolean;
}

export interface ExplosionPoint {
  /** Stable for the lifetime of this one floating label. */
  id: number | string;
  /** Cell where the disc exploded (0–48, row-major from the top). */
  index: number;
  /** Points awarded by this disc's chain wave. */
  points: number;
}

export interface Drop7BoardProps {
  cells: string | readonly number[];
  /** Incoming disc, shown above the board. `null` keeps the row reserved with no disc. */
  nextDisc?: number | null;
  /** Column (0–6) the incoming disc is headed for; also marks that column. `null` parks the disc with no column chosen. */
  dropColumn?: number | null;
  /** Cell indexes (0–48, row-major from the top) to ring. */
  highlight?: readonly number[];
  /** Cell indexes to fade. */
  dim?: readonly number[];
  /** Per-column readout under the board, index = column. */
  columns?: readonly (ColumnNote | null | undefined)[];
  /** CSS width; numbers are pixels. Defaults to the container width, capped. */
  size?: number | string;
  caption?: ReactNode;
  className?: string;
  /** Accessible description; defaults to an occupancy summary. */
  label?: string;
  /** Extra classes per cell — the game uses this for motion classes. */
  cellClassName?: (index: number, cell: number) => string | undefined;
  cellStyle?: (index: number, cell: number) => CSSProperties | undefined;
  /** Floating per-disc score labels supplied by an animated viewer. */
  explosionPoints?: readonly ExplosionPoint[];
  /** Show floating explosion scores. Defaults to true. */
  showExplosionPoints?: boolean;
  /** Rendered absolutely over the grid (column buttons, an end-of-game panel). */
  overlay?: ReactNode;
}

const LABEL = "font-mono text-label font-medium uppercase";

function formatValue(value: number | string) {
  return typeof value === "number" ? Math.round(value).toLocaleString("en-US") : value;
}

/** Bar widths from the numeric values present: 1 for the best, 0 for the worst. */
function barWidths(columns: readonly (ColumnNote | null | undefined)[]) {
  const numeric = columns
    .map((note, column) => [column, note?.value] as const)
    .filter((entry): entry is readonly [number, number] => typeof entry[1] === "number");
  if (numeric.length === 0) return new Map<number, number>();
  const values = numeric.map(([, value]) => value);
  const minimum = Math.min(...values);
  const maximum = Math.max(...values);
  const range = maximum - minimum;
  return new Map(numeric.map(([column, value]) => [column, range === 0 ? 1 : (value - minimum) / range]));
}

export function Drop7Board({
  cells,
  nextDisc,
  dropColumn,
  highlight = [],
  dim = [],
  columns,
  size,
  caption,
  className = "",
  label,
  cellClassName,
  cellStyle,
  explosionPoints = [],
  showExplosionPoints = true,
  overlay,
}: Drop7BoardProps) {
  const board = parseBoard(cells);
  const occupied = board.filter((cell) => cell !== 0).length;
  const covered = board.filter((cell) => cell === 8 || cell === 9).length;
  const widths = columns ? barWidths(columns) : null;
  const width = typeof size === "number" ? `${size}px` : (size ?? "min(100%, 22rem)");

  return (
    <figure className={`inline-flex max-w-full flex-col gap-1.5 align-top ${className}`} style={{ width }}>
      {nextDisc !== undefined && (
        <div className="grid grid-cols-7 px-1.5" aria-label={nextDisc === null ? "next disc" : `next disc ${nextDisc}`}>
          {Array.from({ length: 7 }, (_, column) => (
            <div key={column} className="flex aspect-square items-center justify-center p-[12%]">
              {nextDisc !== null && column === (typeof dropColumn === "number" ? dropColumn : 3) ? (
                <DiscFace cell={nextDisc} className="size-full text-[0.9em]" />
              ) : null}
            </div>
          ))}
        </div>
      )}

      <div className="relative rounded-lg border border-rule bg-surface p-1.5 [container-type:inline-size]">
        <div
          className="grid aspect-square grid-cols-7 grid-rows-7 gap-px overflow-hidden rounded-sm bg-rule"
          role="img"
          aria-label={label ?? `${occupied} occupied cells, ${covered} of them gray discs`}
        >
          {board.map((cell, index) => {
            const column = index % 7;
            const ringed = highlight.includes(index);
            const faded = dim.includes(index);
            const inDrop = typeof dropColumn === "number" && column === dropColumn;
            return (
              <div
                key={index}
                title={cellLabel(cell)}
                className={`relative flex items-center justify-center p-[9%] text-[6.4cqw] ${
                  inDrop ? "bg-raised" : "bg-cell"
                } ${faded ? "opacity-30" : ""}`}
              >
                <DiscFace cell={cell} className={`size-full ${cellClassName?.(index, cell) ?? ""}`} style={cellStyle?.(index, cell)} />
                {showExplosionPoints &&
                  explosionPoints
                    .filter((point) => point.index === index)
                    .map((point) => (
                      <span key={point.id} aria-hidden="true" className={styles.explosionPoint}>
                        +{formatValue(point.points)}
                      </span>
                    ))}
                {ringed && (
                  <span
                    aria-hidden="true"
                    className="pointer-events-none absolute inset-[3%] rounded-sm border-2 border-highlight"
                  />
                )}
              </div>
            );
          })}
        </div>
        {overlay}
      </div>

      {columns && widths && (
        <div className="grid grid-cols-7 gap-1 px-1.5" aria-label="column readout">
          {Array.from({ length: 7 }, (_, column) => {
            const note = columns[column];
            const tone = note?.best ? "text-accent" : note?.muted ? "text-ink-4" : "text-ink-3";
            const bar = widths.get(column);
            return (
              <div
                key={column}
                className={`min-w-0 border-t-2 pt-1 text-center ${note?.best ? "border-accent" : "border-rule"}`}
              >
                <span className={`${LABEL} block truncate ${tone}`}>{note?.label ?? `c${column + 1}`}</span>
                {note?.value !== undefined && note?.value !== null && (
                  <span className={`block truncate font-mono text-[0.65rem] tabular-nums ${note.best ? "text-accent" : "text-ink-2"}`}>
                    {formatValue(note.value)}
                  </span>
                )}
                {bar !== undefined && (
                  <span className="mt-1 block h-1 overflow-hidden rounded-full bg-rule" role="presentation">
                    <span
                      className={`block h-full transition-[width] duration-300 motion-reduce:transition-none ${note?.best ? "bg-accent" : "bg-ink-3"}`}
                      style={{ width: `${bar * 100}%` }}
                    />
                  </span>
                )}
              </div>
            );
          })}
        </div>
      )}

      {caption && <figcaption className="text-caption text-ink-2">{caption}</figcaption>}
    </figure>
  );
}
