"use client";
/**
 * Layout hooks for the chart kit: container measurement, hydration-safe
 * mounting, and canvas text measurement. The pure helpers (text estimate,
 * word wrap, tick selection, the `rounded` scale wrapper) live in
 * web/lib/charts/layout.ts so they can be unit-tested without React; this
 * file re-exports the ones charts use so a kind imports one module.
 *
 * A chart is laid out on the server with the fallback width and estimated
 * text widths, then re-laid out in a layout effect after hydration with the
 * real container width and canvas-measured text, before the first paint.
 */
import { useCallback, useEffect, useLayoutEffect, useRef, useState, type RefObject } from "react";
import { estimateText } from "@/lib/charts/layout";
import { DEFAULT_WIDTH, MIN_WIDTH, measureFont, type FontKind } from "./tokens";

export { estimateText, rounded, tickValues, valueExtent, wrapText } from "@/lib/charts/layout";

const useIsomorphicLayoutEffect = typeof window === "undefined" ? useEffect : useLayoutEffect;

/** Measures text: (text, size, weight?, font?) -> width in px. */
export type Measurer = (text: string, size: number, weight?: number, font?: FontKind) => number;

/**
 * `mounted` flips to true in a layout effect after hydration. Until then the
 * chart must render exactly what the server rendered so hydration never sees
 * a mismatch; the re-render with measured values happens before first paint.
 */
export function useMounted(): boolean {
  const [mounted, setMounted] = useState(false);
  useIsomorphicLayoutEffect(() => {
    setMounted(true);
  }, []);
  return mounted;
}

/** Observes the container's width; returns the fallback until measured. */
export function useContainerWidth<T extends HTMLElement>(fallback = DEFAULT_WIDTH): [RefObject<T | null>, number] {
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

/** Mono glyphs are wider than a proportional sans on average. */
function estimate(text: string, size: number, weight: number, font: FontKind): number {
  return font === "mono" ? text.length * size * 0.62 : estimateText(text, size, weight);
}

/** Real text width through a canvas once mounted; the estimate until then. */
export function useMeasurer(mounted: boolean): Measurer {
  return useCallback<Measurer>(
    (text, size, weight = 400, font = "sans") => {
      if (!mounted) return estimate(text, size, weight, font);
      const context = canvasContext();
      if (!context) return estimate(text, size, weight, font);
      context.font = `${weight} ${size}px ${measureFont(font)}`;
      return context.measureText(text).width;
    },
    [mounted],
  );
}

/** The three hooks every chart kind starts with. */
export function useChartLayout(fallback = DEFAULT_WIDTH): { ref: RefObject<HTMLDivElement | null>; width: number; measure: Measurer; mounted: boolean } {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(fallback);
  return { ref, width, measure, mounted };
}
