"use client";
/**
 * The shell around every chart: the title as HTML (it wraps), a Chart/Table
 * toggle when the caller supplies the table view, the svg the kind draws,
 * a legend for two or more series (a single series needs no legend box: the
 * title names it), the evidence strip, the portal tooltip and a visually
 * hidden live region that repeats the tooltip for screen readers.
 *
 * The frame's root is also the element the kind measures for width, so the
 * kind passes its container ref in. Once, when the frame enters the viewport
 * (or immediately when it is already in view), it gains `is-entering`, which
 * charts.css uses for the mount animation; server HTML is the final state.
 */
import "../charts.css";
import { useEffect, useLayoutEffect, useState, type ReactNode, type RefObject } from "react";
import type { FigureEvidence } from "@/lib/charts/spec";
import { EvidenceStrip } from "@/components/EvidenceStrip";
import { Legend, type LegendItem } from "./Legend";
import { ChartTooltip, type TooltipState } from "./Tooltip";

const useIsomorphicLayoutEffect = typeof window === "undefined" ? useEffect : useLayoutEffect;

export interface ChartFrameProps {
  id?: string;
  title?: string;
  /** The element the kind measures; the frame renders it as its root. */
  frameRef: RefObject<HTMLDivElement | null>;
  legend?: LegendItem[];
  legendActive?: number | null;
  onLegendHover?: (index: number | null) => void;
  evidence?: FigureEvidence;
  tooltip: TooltipState | null;
  live: string;
  /** The source-data table; when present the header gains a Chart/Table toggle. */
  table?: ReactNode;
  compact?: boolean;
  className?: string;
  children: ReactNode;
}

function useEntering(ref: RefObject<HTMLElement | null>): boolean {
  const [entering, setEntering] = useState(false);
  useIsomorphicLayoutEffect(() => {
    const element = ref.current;
    if (!element || entering) return;
    const box = element.getBoundingClientRect();
    const viewportHeight = typeof window !== "undefined" ? window.innerHeight : 0;
    if (viewportHeight > 0 && box.bottom > 0 && box.top < viewportHeight) {
      setEntering(true);
      return;
    }
    if (typeof IntersectionObserver === "undefined") {
      setEntering(true);
      return;
    }
    const observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((entry) => entry.isIntersecting)) {
          setEntering(true);
          observer.disconnect();
        }
      },
      { threshold: 0.25 },
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, [ref, entering]);
  return entering;
}

export function ChartFrame({ id, title, frameRef, legend, legendActive = null, onLegendHover, evidence, tooltip, live, table, compact, className, children }: ChartFrameProps) {
  const [view, setView] = useState<"chart" | "table">("chart");
  const entering = useEntering(frameRef);
  const titleId = id ? `${id}-title` : undefined;
  const classes = ["rchart", compact ? "is-compact" : "", entering ? "is-entering" : "", className ?? ""].filter(Boolean).join(" ");

  return (
    <div className={classes} ref={frameRef} id={id}>
      {(title || table) && (
        <div className="rchart-head">
          {title && (
            <h4 className="rchart-title" id={titleId}>
              {title}
            </h4>
          )}
          {table && (
            <div className="rchart-toggle" role="group" aria-label="View as">
              <button type="button" className={view === "chart" ? "is-on" : undefined} aria-pressed={view === "chart"} onClick={() => setView("chart")}>
                Chart
              </button>
              <button type="button" className={view === "table" ? "is-on" : undefined} aria-pressed={view === "table"} onClick={() => setView("table")}>
                Table
              </button>
            </div>
          )}
        </div>
      )}
      {view === "table" && table ? (
        <div className="rchart-table-inline">{table}</div>
      ) : (
        <>
          {children}
          {legend && legend.length >= 2 && <Legend items={legend} active={legendActive} onHover={onLegendHover ?? (() => undefined)} />}
        </>
      )}
      {evidence && <EvidenceStrip {...evidence} />}
      <div className="rchart-live" aria-live="polite" aria-atomic="true">
        {live}
      </div>
      <ChartTooltip state={view === "chart" ? tooltip : null} />
    </div>
  );
}
