"use client";
/**
 * A chart tooltip rendered into document.body, positioned in viewport
 * coordinates and clamped inside the viewport, so it can never be clipped by
 * the figure box or overlap the chart title the way the old in-SVG popovers
 * did. Anchors are viewport (clientX/clientY) coordinates.
 */
import { useLayoutEffect, useRef, useState, type CSSProperties } from "react";
import { createPortal } from "react-dom";

export interface TooltipLine {
  text: string;
  strong?: boolean;
  muted?: boolean;
  /** A colour swatch drawn before the text (series colour). */
  swatch?: string;
}

export interface TooltipState {
  /** Viewport x/y of the pointer or the focused marker. */
  x: number;
  y: number;
  lines: TooltipLine[];
}

const OFFSET = 14;
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
    let left = state.x + OFFSET;
    let top = state.y + OFFSET;
    if (left + width > window.innerWidth - EDGE) left = state.x - width - OFFSET;
    if (left < EDGE) left = EDGE;
    if (top + height > window.innerHeight - EDGE) top = state.y - height - OFFSET;
    if (top < EDGE) top = EDGE;
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

  return createPortal(
    <div ref={ref} className="rchart-tooltip" role="tooltip" style={style}>
      {state.lines.map((line, index) => (
        <div
          key={index}
          className={
            "rchart-tooltip-line" +
            (line.strong ? " is-strong" : "") +
            (line.muted ? " is-muted" : "")
          }
        >
          {line.swatch && <span className="rchart-swatch" style={{ background: line.swatch }} />}
          {line.text}
        </div>
      ))}
    </div>,
    document.body,
  );
}
