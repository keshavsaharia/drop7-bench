"use client";
/**
 * The legend: one entry per series, keyed by a swatch that mirrors the mark
 * (line, dashed line, rect, dot, hollow dot, diamond, band). Hovering an
 * entry tells the chart which series to keep at full strength; the chart
 * dims the others to 30 %. The frame renders it only for two or more series.
 */
import { Key, type KeyStyle } from "./Key";

export interface LegendItem {
  name: string;
  key: KeyStyle;
}

export interface LegendProps {
  items: LegendItem[];
  active: number | null;
  onHover: (index: number | null) => void;
}

export function Legend({ items, active, onHover }: LegendProps) {
  return (
    <ul className="rchart-legend" onMouseLeave={() => onHover(null)}>
      {items.map((item, index) => (
        <li
          key={`${index}-${item.name}`}
          className={active !== null && active !== index ? "is-dim" : undefined}
          onMouseEnter={() => onHover(index)}
          onFocus={() => onHover(index)}
          onBlur={() => onHover(null)}
        >
          <Key style={item.key} />
          <span>{item.name}</span>
        </li>
      ))}
    </ul>
  );
}
