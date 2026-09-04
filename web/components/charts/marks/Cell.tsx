/**
 * A heatmap cell: a square with a 2 px surface gap on every side (the gap
 * is left by the layout, not drawn), an optional in-cell value when the cell
 * is large enough, and a hover lift.
 */
export function Cell({ x, y, size, color, label, ink, hot = false, delay = 0 }: { x: number; y: number; size: number; color: string; label?: string; ink?: "light" | "dark"; hot?: boolean; delay?: number }) {
  const inner = Math.max(0, size - 2);
  const className = ["rchart-cell", hot ? "is-hot" : ""].filter(Boolean).join(" ");
  return (
    <g className={className} style={delay ? { animationDelay: `${delay}ms` } : undefined}>
      <rect x={x + 1} y={y + 1} width={inner} height={inner} fill={color} rx={2} />
      {label && (
        <text className={`rchart-cell-label is-${ink ?? "dark"}`} x={x + size / 2} y={y + size / 2} dy="0.35em" textAnchor="middle">
          {label}
        </text>
      )}
    </g>
  );
}
