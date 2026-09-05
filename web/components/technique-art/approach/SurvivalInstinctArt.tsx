/**
 * Card art for `lifetime-objective/survival-instinct`: the one human rule the
 * search is handed at the root. A 3 waits over a column that already holds
 * three discs, where it would land fourth of four and could never match its
 * own count, so the rule crosses that cell out; the disc goes instead to a
 * column holding two, lands third of three, and clears.
 *
 * Server component. Motion lives in survival-instinct.css (transform and
 * opacity only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing, ArtScore, cellCenter } from "../board";
import type { ArtProps } from "../registry";
import "./survival-instinct.css";

/**
 * A settled mid-game position: no run on it matches its own value, so nothing
 * is already clearing. Column 2 holds three discs and column 5 holds two.
 */
const CELLS =
  "0000000" +
  "0000000" +
  "0008000" +
  "0507600" +
  "0753205" +
  "3266542" +
  "5643264";

const NEXT = 3;
/** Three discs already: the 3 would land fourth, and a fourth is not a 3. */
const DEAD_COL = 2;
const DEAD_ROW = 3;
/** Two discs already: the 3 lands third of three and clears on arrival. */
const LIVE_COL = 5;
const LIVE_ROW = 4;

const LABEL_X = 154;

export function SurvivalInstinctArt(props: ArtProps) {
  const [deadX, deadY] = cellCenter(DEAD_COL, DEAD_ROW);
  return (
    <svg
      {...artSvgProps(
        "approach-survival-instinct",
        "A 3 refused over a column of three discs and dropped instead where it lands third of three and clears",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={CELLS} />
      </ArtBoard>
      <g className="tart-final" data-anim="refuse">
        <ArtDisc value={NEXT} col={DEAD_COL} row={DEAD_ROW} opacity={0.45} />
        <rect
          x={deadX - 8}
          y={deadY - 8}
          width={16}
          height={16}
          rx={2}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth={1.4}
          strokeDasharray="3 2"
        />
        <path
          d={`M${deadX - 5},${deadY - 5}l10,10M${deadX + 5},${deadY - 5}l-10,10`}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth={1.8}
          strokeLinecap="round"
        />
      </g>
      <ArtDisc value={NEXT} col={LIVE_COL} row={LIVE_ROW} data-anim="drop" />
      <g className="tart-final" data-anim="clear">
        <ArtRing col={LIVE_COL} row={LIVE_ROW} />
        <ArtScore depth={1} col={LIVE_COL} row={LIVE_ROW - 1} />
      </g>
      <text x={LABEL_X} y={44} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        root filter
      </text>
      <g data-anim="state-a" opacity={0}>
        <text x={LABEL_X} y={92} fontFamily={ART_MONO} fontSize={12} fontWeight={700} fill="var(--color-series-2)">
          4 ≠ 3
        </text>
        <text x={LABEL_X} y={106} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          never clears
        </text>
      </g>
      <g className="tart-final" data-anim="state-b">
        <text x={LABEL_X} y={92} fontFamily={ART_MONO} fontSize={12} fontWeight={700} fill="var(--color-highlight)">
          3 = 3
        </text>
        <text x={LABEL_X} y={106} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          clears on landing
        </text>
      </g>
    </svg>
  );
}
