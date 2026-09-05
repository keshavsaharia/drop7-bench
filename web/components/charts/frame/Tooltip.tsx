"use client";
/**
 * The one tooltip for every chart. Rendered into document.body, positioned
 * in viewport coordinates and clamped inside the viewport. It is anchored to
 * the crosshair or the mark, never to the pointer: above-right of a mark,
 * top-right of a crosshair, flipping left or below near the viewport edges.
 *
 * Layout: an optional head line (the x value or category, secondary ink),
 * one row per series with the VALUE FIRST in the mono face and the series
 * name after it, then the detail lines (bounds, floor, n, W-T-L, note), then
 * the source line in mono, muted. Every string is inserted as a React text
 * node.
 */
import { useLayoutEffect, useRef, useState, type CSSProperties } from "react";
import { createPortal } from "react-dom";
import { Key, type KeyStyle } from "./Key";

export interface TooltipRow {
  value: string;
  label?: string;
  key?: KeyStyle;
  /** The hovered series' row, emphasised. */
  hot?: boolean;
  /** A secondary line under the row (a one-sided bound, a note). */
  note?: string;
}

export interface TooltipContent {
  head?: string;
  rows: TooltipRow[];
  details?: string[];
  source?: string;
}

export interface TooltipState {
  /** Viewport coordinates of the anchor. */
  x: number;
  y: number;
  anchor: "mark" | "crosshair";
  content: TooltipContent;
}

const OFFSET = 12;
const EDGE = 8;

export function ChartTooltip({ state }: { state: TooltipState | null }) {
  const ref = useRef<HTMLDivElement | null>(null);
  const [position, setPosition] = useState<{ left: number; top: number } | null>(null);

  useLayoutEffect(() => {
    if (!state || !ref.current) {
      setPosition(null);
      return;
    }
    const { width, height } = ref.current.getBoundingClientRect();
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    let left = state.x + OFFSET;
    if (left + width > vw - EDGE) left = state.x - width - OFFSET;
    if (left < EDGE) left = Math.max(EDGE, Math.min(vw - width - EDGE, state.x - width / 2));
    let top: number;
    if (state.anchor === "mark") {
      top = state.y - height - OFFSET;
      if (top < EDGE) top = state.y + OFFSET;
    } else {
      top = state.y;
    }
    if (top + height > vh - EDGE) top = Math.max(EDGE, vh - height - EDGE);
    setPosition({ left, top });
  }, [state]);

  if (!state || typeof document === "undefined") return null;

  const style: CSSProperties = {
    position: "fixed",
    left: position?.left ?? state.x + OFFSET,
    top: position?.top ?? state.y + OFFSET,
    visibility: position ? "visible" : "hidden",
    pointerEvents: "none",
    zIndex: 60,
  };
  const { content } = state;

  return createPortal(
    <div ref={ref} className="rchart-tooltip" role="tooltip" style={style}>
      {content.head && <p className="rchart-tooltip-head">{content.head}</p>}
      {content.rows.map((row, index) => (
        <div key={index} className={row.hot ? "rchart-tooltip-row is-hot" : "rchart-tooltip-row"}>
          {row.key && <Key style={row.key} />}
          <span className="rchart-tooltip-value">{row.value}</span>
          {row.label && <span className="rchart-tooltip-label">{row.label}</span>}
          {row.note && <span className="rchart-tooltip-note">{row.note}</span>}
        </div>
      ))}
      {content.details?.map((line, index) => (
        <p key={index} className="rchart-tooltip-meta">
          {line}
        </p>
      ))}
      {content.source && <p className="rchart-tooltip-source">{content.source}</p>}
    </div>,
    document.body,
  );
}
