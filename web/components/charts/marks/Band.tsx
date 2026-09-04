/**
 * An area wash between an upper and a lower line: the series hue at 10 %
 * opacity (charts.css .rchart-band), no stroke.
 */
import type { Pt } from "./Line";

export function Band({ upper, lower, color, dim = false }: { upper: readonly Pt[]; lower: readonly Pt[]; color: string; dim?: boolean }) {
  if (upper.length < 2 || lower.length < 2) return null;
  const d = `${upper.map(([x, y], i) => `${i === 0 ? "M" : "L"}${x},${y}`).join(" ")} ${[...lower]
    .reverse()
    .map(([x, y]) => `L${x},${y}`)
    .join(" ")} Z`;
  return <path className={dim ? "rchart-band rchart-dim" : "rchart-band"} d={d} fill={color} />;
}
