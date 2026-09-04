/**
 * <FigureGrid names={["score-vs-depth", "moves-vs-depth"]} columns={2} />
 *
 * Several independent figures side by side for "What happened" sections:
 * two or three columns at wide widths, one column below. Each figure keeps
 * its own chart, table toggle and notes block; none breaks out to the wide
 * column. Server component; styled by charts.css `.figure-grid`.
 */
import { Figure } from "./Figure";

export function FigureGrid({ names, columns = 2, captions }: { names: string[]; columns?: 2 | 3; captions?: (string | undefined)[] }) {
  return (
    <div className={`figure-grid is-columns-${columns}`}>
      {names.map((name, index) => (
        <Figure key={name} name={name} caption={captions?.[index]} wide={false} compact />
      ))}
    </div>
  );
}
