"use client";
/**
 * Layout helpers for the chart kit: container measurement, text measurement
 * and word wrapping. Everything here is deterministic for a given width and
 * measurer, so a chart can be laid out on the server with an estimated text
 * width and re-laid out on the client with the real one.
 */
import { useCallback, useEffect, useLayoutEffect, useRef, useState, type RefObject } from "react";
import { FONT, MIN_WIDTH } from "./theme";

const useIsomorphicLayoutEffect = typeof window === "undefined" ? useEffect : useLayoutEffect;

/**
 * `mounted` flips to true in a layout effect after hydration. Until then the
 * chart must render exactly what the server rendered (fallback width,
 * estimated text widths) so hydration never sees a mismatch; the re-render
 * with measured values happens before the first paint.
 */
export function useMounted(): boolean {
  const [mounted, setMounted] = useState(false);
  useIsomorphicLayoutEffect(() => {
    setMounted(true);
  }, []);
  return mounted;
}

/** Observes the container's width; returns the fallback until measured. */
export function useContainerWidth<T extends HTMLElement>(
  fallback: number,
): [RefObject<T | null>, number] {
  const ref = useRef<T | null>(null);
  const [width, setWidth] = useState(fallback);
  useIsomorphicLayoutEffect(() => {
    const element = ref.current;
    if (!element) return;
    const apply = () => {
      const measured = Math.round(element.getBoundingClientRect().width);
      if (measured > 0) setWidth(Math.max(MIN_WIDTH, measured));
    };
    apply();
    if (typeof ResizeObserver === "undefined") return;
    const observer = new ResizeObserver(apply);
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  return [ref, width];
}

export type Measurer = (text: string, size: number, weight?: number) => number;

let canvas: CanvasRenderingContext2D | null | undefined;

function canvasContext(): CanvasRenderingContext2D | null {
  if (canvas !== undefined) return canvas;
  if (typeof document === "undefined") {
    canvas = null;
    return canvas;
  }
  canvas = document.createElement("canvas").getContext("2d");
  return canvas;
}

/** Estimate used on the server and before hydration: proportional sans-serif averages ~0.56 em. */
export function estimateText(text: string, size: number, weight = 400): number {
  return text.length * size * (weight >= 600 ? 0.6 : 0.56);
}

/** Real text width through a canvas once mounted; the estimate until then. */
export function useMeasurer(mounted: boolean): Measurer {
  return useCallback<Measurer>(
    (text, size, weight = 400) => {
      if (!mounted) return estimateText(text, size, weight);
      const context = canvasContext();
      if (!context) return estimateText(text, size, weight);
      context.font = `${weight} ${size}px ${FONT}`;
      return context.measureText(text).width;
    },
    [mounted],
  );
}

/**
 * Greedy word wrap into lines no wider than `maxWidth`. A single word wider
 * than the limit is kept on its own line; the caller decides whether that
 * layout is acceptable (`fits` is false in that case).
 */
export function wrapText(
  text: string,
  maxWidth: number,
  size: number,
  measure: Measurer,
  maxLines = Infinity,
): { lines: string[]; fits: boolean } {
  const words = text.split(/\s+/).filter(Boolean);
  const lines: string[] = [];
  let line = "";
  let fits = true;
  for (const word of words) {
    const candidate = line ? `${line} ${word}` : word;
    if (measure(candidate, size) <= maxWidth || !line) {
      line = candidate;
      if (!line.includes(" ") && measure(line, size) > maxWidth) fits = false;
    } else {
      lines.push(line);
      line = word;
      if (measure(word, size) > maxWidth) fits = false;
    }
  }
  if (line) lines.push(line);
  if (lines.length > maxLines) fits = false;
  return { lines, fits };
}
