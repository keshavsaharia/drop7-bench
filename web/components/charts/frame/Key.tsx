/**
 * The small colour key drawn before a legend entry or a tooltip row. It
 * mirrors the mark: a short stroke for a line (dashed for a control), a
 * square for a bar or cell, a dot or hollow dot for a marker, a diamond for
 * a reference estimate, a translucent square for a band. Colour arrives as a
 * CSS variable reference and is applied through the `--key` custom property
 * so charts.css owns every geometry rule.
 */
export type KeyShape = "line" | "dashed" | "rect" | "dot" | "hollow" | "diamond" | "band";

export interface KeyStyle {
  color: string;
  shape: KeyShape;
  thin?: boolean;
}

export function Key({ style }: { style: KeyStyle }) {
  const className = ["rchart-key", `is-${style.shape}`, style.thin ? "is-thin" : ""].filter(Boolean).join(" ");
  return <span className={className} style={{ "--key": style.color } as React.CSSProperties} aria-hidden="true" />;
}
