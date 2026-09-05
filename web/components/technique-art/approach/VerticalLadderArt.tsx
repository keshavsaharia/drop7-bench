/**
 * Card art for `fair-expectimax/vertical-ladder`: a column that stores energy
 * the score cannot see, and a rise that spends it. Three discs sit inert in
 * the middle column of a real board; the covered row arrives and makes that
 * column four tall, so the 4 in it clears; the 6 above drops into a row of six
 * and clears in turn. The emptied column and the two wave scores are the
 * resting frame.
 *
 * The board content sits in a nested <svg> so the covered row is clipped until
 * the rise carries it in.
 *
 * Server component. Motion lives in vertical-ladder.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import {
  ArtBoard,
  ArtCells,
  ArtDisc,
  ArtGray,
  ArtScore,
  BOARD,
  boardHeight,
  boardWidth,
  type BoardGeometry,
} from "../board";
import "./vertical-ladder.css";

/** Board-local geometry: the nested <svg> is already offset to the board. */
const INNER: BoardGeometry = { ...BOARD, x: 0, y: 0 };

/**
 * The board once the rise has landed and the chain has run: the covered row is
 * drawn separately, and column 3 has lost the 4 and the 6 it spent.
 */
const SETTLED =
  "0000000" +
  "0000000" +
  "0000000" +
  "0000000" +
  "4710510" +
  "6155464" +
  "0000000";

const LADDER_COL = 3;
const LABEL_X = 152;

export function VerticalLadderArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-vertical-ladder",
        "Three discs sit inert in one column until a covered row rises under them, and the column then clears in two waves",
        props,
      )}
    >
      <ArtBoard />
      <svg
        x={BOARD.x}
        y={BOARD.y}
        width={boardWidth()}
        height={boardHeight()}
        overflow="hidden"
      >
        <g data-anim="rise">
          <ArtCells cells={SETTLED} g={INNER} />
          <g data-anim="row">
            {Array.from({ length: INNER.cols }, (_, col) => (
              <ArtGray key={col} col={col} row={INNER.rows - 1} g={INNER} />
            ))}
          </g>
          <ArtDisc value={4} col={LADDER_COL} row={4} g={INNER} data-anim="four" opacity={0} />
          <ArtDisc value={6} col={LADDER_COL} row={4} g={INNER} data-anim="six" opacity={0} />
        </g>
      </svg>
      <ArtScore depth={1} col={LADDER_COL} row={2} data-anim="wave-one" />
      <ArtScore depth={2} col={LADDER_COL} row={3} data-anim="wave-two" />
      <g fontFamily={ART_MONO} fontSize={9}>
        <g data-anim="step-a" opacity={0} fill="var(--color-ink-2)">
          <text x={LABEL_X} y={68}>
            three discs,
          </text>
          <text x={LABEL_X} y={80}>
            nothing matches
          </text>
        </g>
        <g data-anim="step-b" opacity={0} fill="var(--color-highlight)">
          <text x={LABEL_X} y={68}>
            the rise makes
          </text>
          <text x={LABEL_X} y={80}>
            the column four
          </text>
        </g>
        <g data-anim="step-c" opacity={0} fill="var(--color-accent)">
          <text x={LABEL_X} y={68}>
            it takes itself
          </text>
          <text x={LABEL_X} y={80}>
            apart, two waves
          </text>
        </g>
        <g className="tart-final" data-anim="rest" fill="var(--color-ink-2)">
          <text x={LABEL_X} y={68}>
            energy stored now,
          </text>
          <text x={LABEL_X} y={80}>
            spent by the rise
          </text>
        </g>
      </g>
    </svg>
  );
}
