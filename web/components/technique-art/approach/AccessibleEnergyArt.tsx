/**
 * Card art for `oracle-curriculum/accessible-energy`: how much clearable value
 * a board is still holding. A real position is drawn once, dim; a sweep runs
 * down it and brightens, band by band, the discs that could still be made to
 * pop given what sits on top of them, floating the game's own wave scores off
 * two of them. The discs buried under three or more stay dark, and the meter
 * beside the board counts only what the sweep reached: the seven at the top of
 * the short column is live, the identical seven entombed in the tall column is
 * not.
 *
 * Server component. Motion lives in accessible-energy.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing, ArtScore, BOARD, boardWidth } from "../board";
import "./accessible-energy.css";

/**
 * A settled position, row-major from the top. No disc sits in a run equal to
 * its own value, so nothing here is mid-clear. Column six is empty, which is
 * what keeps the two sevens (column three row five, column five row five) off
 * a full row of seven and therefore still on the board.
 */
const POSITION =
  "0000000" + "0053000" + "0621000" + "0772200" + "4234100" + "1817970" + "5425340";

/**
 * The same position split into the discs the sweep can still reach — at most
 * one disc covering them — in three bands, top to bottom. Everything absent
 * from all three is entombed and stays at the dim layer's opacity.
 */
const REACHABLE_TOP =
  "0000000" + "0053000" + "0621000" + "0000000" + "0000000" + "0000000" + "0000000";
const REACHABLE_MID =
  "0000000" + "0000000" + "0000000" + "0700200" + "4000100" + "0000000" + "0000000";
const REACHABLE_LOW =
  "0000000" + "0000000" + "0000000" + "0000000" + "0000000" + "1000070" + "0000040";

/** How far a score label ends up above the disc it came off. */
const FLOAT = 24;
const PANEL_X = 152;
const METER_W = 152;

export function AccessibleEnergyArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-accessible-energy",
        "A Drop7 board swept from the top: the discs that can still be made to pop light up with the score they are holding, and the buried ones stay dark",
        props,
      )}
    >
      <ArtBoard />
      <g className="entombed" opacity={0.3}>
        <ArtCells cells={POSITION} />
      </g>
      <g className="reachable-top" data-anim="band-a">
        <ArtCells cells={REACHABLE_TOP} />
      </g>
      <g className="reachable-mid" data-anim="band-b">
        <ArtCells cells={REACHABLE_MID} />
      </g>
      <g className="reachable-low" data-anim="band-c">
        <ArtCells cells={REACHABLE_LOW} />
      </g>
      <line
        data-anim="sweep"
        x1={BOARD.x + 1}
        y1={BOARD.y}
        x2={BOARD.x + boardWidth() - 1}
        y2={BOARD.y}
        stroke="var(--color-accent)"
        strokeWidth={1.5}
        opacity={0}
      />
      <g transform={`translate(0 ${-FLOAT})`}>
        <ArtScore depth={1} col={0} row={4} data-anim="score-a" />
        <ArtScore depth={2} col={4} row={3} data-anim="score-b" />
      </g>
      <g className="legend" fontFamily={ART_MONO}>
        <circle cx={PANEL_X + 9} cy={50} r={5.5} fill="var(--color-ink-1)" />
        <text x={PANEL_X + 22} y={54} fontSize={10} fill="var(--color-ink-1)">
          reachable
        </text>
        <circle cx={PANEL_X + 9} cy={74} r={5.5} fill="var(--color-ink-4)" />
        <text x={PANEL_X + 22} y={78} fontSize={10} fill="var(--color-ink-3)">
          entombed
        </text>
        <text x={PANEL_X} y={116} fontSize={10} fill="var(--color-ink-2)">
          energy
        </text>
      </g>
      <rect
        x={PANEL_X}
        y={124}
        width={METER_W}
        height={10}
        rx={3}
        fill="var(--color-raised)"
        stroke="var(--color-rule)"
      />
      <rect
        data-anim="meter"
        x={PANEL_X + 1.5}
        y={125.5}
        width={108}
        height={7}
        rx={2.5}
        fill="var(--color-highlight)"
      />
      <ArtRing className="tart-final" data-anim="ring" col={5} row={5} />
    </svg>
  );
}
