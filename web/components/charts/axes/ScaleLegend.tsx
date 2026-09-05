/**
 * The colour-scale legend for a heatmap: one swatch per step with the
 * minimum and maximum labels at the ends, drawn inside the svg to the right
 * of the grid (or below it on narrow widths).
 */
export function ScaleLegend({ x, y, colors, min, max, swatch = 14, vertical = true }: { x: number; y: number; colors: string[]; min: string; max: string; swatch?: number; vertical?: boolean }) {
  const gap = 2;
  return (
    <g className="rchart-scale-legend" aria-hidden="true">
      {colors.map((color, index) => (
        <rect
          key={index}
          x={vertical ? x : x + index * (swatch + gap)}
          y={vertical ? y + (colors.length - 1 - index) * (swatch + gap) : y}
          width={swatch}
          height={swatch}
          fill={color}
          rx={2}
        />
      ))}
      {vertical ? (
        <>
          <text className="rchart-tick" x={x + swatch + 6} y={y + swatch / 2} dy="0.35em">
            {max}
          </text>
          <text className="rchart-tick" x={x + swatch + 6} y={y + (colors.length - 1) * (swatch + gap) + swatch / 2} dy="0.35em">
            {min}
          </text>
        </>
      ) : (
        <>
          <text className="rchart-tick" x={x} y={y + swatch + 6} dy="0.9em">
            {min}
          </text>
          <text className="rchart-tick" x={x + colors.length * (swatch + gap) - gap} y={y + swatch + 6} dy="0.9em" textAnchor="end">
            {max}
          </text>
        </>
      )}
    </g>
  );
}
