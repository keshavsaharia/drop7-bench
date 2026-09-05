/**
 * Card art for `value-policy-learning/chance-state-nnue`: which node in the
 * tree the value sits on. A disc lands, and the position the network scores is
 * the one immediately after it — the board and the moves left until the row
 * rises, with no input for what comes next. The seven discs the game could
 * deal hang beyond that node and stay faint, because averaging over them is
 * left to the trajectories instead of being learned seven times over.
 *
 * Server component. Motion lives in chance-state-nnue.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, type BoardGeometry } from "../board";
import "./chance-state-nnue.css";

/** A settled corner of a board: nothing on it is standing in a run of its own value. */
const BOARD_G: BoardGeometry = { x: 8, y: 52, cell: 18, cols: 5, rows: 4 };
const CELLS = "00000" + "00000" + "04000" + "56603";

/** The disc that has just been played, and the column it falls into. */
const PLACED = { value: 4, col: 0, row: 2 };

/** The chance state itself: after the move, before the deal. */
const NODE = { x: 150, y: 92, r: 14 };

/** The seven values the game could deal next, none of them an input. */
const DEAL: BoardGeometry = { x: 246, y: 22, cell: 20, cols: 1, rows: 7 };
const DEAL_VALUES = [1, 2, 3, 4, 5, 6, 7];
const DEAL_EDGES = DEAL_VALUES.map(
  (_, index) => `M${NODE.x + NODE.r + 2} ${NODE.y}L${DEAL.x - 3} ${DEAL.y + (index + 0.5) * DEAL.cell}`,
).join("");

/** The placement: board on the left, chance state on the right. */
const PLACE_ARROW = `M104 ${NODE.y}H${NODE.x - NODE.r - 4}M${NODE.x - NODE.r - 9} ${NODE.y - 4}L${NODE.x - NODE.r - 4} ${NODE.y}L${NODE.x - NODE.r - 9} ${NODE.y + 4}`;

export function ChanceStateNnueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-chance-state-nnue",
        "A disc lands and the node that gets valued is the one right after it: the board and the rise clock, with the seven discs that could be dealt left faint beyond it",
        props,
      )}
    >
      <ArtBoard g={BOARD_G} />
      <ArtCells cells={CELLS} g={BOARD_G} />
      <g data-anim="drop">
        <ArtDisc value={PLACED.value} col={PLACED.col} row={PLACED.row} g={BOARD_G} />
      </g>
      <path
        data-anim="arrow"
        d={PLACE_ARROW}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth={1.25}
        strokeLinecap="round"
      />
      <g data-anim="clock">
        <path
          d={`M${NODE.x} ${NODE.y + NODE.r + 10}V${NODE.y + NODE.r + 1}M${NODE.x - 3.5} ${NODE.y + NODE.r + 5}L${NODE.x} ${NODE.y + NODE.r + 1}L${NODE.x + 3.5} ${NODE.y + NODE.r + 5}`}
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth={1}
          strokeLinecap="round"
        />
        <rect x={NODE.x - 22} y={NODE.y + NODE.r + 10} width={44} height={16} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <text
          x={NODE.x}
          y={NODE.y + NODE.r + 18}
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          rise 3
        </text>
      </g>
      <g className="node" data-anim="node">
        <circle
          cx={NODE.x}
          cy={NODE.y}
          r={NODE.r}
          fill="var(--color-accent-soft)"
          stroke="var(--color-accent)"
          strokeWidth={1.6}
        />
        <text
          x={NODE.x}
          y={NODE.y}
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily={ART_MONO}
          fontSize={13}
          fontWeight={700}
          fill="var(--color-accent)"
        >
          U
        </text>
      </g>
      <g data-anim="deal" opacity={0.55}>
        <path d={DEAL_EDGES} fill="none" stroke="var(--color-ink-3)" strokeWidth={0.9} />
        {DEAL_VALUES.map((value, index) => (
          <ArtDisc key={value} value={value} col={0} row={index} g={DEAL} />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize={9} textAnchor="middle">
        <text x={NODE.x} y={NODE.y - 22} fill="var(--color-ink-2)">
          chance state
        </text>
        <text x={DEAL.x + DEAL.cell / 2} y={14} fill="var(--color-ink-3)">
          next disc
        </text>
      </g>
    </svg>
  );
}
