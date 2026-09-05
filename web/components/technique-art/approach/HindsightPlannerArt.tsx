/**
 * Card art for `oracle-curriculum/hindsight-planner`: the same five moments of
 * the game, twice. On top is what anyone playing can know — the next disc, and
 * then nothing. Below is the tape this planner invents for itself, drawn with
 * a broken border because it is imagined: it writes every disc down, and then
 * plans straight through the whole of it as though it were certain.
 *
 * The first disc of the invented tape is the real visible one; everything
 * after it is the planner's own. The hindsight is that it is never covered.
 *
 * Server component. Motion lives in hindsight-planner.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, cellCenter, discRadius, type BoardGeometry } from "../board";
import "./hindsight-planner.css";

/** Five moments of the game, as a one-row tape. */
const GAME: BoardGeometry = { x: 76, y: 30, cell: 36, cols: 5, rows: 1 };
const TAPE: BoardGeometry = { ...GAME, y: 88 };
const SLOTS = [0, 1, 2, 3, 4];

/** The visible next disc, which both rows legitimately know. */
const NEXT_DISC = 6;
/** The rest of the invented tape: the planner's own guesses, written as facts. */
const INVENTED = [2, 5, 3, 7];

/** One slot of the game the player cannot see into. */
function Covered({ col, g }: { col: number; g: BoardGeometry }) {
  const [cx, cy] = cellCenter(col, 0, g);
  const r = discRadius(g);
  return (
    <g>
      <circle
        cx={cx}
        cy={cy}
        r={r}
        fill="var(--color-disc-gray-core)"
        stroke="var(--color-disc-gray)"
        strokeWidth={r * 0.28}
      />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={r * 1.1}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill="var(--color-ink-2)"
      >
        ?
      </text>
    </g>
  );
}

export function HindsightPlannerArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-hindsight-planner",
        "The game's next five discs, covered for a player, and the planner's own invented tape below with every one of them written down and planned through",
        props,
      )}
    >
      <ArtBoard g={GAME} />
      <ArtDisc value={NEXT_DISC} col={0} row={0} g={GAME} />
      {SLOTS.slice(1).map((col) => (
        <Covered key={col} col={col} g={GAME} />
      ))}

      <g data-anim="invent">
        <rect
          x={TAPE.x - 4}
          y={TAPE.y - 4}
          width={TAPE.cols * TAPE.cell + 8}
          height={TAPE.cell + 8}
          rx={5}
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth={1.2}
          strokeDasharray="3 2.5"
        />
        <ArtBoard g={TAPE} />
        <ArtDisc value={NEXT_DISC} col={0} row={0} g={TAPE} />
      </g>
      {INVENTED.map((value, index) => (
        <g key={value} data-anim={`written-${index + 1}`}>
          <ArtDisc value={value} col={index + 1} row={0} g={TAPE} />
        </g>
      ))}

      <path
        d={SLOTS.map((col) => `M${cellCenter(col, 0, TAPE)[0]} 130v7`).join("")}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        fill="none"
      />
      <rect
        data-anim="plan"
        x={cellCenter(0, 0, TAPE)[0]}
        y={137}
        width={152}
        height={2.6}
        rx={1.3}
        fill="var(--color-accent)"
      />

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={10} y={51}>
          the game
        </text>
        <text x={10} y={109}>
          its tape
        </text>
        <text x={262} y={142}>
          depth 8
        </text>
      </g>

      <g className="tart-final" data-anim="finish">
        <path
          d="M246 136.3l8 2-8 2z"
          fill="var(--color-accent)"
        />
        <text
          x={160}
          y={166}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          it plans inside a future it knows
        </text>
      </g>
    </svg>
  );
}
