/**
 * Card art for `ntuple-rl/bellman-ntuple`: one stored position, seven updates.
 * On play the visible disc falls into every legal column at once, one shared
 * set of imagined reveals is laid over the seven readings so they are judged
 * under the same luck, the seven values grow together, and the best of them —
 * not the column that happened to be played — is carried back to the board as
 * the update target.
 *
 * Server component. Motion lives in bellman-ntuple.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing } from "../board";
import "./bellman-ntuple.css";

/** The stored position: seven columns of different heights, all seven legal. */
const CELLS =
  "0000000" + "0000000" + "0000000" + "0000010" + "0302060" + "5804723" + "2641356";

/** The visible disc, and the row it comes to rest on in each column. */
const DISC = 4;
const LANDING = [4, 3, 5, 3, 4, 2, 4];

/** What each column's imagined future is worth. The fifth reading is the best. */
const READINGS = [30, 46, 22, 58, 84, 40, 52];
const BAR_X = 158;
const BAR_STEP = 21;
const BAR_W = 14;
const BAR_BASE = 140;
const BEST = 4;
const BEST_X = BAR_X + BEST * BAR_STEP;

export function BellmanNtupleArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-bellman-ntuple",
        "The visible disc dropped into all seven legal columns at once, the seven readings judged under one shared set of imagined reveals, and the best one taken back to the board",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={CELLS} />
      <g className="tries" data-anim="tries">
        {LANDING.map((row, col) => (
          <ArtDisc key={col} value={DISC} col={col} row={row} opacity={0.55} />
        ))}
      </g>
      <ArtRing col={BEST} row={LANDING[BEST]} data-anim="best" />
      <g className="luck" data-anim="luck">
        <text x={BAR_X} y={38} fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          same luck
        </text>
        <path
          d={`M${BAR_X} 46H${BAR_X + 6 * BAR_STEP + BAR_W}`}
          stroke="var(--color-accent)"
          strokeWidth={1.2}
          strokeDasharray="4 3"
          fill="none"
        />
        <path
          d={READINGS.map((_, col) => `M${BAR_X + col * BAR_STEP + BAR_W / 2} 46v4`).join("")}
          stroke="var(--color-accent)"
          strokeWidth={1}
          fill="none"
        />
      </g>
      <g className="bars" data-anim="bars">
        {READINGS.map((value, col) => (
          <rect
            key={col}
            x={BAR_X + col * BAR_STEP}
            y={BAR_BASE - value}
            width={BAR_W}
            height={value}
            rx={2}
            fill="var(--color-ink-4)"
          />
        ))}
      </g>
      <g className="best" data-anim="best">
        <rect
          x={BEST_X}
          y={BAR_BASE - READINGS[BEST]}
          width={BAR_W}
          height={READINGS[BEST]}
          rx={2}
          fill="var(--color-highlight)"
        />
        <text
          x={BEST_X + BAR_W / 2}
          y={152}
          textAnchor="middle"
          fontSize={9}
          fontFamily={ART_MONO}
          fill="var(--color-highlight)"
        >
          max
        </text>
      </g>
      <g className="tart-final" data-anim="target">
        <path
          d="M234 146C210 172 150 172 106 158"
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
        <path d="M106 158l10-3-1 9z" fill="var(--color-highlight)" />
        <text x={60} y={168} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          target
        </text>
      </g>
    </svg>
  );
}
