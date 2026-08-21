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
 */
import type { CSSProperties, ReactNode } from "react";
import { DiscFace, cellLabel, parseBoard } from "./discs";

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

export interface Drop7BoardProps {
  cells: string | readonly number[];
  /** Incoming disc, shown above the board. */
  nextDisc?: number;
  /** Column (0–6) the incoming disc is headed for; also marks that column. */
  dropColumn?: number;
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
  /** Rendered absolutely over the grid (column buttons, an end-of-game panel). */
  overlay?: ReactNode;
}

const LABEL = "text-[0.625rem] font-semibold uppercase tracking-[0.12em]";

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
        <div className="grid grid-cols-7 px-1.5" aria-label={`next disc ${nextDisc}`}>
          {Array.from({ length: 7 }, (_, column) => (
            <div key={column} className="flex aspect-square items-center justify-center p-[12%]">
              {column === (dropColumn ?? 3) ? (
                <DiscFace cell={nextDisc} className="size-full text-[0.9em]" />
              ) : null}
            </div>
          ))}
        </div>
      )}

      <div className="relative rounded-lg border border-zinc-800 bg-zinc-900 p-1.5 [container-type:inline-size]">
        <div
          className="grid aspect-square grid-cols-7 grid-rows-7 gap-px overflow-hidden rounded-sm bg-zinc-800"
          role="img"
          aria-label={label ?? `${occupied} occupied cells, ${covered} of them gray discs`}
        >
          {board.map((cell, index) => {
            const column = index % 7;
            const ringed = highlight.includes(index);
            const faded = dim.includes(index);
            const inDrop = dropColumn !== undefined && column === dropColumn;
            return (
              <div
                key={index}
                title={cellLabel(cell)}
                className={`relative flex items-center justify-center p-[9%] text-[6.4cqw] ${
                  inDrop ? "bg-zinc-900" : "bg-zinc-950"
                } ${faded ? "opacity-30" : ""}`}
              >
                <DiscFace cell={cell} className={`size-full ${cellClassName?.(index, cell) ?? ""}`} style={cellStyle?.(index, cell)} />
                {ringed && (
                  <span
                    aria-hidden="true"
                    className="pointer-events-none absolute inset-[3%] rounded-sm border-2 border-[#facc15]"
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
            const tone = note?.best ? "text-sky-400" : note?.muted ? "text-zinc-700" : "text-zinc-500";
            const bar = widths.get(column);
            return (
              <div
                key={column}
                className={`min-w-0 border-t-2 pt-1 text-center ${note?.best ? "border-sky-400" : "border-zinc-800"}`}
              >
                <span className={`${LABEL} block truncate ${tone}`}>{note?.label ?? `c${column + 1}`}</span>
                {note?.value !== undefined && note?.value !== null && (
                  <span className={`block truncate font-mono text-[0.65rem] tabular-nums ${note.best ? "text-sky-300" : "text-zinc-400"}`}>
                    {formatValue(note.value)}
                  </span>
                )}
                {bar !== undefined && (
                  <span className="mt-1 block h-1 overflow-hidden rounded-full bg-zinc-800" role="presentation">
                    <span
                      className={`block h-full transition-[width] duration-300 ${note?.best ? "bg-sky-400" : "bg-zinc-500"}`}
                      style={{ width: `${bar * 100}%` }}
                    />
                  </span>
                )}
              </div>
            );
          })}
        </div>
      )}

      {caption && <figcaption className="text-xs leading-relaxed text-zinc-400">{caption}</figcaption>}
    </figure>
  );
}
