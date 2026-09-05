/**
 * Card art for `tree-search/puct`: the selection rule itself. The prior over
 * the seven columns is drawn once, above the line, and never moves; the visit
 * counts grow below it. The band showing which child the search descends into
 * starts on the column the prior likes best, and as visits accumulate the
 * term that favours the prior shrinks, so a column the prior barely liked
 * earns the attention and ends with the visits.
 *
 * Server component. Motion lives in puct.css (transform only); the markup is
 * the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./puct.css";

/** The seven legal columns, spread across the frame. */
const COLUMNS = [0, 1, 2, 3, 4, 5, 6];
const SLOT_X = 60;
const SLOT_STEP = 40;
const BAR_WIDTH = 18;

/** A prior from a handful of probes: column 3 is liked, column 7 is not. */
const PRIOR = [12, 26, 46, 20, 10, 14, 8];
/** Where the visits end up once value has had time to speak. */
const VISITS = [6, 14, 32, 11, 7, 9, 46];
/** When each column's visits accumulate. */
const VISIT_ANIM = ["visit-slow", "visit-mid", "visit-early", "visit-slow", "visit-slow", "visit-slow", "visit-late"];

const PRIOR_BASE = 88;
const VISIT_TOP = 96;
const DIVIDER = 92;
/** The column the band rests on: the one the prior did not favour. */
const PICKED = 6;

function slot(column: number): number {
  return SLOT_X + column * SLOT_STEP;
}

export function PuctArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-puct",
        "A fixed prior over seven columns above the line and growing visit counts below it, with the search settling on a column the prior barely favoured",
        props,
      )}
    >
      <g data-anim="pick">
        <rect
          x={slot(PICKED) - 13}
          y="28"
          width="26"
          height="126"
          rx="3"
          fill="var(--color-accent-soft)"
        />
        <path
          d={`M${slot(PICKED) - 6},16L${slot(PICKED) + 6},16L${slot(PICKED)},25Z`}
          fill="var(--color-accent)"
        />
      </g>
      <g>
        {COLUMNS.map((column) => (
          <rect
            key={column}
            x={slot(column) - BAR_WIDTH / 2}
            y={PRIOR_BASE - PRIOR[column]}
            width={BAR_WIDTH}
            height={PRIOR[column]}
            rx="2"
            fill="var(--color-accent-soft)"
            stroke="var(--color-accent-strong)"
            strokeWidth="1"
          />
        ))}
      </g>
      <line
        x1={slot(0) - 24}
        y1={DIVIDER}
        x2={slot(6) + 14}
        y2={DIVIDER}
        stroke="var(--color-rule-strong)"
        strokeWidth="1"
      />
      <g>
        {COLUMNS.map((column) => (
          <rect
            key={column}
            data-anim={VISIT_ANIM[column]}
            x={slot(column) - BAR_WIDTH / 2}
            y={VISIT_TOP}
            width={BAR_WIDTH}
            height={VISITS[column]}
            rx="2"
            fill="var(--color-accent)"
          />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="84">
          prior
        </text>
        <text x="8" y="112">
          visits
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="168" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          visits outgrow the prior
        </text>
      </g>
    </svg>
  );
}
