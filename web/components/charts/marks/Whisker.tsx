/**
 * An interval whisker in ink (never the series colour) with 4 px caps. A
 * one-sided bound leaves the open end uncapped, so "no upper bound recorded"
 * is visible. `ring` draws a surface-coloured halo behind the whisker so it
 * reads over a filled bar of either diverging colour.
 */
import { WHISKER_CAP } from "../tokens";

export function Whisker({
  x1,
  y1,
  x2,
  y2,
  open = null,
  ring = false,
  strong = false,
  cap = WHISKER_CAP,
  dim = false,
}: {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  /** Which end has no recorded bound (drawn without a cap). */
  open?: "start" | "end" | null;
  ring?: boolean;
  strong?: boolean;
  cap?: number;
  dim?: boolean;
}) {
  const vertical = Math.abs(x1 - x2) < 0.01;
  const className = ["rchart-whisker", strong ? "is-strong" : "", dim ? "rchart-dim" : ""].filter(Boolean).join(" ");
  const caps = (
    <>
      {open !== "start" && (vertical ? <line x1={x1 - cap} x2={x1 + cap} y1={y1} y2={y1} /> : <line x1={x1} x2={x1} y1={y1 - cap} y2={y1 + cap} />)}
      {open !== "end" && (vertical ? <line x1={x2 - cap} x2={x2 + cap} y1={y2} y2={y2} /> : <line x1={x2} x2={x2} y1={y2 - cap} y2={y2 + cap} />)}
    </>
  );
  return (
    <g>
      {ring && (
        <g className="rchart-whisker-ring">
          <line x1={x1} y1={y1} x2={x2} y2={y2} />
          {caps}
        </g>
      )}
      <g className={className}>
        <line x1={x1} y1={y1} x2={x2} y2={y2} />
        {caps}
      </g>
    </g>
  );
}
