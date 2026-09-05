/**
 * Card art for `heuristic-search/tunneling`: a channel dug down beside the
 * tall side of the board so the covers buried high in the wall can be reached.
 * A four lands on a column of fours, the whole column clears from top to
 * floor, and the wall it opens onto gives up its high cover — the cracked gray
 * beside the trench reveals its number, the solid one below it cracks. The
 * resting frame is the open trench next to the pile it now reaches into.
 *
 * Server component. Motion lives in tunneling.css (transform and opacity
 * only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtRing, ArtScore, BOARD, columnX } from "../board";
import "./tunneling.css";

/**
 * The board without the column being dug or the two covers beside it: a full
 * pile whose only low ground is column 2. Verified against the engine's rules:
 * nothing pops until the fourth four lands.
 */
const PILE =
  "0000000" + "0008500" + "0402710" + "0300572" + "6102325" + "6600163" + "5401231";

/** The column of fours that clears: rows 4, 5 and 6 stand, row 3 arrives. */
const TRENCH_COL = 2;
const STANDING_ROWS = [4, 5, 6];
/** The wall face beside the trench: a cracked cover high up, a solid one under it. */
const HIGH_COVER = { col: 3, row: 3 };
const LOW_COVER = { col: 3, row: 5 };

export function TunnelingArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-tunneling",
        "A column of fours clears from top to floor, opening a trench that reaches the gray discs buried high in the wall beside it",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={PILE} />
      </ArtBoard>

      <rect
        data-anim="flash"
        opacity={0}
        x={BOARD.x + TRENCH_COL * BOARD.cell + 1}
        y={BOARD.y + 3 * BOARD.cell + 1}
        width={BOARD.cell - 2}
        height={4 * BOARD.cell - 2}
        rx={2}
        fill="var(--color-accent)"
      />

      <g data-anim="dig" opacity={0}>
        {STANDING_ROWS.map((row) => (
          <ArtDisc key={row} value={4} col={TRENCH_COL} row={row} />
        ))}
        <ArtDisc data-anim="drop" value={4} col={TRENCH_COL} row={3} />
      </g>

      <ArtGray data-anim="cover-out" opacity={0} cracked col={HIGH_COVER.col} row={HIGH_COVER.row} />
      <ArtDisc data-anim="cover-in" value={3} col={HIGH_COVER.col} row={HIGH_COVER.row} />
      <ArtGray data-anim="cover-out" opacity={0} col={LOW_COVER.col} row={LOW_COVER.row} />
      <ArtGray data-anim="cover-in" cracked col={LOW_COVER.col} row={LOW_COVER.row} />
      <ArtRing col={HIGH_COVER.col} row={HIGH_COVER.row} />

      <ArtScore data-anim="score" opacity={0} depth={1} col={TRENCH_COL} row={4} />

      <text
        x={columnX(TRENCH_COL)}
        y={166}
        textAnchor="middle"
        fontFamily={ART_MONO}
        fontSize={9}
        fill="var(--color-ink-3)"
      >
        trench
      </text>

      <g data-anim="step-a" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          a cover sits high
        </text>
        <text x={158} y={101}>
          in the wall
        </text>
      </g>
      <g data-anim="step-b" opacity={0} fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
        <text x={158} y={88}>
          the column clears
        </text>
        <text x={158} y={101}>
          top to floor
        </text>
      </g>
      <g className="tart-final" data-anim="rest" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={158} y={88}>
          the trench reaches
        </text>
        <text x={158} y={101}>
          what was walled in
        </text>
      </g>
    </svg>
  );
}
