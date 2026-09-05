/**
 * A bar: at most 24 px thick (the kind caps it), square at the baseline and
 * rounded 4 px at the data end, full opacity. The mount animation scales it
 * from the baseline, so the transform origin sits on the baseline edge.
 * Colour is a token reference passed in `color`.
 */
import type { CSSProperties } from "react";
import { barPath, type BarEnd } from "@/lib/charts/geometry";

const ORIGIN: Record<BarEnd, string> = { top: "50% 100%", bottom: "50% 0%", right: "0% 50%", left: "100% 50%" };

export function Bar({ x, y, w, h, end, color, hot = false, dim = false, radius = 4 }: { x: number; y: number; w: number; h: number; end: BarEnd; color: string; hot?: boolean; dim?: boolean; radius?: number }) {
  const d = barPath(x, y, w, h, radius, end);
  if (!d) return null;
  const horizontal = end === "left" || end === "right";
  const className = ["rchart-bar", horizontal ? "is-horizontal" : "", hot ? "is-hot" : "", dim ? "rchart-dim" : ""].filter(Boolean).join(" ");
  const style: CSSProperties = { transformOrigin: ORIGIN[end] };
  return <path className={className} d={d} fill={color} style={style} />;
}
