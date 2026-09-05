/**
 * Card art for `fair-expectimax/phase-fair-combination`: the rise clock as the
 * thing that moves the value. On play the five pips above the board go out one
 * drop at a time, and the residual stacked on top of the fixed evaluator's bar
 * grows with every pip lost; when the last one goes the board rises, a fresh
 * row of covers appears underneath, the clock resets and the residual falls
 * back to where it started.
 *
 * Server component. Motion lives in phase-fair-combination.css (opacity and
 * transform only); the markup is the resting frame, the moment just after a
 * rise.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtGray, BOARD, boardWidth } from "../board";
import "./phase-fair-combination.css";

/**
 * The board as it stands after the rise, so the bottom row is free for the new
 * covers. During play the discs sit one row lower and shift up into this.
 */
const CELLS =
  "0000000" + "0000000" + "0006000" + "0500006" + "0167402" + "3611642" + "0000000";

/** Five drops to the next rise, drawn across the width of the board. */
const PIPS = [0, 1, 2, 3, 4];
const PIP_W = 20;
const PIP_STEP = 25.5;
const PIP_X = 17;

/** The seven columns the new row of covers arrives under. */
const COLUMNS = [0, 1, 2, 3, 4, 5, 6];

/** The value bar: a fixed base with the phase residual stacked on top of it. */
const BAR = { x: 188, w: 30, top: 48, base: 100, floor: 150 };

export function PhaseFairCombinationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-phase-fair-combination",
        "The five drops before a row rise counting down, with a phase residual stacked on the fixed evaluator's value and growing as the rise nears",
        props,
      )}
    >
      <g className="clock">
        {PIPS.map((index) => (
          <rect
            key={index}
            x={PIP_X + index * PIP_STEP}
            y={8}
            width={PIP_W}
            height={9}
            rx={2}
            fill="var(--color-cell)"
            stroke="var(--color-rule)"
          />
        ))}
        {PIPS.map((index) => (
          <rect
            key={index}
            data-anim={`pip-${index + 1}`}
            x={PIP_X + index * PIP_STEP}
            y={8}
            width={PIP_W}
            height={9}
            rx={2}
            fill="var(--color-accent)"
          />
        ))}
      </g>
      <ArtBoard />
      <g className="stack" data-anim="rise">
        <ArtCells cells={CELLS} />
      </g>
      <rect
        x={BAR.x}
        y={BAR.top}
        width={BAR.w}
        height={BAR.floor - BAR.top}
        rx={3}
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
      />
      <rect
        x={BAR.x}
        y={BAR.base}
        width={BAR.w}
        height={BAR.floor - BAR.base}
        rx={2}
        fill="var(--color-ink-4)"
      />
      <rect
        data-anim="residual"
        x={BAR.x}
        y={BAR.base - 14}
        width={BAR.w}
        height={14}
        rx={2}
        fill="var(--color-accent-strong)"
      />
      <g fontSize={9} fontFamily={ART_MONO}>
        <text x={226} y={128} fill="var(--color-ink-2)">
          base
        </text>
        <text x={226} y={96} fill="var(--color-accent)">
          phase
        </text>
      </g>
      <g className="tart-final" data-anim="rise-row">
        {COLUMNS.map((col) => (
          <ArtGray key={col} col={col} row={6} />
        ))}
        <text
          x={BOARD.x + boardWidth() / 2}
          y={168}
          textAnchor="middle"
          fontSize={9}
          fontFamily={ART_MONO}
          fill="var(--color-ink-2)"
        >
          rise
        </text>
      </g>
    </svg>
  );
}
