/**
 * Card art for `heuristic-search/phase-horizon`: the same board, scored twice.
 * The board on the left is the position now; the clock between them is the
 * drops remaining before the next rise, and as it fills, the board on the
 * right is projected forward to that moment — everything shifts up a row and
 * the seven covered discs waiting underneath arrive. The projected board is
 * the one the evaluator scores, and its tallest column has reached the ceiling.
 *
 * The position is a real one: nothing on it clears where it stands, and
 * nothing clears after the rise either, so the only difference between the two
 * boards is the room the rise took away.
 *
 * Server component. Motion lives in phase-horizon.css (transform and opacity
 * only); the markup is the resting frame, which is why the risen group carries
 * its shift as an attribute.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtGray, ArtRing, BOARD_RIGHT, columnX } from "../board";
import "./phase-horizon.css";

/** A settled mid-game board with one drop of headroom in its tallest column. */
const CELLS = "0000000" + "0008000" + "0805000" + "0402080" + "6304560" + "2243636" + "5462225";

/** The column that reaches the ceiling once the row underneath arrives. */
const TALLEST = 3;
/** The engine's rise clock: five drops to a level. */
const CLOCK = [0, 1, 2, 3, 4];
const PIP = { x: 156, y: 57, size: 8, gap: 14 };
/** The seven covered discs waiting under the floor, one per column. */
const NEW_ROW = [0, 1, 2, 3, 4, 5, 6];

export function PhaseHorizonArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-phase-horizon",
        "The same board twice: as it stands now, and projected forward to the moment the next row rises and takes its headroom",
        props,
      )}
    >
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)" textAnchor="middle">
        <text x={columnX(3)} y={20}>
          now
        </text>
        <text x={columnX(3, BOARD_RIGHT)} y={20}>
          at the rise
        </text>
        <text x={160} y={140} fill="var(--color-ink-3)">
          clock
        </text>
      </g>

      <ArtBoard />
      <ArtCells cells={CELLS} />

      <ArtBoard g={BOARD_RIGHT} />
      <g data-anim="rise" transform={`translate(0 ${-BOARD_RIGHT.cell})`}>
        <ArtCells cells={CELLS} g={BOARD_RIGHT} />
        <g data-anim="arrive">
          {NEW_ROW.map((col) => (
            <ArtGray key={col} col={col} row={BOARD_RIGHT.rows} g={BOARD_RIGHT} />
          ))}
        </g>
      </g>

      <g className="clock">
        {CLOCK.map((step) => (
          <rect
            key={step}
            x={PIP.x}
            y={PIP.y + step * PIP.gap}
            width={PIP.size}
            height={PIP.size}
            rx={1.5}
            fill="var(--color-cell)"
            stroke="var(--color-rule-strong)"
          />
        ))}
        {CLOCK.map((step) => (
          <rect
            key={step}
            data-anim={`pip-${step + 1}`}
            x={PIP.x}
            y={PIP.y + step * PIP.gap}
            width={PIP.size}
            height={PIP.size}
            rx={1.5}
            fill="var(--color-accent)"
          />
        ))}
      </g>

      <g className="tart-final" data-anim="ceiling">
        <path
          d={`M${BOARD_RIGHT.x + TALLEST * BOARD_RIGHT.cell} ${BOARD_RIGHT.y}h${BOARD_RIGHT.cell}`}
          stroke="var(--color-highlight)"
          strokeWidth={2}
        />
        <ArtRing col={TALLEST} row={0} g={BOARD_RIGHT} />
        <text
          x={160}
          y={176}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          the board it will be
        </text>
      </g>
    </svg>
  );
}
