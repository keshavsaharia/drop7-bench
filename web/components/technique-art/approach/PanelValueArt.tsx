/**
 * Card art for `constructive-reservoir/panel-value`: a panel is one position
 * with the same disc dropped into every legal column, each sibling carried
 * forward over the futures they all share. On play the seven measured
 * outcomes grow to ragged lengths, their successor states settle into the
 * reservoir, and the value learned from that pool comes back on one straight
 * mark for all seven: a value per state, not a comparison inside the position.
 *
 * Server component. Motion lives in panel-value.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, type BoardGeometry } from "../board";
import "./panel-value.css";

/** The one disc of the position; the panel changes only the column it goes in. */
const DISC = 4;
/** Top of each sibling's column strip: seven rows, one per legal column. */
const ROW_TOP = [14, 31, 48, 65, 82, 99, 116];
const CELL = 15;
const BAR_X = 126;
const BAR_H = 7;
/** How far each column's line got, measured over the futures every column shared. */
const BAR_W = [44, 18, 58, 30, 12, 50, 24];
/** Where the pooled value leaves every column: one mark for all seven. */
const LEARNED_X = 160;
const TRAY = { x: 12, y: 140, w: 294, h: 30 };

function strip(row: number): BoardGeometry {
  return { x: 12, y: ROW_TOP[row], cell: CELL, cols: 7, rows: 1 };
}

/** The reservoir the successor states are drawn into, this panel's among them. */
const POOL = Array.from({ length: 22 }, (_, i) => {
  const col = i % 11;
  const row = i < 11 ? 0 : 1;
  return {
    cx: 186 + col * 11 + ((i % 3) - 1) * 2,
    cy: 149 + row * 12 + ((col % 3) - 1) * 3,
    /** The seven this panel just contributed, among everything already pooled. */
    fresh: i % 3 === 1,
  };
});

export function PanelValueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-panel-value",
        "The same disc dropped into every column of one position, each outcome measured, and a value learned from a pool of states that ends all seven at the same mark",
        props,
      )}
    >
      <text x={12} y={10} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        the panel: every column
      </text>
      <g className="panel" data-anim="panel">
        {ROW_TOP.map((top, row) => (
          <g key={top}>
            <ArtBoard g={strip(row)} />
            <ArtDisc value={DISC} col={row} row={0} g={strip(row)} />
          </g>
        ))}
      </g>
      <g className="bars" data-anim="bars">
        {ROW_TOP.map((top, row) => (
          <rect
            key={top}
            x={BAR_X}
            y={top + 4}
            width={BAR_W[row]}
            height={BAR_H}
            rx={2}
            fill="var(--color-accent-strong)"
          />
        ))}
      </g>
      <g className="pool" data-anim="pool">
        <rect
          x={TRAY.x}
          y={TRAY.y}
          width={TRAY.w}
          height={TRAY.h}
          rx={5}
          fill="var(--color-cell)"
          stroke="var(--color-rule-strong)"
        />
        {POOL.map((dot) => (
          <circle
            key={`${dot.cx}-${dot.cy}`}
            cx={dot.cx}
            cy={dot.cy}
            r={3}
            fill={dot.fresh ? "var(--color-accent)" : "var(--color-ink-4)"}
          />
        ))}
        <text x={20} y={159} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
          reservoir of successor states
        </text>
      </g>
      <g className="learned tart-final" data-anim="learned">
        <line
          x1={LEARNED_X}
          y1={8}
          x2={LEARNED_X}
          y2={136}
          stroke="var(--color-series-2)"
          strokeWidth={1.4}
          strokeDasharray="3 3"
        />
        {ROW_TOP.map((top) => (
          <line
            key={top}
            x1={LEARNED_X - 6}
            y1={top + 7.5}
            x2={LEARNED_X + 6}
            y2={top + 7.5}
            stroke="var(--color-series-2)"
            strokeWidth={2.4}
            strokeLinecap="round"
          />
        ))}
        <text x={LEARNED_X + 12} y={10} fontFamily={ART_MONO} fontSize={9} fill="var(--color-series-2)">
          learned value
        </text>
      </g>
    </svg>
  );
}
