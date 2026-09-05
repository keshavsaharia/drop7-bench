/**
 * A data line: 2 px, round joins and caps; dashed `6 4` for a control
 * series; 1.5 px for a reference or context series. Solid lines carry
 * pathLength="1" so the mount animation can draw them with a unit dash.
 */
export type Pt = readonly [number, number];

export function linePath(points: readonly Pt[]): string {
  return points.map(([x, y], i) => `${i === 0 ? "M" : "L"}${x},${y}`).join(" ");
}

export function Line({ points, color, dashed = false, thin = false, dim = false }: { points: readonly Pt[]; color: string; dashed?: boolean; thin?: boolean; dim?: boolean }) {
  if (points.length < 2) return null;
  const className = ["rchart-line", dashed ? "is-dashed" : "is-drawn", thin ? "is-thin" : "", dim ? "rchart-dim" : ""].filter(Boolean).join(" ");
  return <path className={className} d={linePath(points)} stroke={color} strokeDasharray={dashed ? "6 4" : undefined} pathLength={dashed ? undefined : 1} />;
}
