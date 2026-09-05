/**
 * A point marker: r >= 4 filled with the series colour and a 2 px ring in
 * the surface colour so it stays legible over a line or a neighbour. Hollow
 * for reference series and censored games; a diamond for a reference
 * estimate on a forest row. The hovered marker grows to r = 5.5.
 */
import { MARKER_R, MARKER_R_HOT } from "../tokens";

export function Marker({ x, y, color, r = MARKER_R, hot = false, hollow = false, diamond = false, dim = false }: { x: number; y: number; color: string; r?: number; hot?: boolean; hollow?: boolean; diamond?: boolean; dim?: boolean }) {
  const radius = hot ? Math.max(r, MARKER_R_HOT) : r;
  const className = ["rchart-marker", hollow ? "is-hollow" : "", hot ? "is-hot" : "", dim ? "rchart-dim" : ""].filter(Boolean).join(" ");
  if (diamond) {
    const d = radius + 1.5;
    return <polygon className={className} points={`${x},${y - d} ${x + d},${y} ${x},${y + d} ${x - d},${y}`} fill={hollow ? undefined : color} stroke={hollow ? color : undefined} />;
  }
  return <circle className={className} cx={x} cy={y} r={radius} fill={hollow ? undefined : color} stroke={hollow ? color : undefined} />;
}
