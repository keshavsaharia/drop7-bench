/**
 * Card art for `fair-expectimax/transition-rewards`: the search is paid extra
 * for clearing a numbered disc and for exposing a covered one, so the art
 * draws the two events it pays for. A 5 lands in the fifth column, three 5s
 * clear at +7 each and crack the gray beside them, the two 4s above fall into
 * the gap, and the second wave clears at +39 and reveals the gray's number.
 * Each event lights one row of the small ledger that feeds the move's value.
 *
 * The position is a real one: given this board and a 5, the engine plays the
 * drop as a wave of three discs and then a wave of two, with one reveal.
 *
 * Server component. Motion lives in transition-rewards.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtRing, ArtScore } from "../board";
import "./transition-rewards.css";

/** The discs that take no part in the chain, and are still standing at rest. */
const UNTOUCHED = "0000000" + "0000000" + "0000000" + "0000000" + "0700005" + "0600007" + "6000006";

/** The three 5s of the first wave; the last of them is the disc being dropped. */
const WAVE_ONE = [2, 3, 4];
/** The two 4s that fall into the gap and clear as the second wave. */
const WAVE_TWO = [2, 3];
/** Column of the gray disc that cracks on the first wave and opens on the second. */
const GRAY_COL = 1;
/** Value the covered disc was hiding. */
const REVEALED = 5;

export function TransitionRewardsArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-transition-rewards",
        "A disc lands and clears a row at +7, cracking the gray beside it; the survivors fall and clear again at +39, opening the gray",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={UNTOUCHED} />

      <g data-anim="wave-one" opacity={0}>
        <ArtDisc value={5} col={WAVE_ONE[0]} row={6} />
        <ArtDisc value={5} col={WAVE_ONE[1]} row={6} />
        <g data-anim="drop">
          <ArtDisc value={5} col={WAVE_ONE[2]} row={6} />
        </g>
      </g>

      <g data-anim="fall">
        <g data-anim="wave-two" opacity={0}>
          {WAVE_TWO.map((col) => (
            <ArtDisc key={col} value={4} col={col} row={5} />
          ))}
        </g>
      </g>

      <g data-anim="gray-solid" opacity={0}>
        <ArtGray col={GRAY_COL} row={6} />
      </g>
      <g data-anim="gray-cracked" opacity={0}>
        <ArtGray cracked col={GRAY_COL} row={6} />
      </g>

      <g data-anim="score-one" opacity={0}>
        {WAVE_ONE.map((col) => (
          <ArtScore key={col} depth={1} col={col} row={6} />
        ))}
      </g>
      <g data-anim="score-two" opacity={0}>
        {WAVE_TWO.map((col) => (
          <ArtScore key={col} depth={2} col={col} row={6} />
        ))}
      </g>

      <g className="ledger" fontFamily={ART_MONO} fontSize={10} fill="var(--color-ink-2)">
        <circle cx={162} cy={62} r={4} fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.2} />
        <circle cx={162} cy={62} r={4} fill="var(--color-accent)" data-anim="pay-clear" />
        <text x={172} y={66}>
          clear
        </text>
        <circle cx={162} cy={102} r={4} fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.2} />
        <circle cx={162} cy={102} r={4} fill="var(--color-accent)" data-anim="pay-reveal" />
        <text x={172} y={106}>
          reveal
        </text>
        <path
          d="M214 62h10v40h-10M224 82h12"
          fill="none"
          stroke="var(--color-rule-strong)"
          strokeWidth={1.2}
        />
        <text x={240} y={86}>
          move value
        </text>
      </g>

      <g className="tart-final" data-anim="opened">
        <ArtDisc value={REVEALED} col={GRAY_COL} row={6} />
        <ArtRing col={GRAY_COL} row={6} />
      </g>
    </svg>
  );
}
