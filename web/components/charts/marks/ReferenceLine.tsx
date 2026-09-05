/**
 * Reference marks that carry a recorded value: the zero line (1 px solid,
 * --color-ink-3), a threshold or target (1.5 px dashed, ink, with an ink
 * label), a recorded band (lo..hi as a translucent ink wash), and the hover
 * crosshair. Labels are always ink, never a series colour.
 */
export function ZeroLine({ orientation, at, from, to }: { orientation: "h" | "v"; at: number; from: number; to: number }) {
  return orientation === "h" ? <line className="rchart-zero" x1={from} x2={to} y1={at} y2={at} /> : <line className="rchart-zero" x1={at} x2={at} y1={from} y2={to} />;
}

export function ReferenceLine({
  orientation,
  at,
  from,
  to,
  label,
  solid = false,
  labelAt = "start",
}: {
  orientation: "h" | "v";
  at: number;
  from: number;
  to: number;
  label?: string;
  solid?: boolean;
  /** For a horizontal line: label at the left or right end; for a vertical one: at the top, to the right or the left. */
  labelAt?: "start" | "end";
}) {
  const className = solid ? "rchart-ref is-solid" : "rchart-ref";
  if (orientation === "h") {
    return (
      <g>
        <line className={className} x1={from} x2={to} y1={at} y2={at} />
        {label && (
          <text className="rchart-ref-label" x={labelAt === "start" ? from + 6 : to - 6} y={at - 5} textAnchor={labelAt === "start" ? "start" : "end"}>
            {label}
          </text>
        )}
      </g>
    );
  }
  return (
    <g>
      <line className={className} x1={at} x2={at} y1={from} y2={to} />
      {label && (
        <text className="rchart-ref-label" x={labelAt === "start" ? at + 5 : at - 5} y={from + 11} textAnchor={labelAt === "start" ? "start" : "end"}>
          {label}
        </text>
      )}
    </g>
  );
}

export function ReferenceBand({ orientation, from, to, start, end }: { orientation: "h" | "v"; from: number; to: number; start: number; end: number }) {
  const lo = Math.min(from, to);
  const size = Math.abs(to - from);
  if (size <= 0) return null;
  return orientation === "h" ? (
    <rect className="rchart-ref-band" x={start} y={lo} width={Math.max(0, end - start)} height={size} />
  ) : (
    <rect className="rchart-ref-band" x={lo} y={start} width={size} height={Math.max(0, end - start)} />
  );
}

export function Crosshair({ x, top, bottom }: { x: number; top: number; bottom: number }) {
  return <line className="rchart-crosshair" x1={x} x2={x} y1={top} y2={bottom} />;
}
