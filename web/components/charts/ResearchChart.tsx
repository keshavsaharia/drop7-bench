"use client";
/**
 * Renders a validated research-figure spec (web/lib/charts/spec.ts) by
 * dispatching on `spec.kind` to the kind components under ./kinds. Every
 * kind computes pixel positions, axis ranges, tick values and text layout
 * only; every number it prints is a value from the spec, formatted.
 *
 * `facet: "series"` on a line, dot or strip spec renders one panel per
 * series with shared axis domains (a layout choice, not a number), one
 * legend-free panel each titled by the series name.
 */
import type { ReactNode } from "react";
import type { FigureSpec } from "@/lib/charts/spec";
import { EvidenceStrip } from "@/components/EvidenceStrip";
import { EmptyState } from "./frame/EmptyState";
import { BarChart } from "./kinds/BarChart";
import { Heatmap } from "./kinds/Heatmap";
import { Histogram } from "./kinds/Histogram";
import { LineChart } from "./kinds/LineChart";
import { PairedDeltas } from "./kinds/PairedDeltas";
import { RowChart } from "./kinds/RowChart";
import { Sparkline } from "./kinds/Sparkline";
import { Stacked } from "./kinds/Stacked";
import { Strip } from "./kinds/Strip";
import { domainOf, numericX, valueRange, type KindProps } from "./kinds/shared";

export interface ResearchChartProps {
  spec: FigureSpec;
  id?: string;
  height?: number;
  compact?: boolean;
  /** The source-data table (server-rendered), shown behind the frame's Chart/Table toggle. */
  table?: ReactNode;
  /** Overrides the spec title; `null` hides it (the page carries its own heading). */
  title?: string | null;
}

function Kind(props: KindProps) {
  switch (props.spec.kind) {
    case "line":
      return <LineChart {...props} />;
    case "bar":
      return <BarChart {...props} />;
    case "delta":
    case "forest":
    case "dot":
      return <RowChart {...props} />;
    case "paired":
      return <PairedDeltas {...props} />;
    case "strip":
      return <Strip {...props} />;
    case "histogram":
      return <Histogram {...props} />;
    case "stacked":
      return <Stacked {...props} />;
    case "heatmap":
      return <Heatmap {...props} />;
    case "sparkline": {
      const series = props.spec.series[0];
      return (
        <div className="rchart">
          {props.title && <h4 className="rchart-title">{props.title}</h4>}
          <Sparkline points={series.points} label={props.spec.title} unit={props.spec.y?.unit} source={series.sourceRecord} />
        </div>
      );
    }
    default:
      return <EmptyState reason="invalid-spec" detail={`unknown kind ${String(props.spec.kind)}`} />;
  }
}

const FACETABLE = new Set(["line", "dot", "strip"]);

export function ResearchChart({ spec, id, height, compact, table, title }: ResearchChartProps) {
  const heading = title === undefined ? spec.title : title ?? undefined;
  if (spec.facet === "series" && FACETABLE.has(spec.kind) && spec.series.length > 1) {
    const yDomain = domainOf(valueRange(spec, false), spec.y?.domain);
    const xs = spec.kind === "line" ? spec.series.flatMap((s) => s.points.map((p) => numericX(p, spec.x))) : [];
    const xDomain = xs.length ? domainOf(xs) : undefined;
    const columns = Math.min(3, spec.series.length);
    return (
      <div className="rchart rchart-facets" id={id}>
        {heading && (
          <h4 className="rchart-title" id={id ? `${id}-title` : undefined}>
            {heading}
          </h4>
        )}
        <div className="rchart-facet-grid" style={{ "--facet-columns": columns } as React.CSSProperties}>
          {spec.series.map((series, index) => (
            <Kind key={`${index}-${series.name}`} spec={{ ...spec, facet: undefined, series: [series], title: series.name }} id={id ? `${id}-f${index}` : undefined} title={series.name} compact yDomain={yDomain} xDomain={xDomain} noLegend />
          ))}
        </div>
        {spec.evidence && <EvidenceStrip {...spec.evidence} />}
        {table && (
          <details className="rchart-data">
            <summary>Source data</summary>
            {table}
          </details>
        )}
      </div>
    );
  }
  return <Kind spec={spec} id={id} title={heading} height={height} compact={compact} table={table} />;
}
