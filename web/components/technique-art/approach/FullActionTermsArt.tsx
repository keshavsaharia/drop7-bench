/**
 * Card art for `fair-expectimax/full-action-terms`: one drop charged twice.
 * On play a disc lands in column six, clears a wave and then a second, and the
 * chain cracks a cover on its way; the state rows the search already reads off
 * the resulting board fill in, and then the old one-move policy's action rows
 * arrive underneath as extra charges. A dashed tie links the two rows that
 * describe the same thing.
 *
 * Server component. Motion lives in full-action-terms.css (opacity and
 * transform only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtGray, ArtDisc, ArtRing, ArtScore } from "../board";
import "./full-action-terms.css";

/** The board before the drop: a short column six, a cover held over column three. */
const CELLS =
  "0000000" + "0000000" + "0000000" + "0005000" + "0006000" + "0661403" + "3522645";

/** Rows the search already reads off the board the move produced. */
const STATE_ROWS = [
  { y: 46, width: 120 },
  { y: 58, width: 84 },
];

/** Rows the one-move policy charged for the move itself, restored here. */
const ACTION_ROWS = [
  { y: 98, width: 96 },
  { y: 110, width: 132 },
  { y: 122, width: 70 },
];

export function FullActionTermsArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-full-action-terms",
        "One drop scored twice: the rows read off the resulting board, then the restored rows charging for the move itself",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={CELLS} />
      <ArtGray col={2} row={4} opacity={0} data-anim="cover-solid" />
      <ArtGray cracked col={2} row={4} data-anim="cover-cracked" />
      <ArtDisc value={2} col={5} row={5} data-anim="drop" />
      <ArtRing col={5} row={5} data-anim="ring" />
      <ArtScore depth={1} col={5} row={4} data-anim="wave-1" />
      <ArtScore depth={2} col={1} row={3} data-anim="wave-2" />
      <g className="state" data-anim="state">
        <text x={152} y={38} fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          state
        </text>
        {STATE_ROWS.map((row) => (
          <rect key={row.y} x={152} y={row.y} width={row.width} height={8} rx={2} fill="var(--color-ink-3)" />
        ))}
      </g>
      <g className="action" data-anim="action">
        <text x={152} y={90} fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          action
        </text>
        {ACTION_ROWS.map((row) => (
          <rect key={row.y} x={152} y={row.y} width={row.width} height={8} rx={2} fill="var(--color-series-2)" />
        ))}
      </g>
      <g className="tie" data-anim="tie" stroke="var(--color-highlight)" fill="var(--color-highlight)">
        <path d="M272 50C304 60 306 104 284 114" fill="none" strokeWidth={1.4} strokeDasharray="4 3" />
        <circle cx={272} cy={50} r={2.6} />
        <circle cx={284} cy={114} r={2.6} />
      </g>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={168} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          the same drop, charged twice
        </text>
      </g>
    </svg>
  );
}
