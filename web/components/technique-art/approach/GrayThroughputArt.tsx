/**
 * Card art for `heuristic-search/gray-throughput`: the board as a tank with
 * an inflow and an outflow. Numbered discs and covered gray discs arrive on
 * the left; cleared numbers and cracked covers leave on the right. On play
 * the outflow runs for a moment and then stops, the outlet closes, and the
 * level rises until it reaches the top rule — which is what the replacement
 * rate under the drawing says has to be met and is not.
 *
 * Server component. Motion lives in gray-throughput.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, ArtGray, type BoardGeometry } from "../board";
import "./gray-throughput.css";

/** The board, drawn small enough to leave an inflow and an outflow beside it. */
const TANK: BoardGeometry = { x: 114, y: 40, cell: 14, cols: 7, rows: 7 };
const TANK_W = TANK.cols * TANK.cell;
const TANK_H = TANK.rows * TANK.cell;

/** Two lanes in, two lanes out; only the disc centres matter. */
const IN_DISCS: BoardGeometry = { x: 6, y: 50, cell: 24, cols: 4, rows: 1 };
const IN_COVERS: BoardGeometry = { x: 6, y: 86, cell: 24, cols: 4, rows: 1 };
const OUT_CLEARS: BoardGeometry = { x: 242, y: 50, cell: 24, cols: 3, rows: 1 };
const OUT_REVEALS: BoardGeometry = { x: 242, y: 86, cell: 24, cols: 3, rows: 1 };

const ARRIVING = [5, 2, 6];

export function GrayThroughputArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-gray-throughput",
        "Discs and covered gray discs flowing into the board faster than clears and reveals take them out, until the board fills",
        props,
      )}
    >
      <ArtBoard g={TANK} />
      <rect
        data-anim="level"
        x={TANK.x + 1}
        y={TANK.y + 1}
        width={TANK_W - 2}
        height={TANK_H - 2}
        rx={3}
        fill="var(--color-disc-gray)"
        opacity={0.45}
      />
      <rect
        data-anim="top"
        x={TANK.x}
        y={TANK.y - 3}
        width={TANK_W}
        height={3}
        rx={1.5}
        fill="var(--color-danger)"
      />

      <g data-anim="flow-in">
        {ARRIVING.map((value, i) => (
          <ArtDisc key={value} value={value} col={i + 1} row={0} g={IN_DISCS} />
        ))}
        <ArtGray col={1} row={0} g={IN_COVERS} />
        <ArtGray col={2} row={0} g={IN_COVERS} />
        <ArtGray col={3} row={0} g={IN_COVERS} />
      </g>

      <path
        d="M100,57L108,62L100,67zM100,93L108,98L100,103z"
        fill="var(--color-ink-3)"
      />
      <path
        d="M216,57L224,62L216,67zM216,93L224,98L216,103z"
        fill="var(--color-ink-3)"
      />

      <g data-anim="flow-out" opacity={0}>
        <ArtDisc value={4} col={0} row={0} g={OUT_CLEARS} />
        <ArtDisc value={1} col={1} row={0} g={OUT_CLEARS} />
        <ArtGray cracked col={0} row={0} g={OUT_REVEALS} />
        <ArtGray cracked col={1} row={0} g={OUT_REVEALS} />
      </g>

      <rect data-anim="stop" x={230} y={44} width={5} height={72} rx={2.5} fill="var(--color-danger)" />

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={10} y={84}>
          in
        </text>
        <text x={292} y={84}>
          out
        </text>
        <text x={160} y={158} textAnchor="middle">
          2.4 clears + 1.4 reveals per move
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={174} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the board fills anyway
        </text>
      </g>
    </svg>
  );
}
