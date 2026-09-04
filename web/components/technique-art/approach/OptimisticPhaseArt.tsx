/**
 * Card art for `ntuple-rl/optimistic-phase`: a look-ahead measured in rises
 * rather than in moves. The search fans out to all seven legal columns, keeps
 * the best two across the first row rise and one across the second, and reads
 * its value there. On play each rise pushes a fresh row of covers under the
 * board as the branch crosses its gate, so the horizon and the board's own
 * deadline move together.
 *
 * Server component. Motion lives in optimistic-phase.css (transform, opacity
 * and dash offsets); the markup is the resting frame, after the second rise.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtGray } from "../board";
import "./optimistic-phase.css";

/**
 * The stack as it stands after both rises, so the bottom two rows are free for
 * the two rows of covers. During play it sits lower and is pushed up into this.
 */
const CELLS =
  "0000000" + "0000000" + "0600200" + "4302501" + "1736425" + "0000000" + "0000000";

const COLUMNS = [0, 1, 2, 3, 4, 5, 6];

/** The root fans to every legal column; the branch ends are its seven leaves. */
const ROOT_X = 150;
const ROOT_Y = 89;
const FAN_X = 192;
const FAN_Y = [34, 53, 72, 90, 109, 128, 146];

/** The two rise boundaries the search deepens across. */
const GATES = [200, 262];

export function OptimisticPhaseArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-optimistic-phase",
        "A search fanning out to all seven columns, carrying the best two across one row rise and one across a second, while two rows of covers arrive under the board",
        props,
      )}
    >
      <ArtBoard />
      <g className="stack" data-anim="stack">
        <ArtCells cells={CELLS} />
      </g>
      <g className="cover-1" data-anim="cover-1">
        {COLUMNS.map((col) => (
          <ArtGray key={col} col={col} row={5} />
        ))}
      </g>
      <g className="cover-2" data-anim="cover-2">
        {COLUMNS.map((col) => (
          <ArtGray key={col} col={col} row={6} />
        ))}
      </g>
      <g className="gates" fontSize={9} fontFamily={ART_MONO} textAnchor="middle">
        {GATES.map((x, index) => (
          <g key={x} data-anim={`gate-${index + 1}`}>
            <path d={`M${x} 24V156`} stroke="var(--color-ink-3)" strokeWidth={1.2} strokeDasharray="3 4" fill="none" />
            <text x={x} y={18} fill="var(--color-ink-2)">
              rise
            </text>
          </g>
        ))}
      </g>
      <path
        className="fan"
        data-anim="fan"
        d={FAN_Y.map((y) => `M${ROOT_X} ${ROOT_Y}L${FAN_X} ${y}`).join("")}
        stroke="var(--color-ink-4)"
        strokeWidth={1.2}
        strokeDasharray={72}
        fill="none"
      />
      <path
        className="pair"
        data-anim="pair"
        d={`M${FAN_X} ${FAN_Y[1]}L256 66M${FAN_X} ${FAN_Y[4]}L256 96`}
        stroke="var(--color-accent)"
        strokeWidth={1.8}
        strokeDasharray={70}
        fill="none"
      />
      <path
        className="deep"
        data-anim="deep"
        d="M256 66L300 84"
        stroke="var(--color-highlight)"
        strokeWidth={2.2}
        strokeDasharray={52}
        fill="none"
      />
      <g className="tart-final" data-anim="value">
        <path d="M300 77l7 7-7 7-7-7z" fill="var(--color-highlight)" />
      </g>
    </svg>
  );
}
