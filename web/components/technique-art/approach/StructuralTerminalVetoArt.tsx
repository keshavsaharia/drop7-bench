/**
 * Card art for `constructive-reservoir/structural-terminal-veto`: the dead end
 * is the subject. One choice, two boards. On play the board the four-move
 * search picked runs on to the end of the game and packs solid — no legal move
 * left, whatever it scored on the way — and takes a cross, while the runner-up's
 * board still has room above every column. Both panels of imagined futures say
 * the same thing, so the choice crosses to the runner-up.
 *
 * Server component. Motion lives in structural-terminal-veto.css (opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, BOARD, BOARD_RIGHT, boardHeight, boardWidth } from "../board";
import "./structural-terminal-veto.css";

/** The search's pick, part way through its terminal simulation. */
const PICKED = "0000000" + "0006000" + "0502070" + "3184652" + "6247318" + "5732164" + "2415873";
/** What the rest of that simulation adds: every remaining cell, to the ceiling. */
const PACKED = "4172635" + "5720816" + "3060104" + "0000000" + "0000000" + "0000000" + "0000000";
/** The runner-up's board, still open above every column. */
const RUNNER_UP = "0000000" + "0000000" + "0000000" + "0000000" + "0400500" + "2603170" + "5182463";

/** Each branch is judged twice, on two independent panels of imagined futures. */
const PANEL_OFFSET = 1.6;

function branch(toX: number, offset: number): string {
  return `M160,${(16.8 + offset).toFixed(1)}L${toX},${(23.8 + offset).toFixed(1)}`;
}

const LEFT_X = BOARD.x + boardWidth() / 2;
const RIGHT_X = BOARD_RIGHT.x + boardWidth(BOARD_RIGHT) / 2;
const EDGES = [
  branch(LEFT_X, -PANEL_OFFSET),
  branch(LEFT_X, PANEL_OFFSET),
  branch(RIGHT_X, -PANEL_OFFSET),
  branch(RIGHT_X, PANEL_OFFSET),
].join("");

export function StructuralTerminalVetoArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-structural-terminal-veto",
        "Two boards from one choice: the search's pick packs solid with no move left, the runner-up's board still has room",
        props,
      )}
    >
      <path d={EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth="1.1" />
      <path
        className="kept"
        data-anim="kept"
        d={`${branch(LEFT_X, -PANEL_OFFSET)}${branch(LEFT_X, PANEL_OFFSET)}`}
        fill="none"
        stroke="var(--color-ink-2)"
        strokeWidth="2"
        opacity="0"
      />
      <path
        className="vetoed"
        data-anim="vetoed"
        d={`${branch(RIGHT_X, -PANEL_OFFSET)}${branch(RIGHT_X, PANEL_OFFSET)}`}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth="2"
      />
      <rect x="154" y="6" width="12" height="12" rx="3" fill="var(--color-raised)" stroke="var(--color-ink-2)" strokeWidth="1.2" />

      <g className="doomed" data-anim="doomed" opacity="0.4">
        <ArtBoard />
        <ArtCells cells={PICKED} />
        <g data-anim="packs">
          <ArtCells cells={PACKED} />
        </g>
      </g>
      <path
        className="dead"
        data-anim="dead"
        d={`M${BOARD.x + 6},${BOARD.y + 6}l${boardWidth() - 12},${boardHeight() - 12}M${BOARD.x + boardWidth() - 6},${BOARD.y + 6}l${12 - boardWidth()},${boardHeight() - 12}`}
        fill="none"
        stroke="var(--color-ink-2)"
        strokeWidth="2.6"
        strokeLinecap="round"
      />

      <ArtBoard g={BOARD_RIGHT} />
      <ArtCells cells={RUNNER_UP} g={BOARD_RIGHT} />
      <rect
        className="chosen"
        data-anim="chosen"
        x={BOARD_RIGHT.x - 3}
        y={BOARD_RIGHT.y - 3}
        width={boardWidth(BOARD_RIGHT) + 6}
        height={boardHeight(BOARD_RIGHT) + 6}
        rx="6"
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth="1.8"
      />

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={BOARD.x} y="17">
          search&rsquo;s pick
        </text>
        <text x={BOARD_RIGHT.x + boardWidth(BOARD_RIGHT)} y="17" textAnchor="end">
          runner-up
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          both panels agree: the dead end is refused
        </text>
      </g>
    </svg>
  );
}
