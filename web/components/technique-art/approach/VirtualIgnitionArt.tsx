/**
 * Card art for `heuristic-search/virtual-ignition`: score a board by asking
 * what would clear if a disc were dropped, without dropping one. A dashed
 * three falls into a column, the run it would complete outlines and fires, the
 * cover beside it shows the number it would give up — and then the whole thing
 * un-fires and the ghost lifts back out. Every real disc is exactly where it
 * was. The resting frame is the untouched board with the answer drawn over it.
 *
 * Server component. Motion lives in virtual-ignition.css (transform and
 * opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtRing, ArtScore, BOARD, cellCenter, columnX } from "../board";
import "./virtual-ignition.css";

/**
 * The real board, minus the two discs and the cover the ghost wave touches.
 * Verified against the engine's rules: nothing pops as it stands, and a three
 * in column 4 would clear the row-4 run and open the cracked cover under it.
 */
const REAL =
  "0000000" + "0000000" + "0000000" + "2006050" + "7600000" + "6548505" + "5142463";

/** The candidate ignition point, and the run a virtual three would complete. */
const SEED_COL = 4;
const WAVE_ROW = 4;
const WAVE_COLS = [3, 4, 5];
/** The cover that wave would reach, and the number it would give up. */
const COVER = { col: 5, row: 5 };
/** Where the ghost waits, above its column; virtual-ignition.css falls it 93px into the cell. */
const GHOST_Y = 14;
const SEED_X = columnX(SEED_COL);

export function VirtualIgnitionArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-virtual-ignition",
        "A dashed disc drops onto a Drop7 board, the run it would clear fires and un-fires, and the real board is left exactly as it was",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={REAL} />
      </ArtBoard>

      <ArtDisc data-anim="dip" value={3} col={3} row={WAVE_ROW} />
      <ArtDisc data-anim="dip" value={3} col={5} row={WAVE_ROW} />
      <ArtGray data-anim="dip" cracked col={COVER.col} row={COVER.row} />
      <ArtDisc data-anim="ghost-reveal" opacity={0} value={6} col={COVER.col} row={COVER.row} />
      <ArtRing col={COVER.col} row={COVER.row} />

      <rect
        data-anim="flash"
        opacity={0}
        x={BOARD.x + WAVE_COLS[0] * BOARD.cell + 1}
        y={BOARD.y + WAVE_ROW * BOARD.cell + 1}
        width={WAVE_COLS.length * BOARD.cell - 2}
        height={BOARD.cell - 2}
        rx={2}
        fill="var(--color-accent)"
      />

      <g data-anim="outline" className="ghost-outline">
        {WAVE_COLS.map((col) => {
          const [cx, cy] = cellCenter(col, WAVE_ROW);
          return <rect key={col} x={cx - 8} y={cy - 8} width={16} height={16} rx={2} />;
        })}
      </g>

      <ArtScore data-anim="score" depth={1} col={SEED_COL} row={WAVE_ROW - 1} />

      <g data-anim="ghost" className="ghost-disc">
        <circle cx={SEED_X} cy={GHOST_Y} r={7.2} />
        <text
          x={SEED_X}
          y={GHOST_Y}
          textAnchor="middle"
          dominantBaseline="central"
          fontSize={9}
          fontWeight={700}
          fontFamily={ART_MONO}
          fill="var(--color-accent)"
        >
          3
        </text>
      </g>

      <text x={columnX(SEED_COL) + 13} y={17} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        virtual
      </text>

      <g data-anim="step-a" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          drop nothing,
        </text>
        <text x={158} y={101}>
          ask anyway
        </text>
      </g>
      <g data-anim="step-b" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
        <text x={158} y={88}>
          three would go,
        </text>
        <text x={158} y={101}>
          a cover would open
        </text>
      </g>
      <g className="tart-final" data-anim="rest" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          the answer is kept,
        </text>
        <text x={158} y={101}>
          the board untouched
        </text>
      </g>
    </svg>
  );
}
