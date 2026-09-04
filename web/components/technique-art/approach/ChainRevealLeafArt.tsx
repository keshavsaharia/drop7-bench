/**
 * Card art for `lifetime-objective/chain-reveal-leaf`: value a board by the
 * chain and the reveals it can still produce. A four lands on a cracked cover
 * and completes a run of four; the wave pays +7, cracks the cover next door
 * and opens the one under the landing, which gives up a four of its own; that
 * four is now a column run of three, so it clears as the second wave for +39
 * and opens the cover beside it. The reveal is both what the chain produces
 * and what feeds it.
 *
 * Server component. Motion lives in chain-reveal-leaf.css (transform and
 * opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtRing, ArtScore, BOARD } from "../board";
import "./chain-reveal-leaf.css";

/**
 * The board without the run that clears or the two covers the chain opens.
 * Verified against the engine's rules: nothing pops until the fourth four
 * lands, and the reveal it produces carries the chain into a second wave.
 */
const PILE =
  "0000000" + "0000000" + "0000030" + "0000075" + "0000017" + "5006063" + "4615326";

/** The run the landing disc completes, and the column it falls into. */
const WAVE_ROW = 4;
const DROP_COL = 1;
const STANDING_COLS = [0, 2, 3];
/** The cover the first wave opens, and the cover the second wave opens. */
const FIRST_COVER = { col: 1, row: 5 };
const SECOND_COVER = { col: 2, row: 5 };

export function ChainRevealLeafArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-chain-reveal-leaf",
        "A run of fours clears and opens a cover, the four it reveals clears as the second wave for thirty-nine, and that wave opens the cover beside it",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={PILE} />
      </ArtBoard>

      <rect
        data-anim="flash-1"
        opacity={0}
        x={BOARD.x + 1}
        y={BOARD.y + WAVE_ROW * BOARD.cell + 1}
        width={4 * BOARD.cell - 2}
        height={BOARD.cell - 2}
        rx={2}
        fill="var(--color-accent)"
      />
      <rect
        data-anim="flash-2"
        opacity={0}
        x={BOARD.x + FIRST_COVER.col * BOARD.cell + 1}
        y={BOARD.y + FIRST_COVER.row * BOARD.cell + 1}
        width={BOARD.cell - 2}
        height={BOARD.cell - 2}
        rx={2}
        fill="var(--color-accent)"
      />

      <g data-anim="wave-1" opacity={0}>
        {STANDING_COLS.map((col) => (
          <ArtDisc key={col} value={4} col={col} row={WAVE_ROW} />
        ))}
        <ArtDisc data-anim="drop" value={4} col={DROP_COL} row={WAVE_ROW} />
      </g>

      <ArtGray data-anim="cover-1-out" opacity={0} cracked col={FIRST_COVER.col} row={FIRST_COVER.row} />
      <ArtDisc data-anim="wave-2" opacity={0} value={4} col={FIRST_COVER.col} row={FIRST_COVER.row} />

      <ArtGray data-anim="cover-2-solid" opacity={0} col={SECOND_COVER.col} row={SECOND_COVER.row} />
      <ArtGray data-anim="cover-2-cracked" opacity={0} cracked col={SECOND_COVER.col} row={SECOND_COVER.row} />
      <ArtDisc data-anim="cover-2-open" value={6} col={SECOND_COVER.col} row={SECOND_COVER.row} />
      <ArtRing col={SECOND_COVER.col} row={SECOND_COVER.row} />

      <ArtScore data-anim="score-1" opacity={0} depth={1} col={DROP_COL} row={WAVE_ROW - 1} />
      <ArtScore data-anim="score-2" depth={2} col={DROP_COL} row={WAVE_ROW} />

      <g data-anim="step-a" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          one drop,
        </text>
        <text x={158} y={101}>
          four clear
        </text>
      </g>
      <g data-anim="step-b" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
        <text x={158} y={88}>
          the wave opens
        </text>
        <text x={158} y={101}>
          a cover
        </text>
      </g>
      <g className="tart-final" data-anim="rest" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          the reveal feeds
        </text>
        <text x={158} y={101}>
          the second wave
        </text>
      </g>
    </svg>
  );
}
