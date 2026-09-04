/**
 * Card art for `ntuple-rl/native-ppo`: the apparatus, not the learning. The
 * exact one-move search puts the next disc in every one of the seven columns
 * and scores each placement, and the copy that is being trained carries that
 * answer to the gate that guards the next stage. On play the seven candidate
 * placements and their scores appear column by column, the best one lands and
 * is ringed, and the copy pushes at the gate, which does not open.
 *
 * The board is a legal Drop7 position with nothing pending; a 3 in column 3 is
 * the placement that clears, checked against the engine's run rule.
 *
 * Server component. Motion lives in native-ppo.css (transform and opacity
 * only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing, cellCenter, columnX, discRadius } from "../board";
import "./native-ppo.css";

const NATIVE = { x: 16, y: 54, cell: 15, cols: 7, rows: 4 };
/** Heights 2, 3, 1, 3, 2, 3, 1 within the crop; nothing is pending a clear. */
const CELLS = "0000000" + "0506040" + "3505760" + "6426554";
/** The next disc. */
const NEXT = 3;
/** Where that disc comes to rest in each column. */
const LANDING = [1, 0, 2, 0, 1, 0, 2];
/** How tall each placement's score stands; the clearing one is the tallest. */
const SCORES = [10, 16, 28, 14, 12, 20, 8];
/** The clearing placement, and the search's answer. */
const PICK = 2;

const TICK_TOP = NATIVE.y + NATIVE.rows * NATIVE.cell + 6;
const BOARD_MID = NATIVE.x + (NATIVE.cols * NATIVE.cell) / 2;

/** The copy being trained: three inputs, two hidden units, one answer. */
const NET_IN = [78, 92, 106];
const NET_HID = [85, 99];
const NET_EDGES = NET_IN.flatMap((y) => NET_HID.map((y2) => `M184,${y}L204,${y2}`))
  .concat(NET_HID.map((y) => `M204,${y}L224,92`))
  .join("");

/** One candidate placement: the next disc, outlined where it would come to rest. */
function Candidate({ col }: { col: number }) {
  const [cx, cy] = cellCenter(col, LANDING[col], NATIVE);
  const r = discRadius(NATIVE);
  return (
    <>
      <circle
        cx={cx}
        cy={cy}
        r={r}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={1.1}
        strokeDasharray="3 2.4"
      />
      <text
        x={cx}
        y={cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={r * 1.2}
        fontFamily={ART_MONO}
        fill="var(--color-accent)"
      >
        {NEXT}
      </text>
    </>
  );
}

export function NativePpoArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-native-ppo",
        "The one-move search places the next disc in all seven columns and scores each, and the copy of it stops at a gate that stays shut",
        props,
      )}
    >
      <circle cx={BOARD_MID} cy={38} r={7} fill={`var(--color-disc-${NEXT})`} />
      <text
        x={BOARD_MID}
        y={38}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={9}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${NEXT}-fg)`}
      >
        {NEXT}
      </text>

      <ArtBoard g={NATIVE}>
        <ArtCells cells={CELLS} g={NATIVE} />
      </ArtBoard>

      {SCORES.map((score, col) => (
        <g key={col} data-anim={`col-${col + 1}`}>
          <Candidate col={col} />
          <rect
            x={columnX(col, NATIVE) - 3.5}
            y={TICK_TOP}
            width={7}
            height={score}
            rx={1.5}
            fill={col === PICK ? "var(--color-accent)" : "var(--color-accent-strong)"}
          />
        </g>
      ))}

      <g data-anim="pick">
        <ArtDisc value={NEXT} col={PICK} row={LANDING[PICK]} g={NATIVE} />
        <ArtRing col={PICK} row={LANDING[PICK]} g={NATIVE} />
      </g>

      <path d={NET_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth={0.8} />
      <rect
        x={170}
        y={66}
        width={68}
        height={52}
        rx={4}
        fill="none"
        stroke="var(--color-rule-strong)"
      />
      <g fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth={1.1}>
        {NET_IN.map((y) => (
          <circle key={`i${y}`} cx={184} cy={y} r={3.4} />
        ))}
        {NET_HID.map((y) => (
          <circle key={`h${y}`} cx={204} cy={y} r={3.4} />
        ))}
        <circle cx={224} cy={92} r={4.4} />
      </g>

      <g stroke="var(--color-ink-2)" strokeWidth={1.4}>
        <rect x={262} y={62} width={44} height={60} rx={3} fill="var(--color-raised)" />
        <path
          d="M266,78h36M266,92h36M266,106h36"
          stroke="var(--color-rule-strong)"
          strokeWidth={1}
          fill="none"
        />
      </g>

      <circle data-anim="carry" cx={250} cy={92} r={5} fill="var(--color-accent)" />

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={BOARD_MID} y={160} textAnchor="middle">
          one move
        </text>
        <text x={204} y={136} textAnchor="middle">
          copy
        </text>
        <text x={284} y={136} textAnchor="middle">
          gate
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={174} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the copy never opened the gate
        </text>
      </g>
    </svg>
  );
}
