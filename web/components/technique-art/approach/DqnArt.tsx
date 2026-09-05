/**
 * Card art for `value-policy-learning/dqn`: the "double" in double-DQN. Two
 * copies of the same small network sit side by side, the live one on the left
 * and the lagging copy on the right. On play the online net lights and hands
 * its chosen column across as a disc in a cell; the target net lights in turn
 * and returns a number down to the bar below. The dashed outline is where the
 * online net's own estimate of its own choice stood, and the solid bar settles
 * under it: the second opinion is the one that counts.
 *
 * Server component. Motion lives in dqn.css (transform and opacity only); the
 * markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, type BoardGeometry } from "../board";
import "./dqn.css";

const NET = { y: 30, w: 84, h: 72 };
const ONLINE_X = 14;
const TARGET_X = 222;
/** Layer x offsets and the nodes in each: three in, two hidden, one value out. */
const LAYERS: { dx: number; nodes: number[] }[] = [
  { dx: 22, nodes: [48, 66, 84] },
  { dx: 48, nodes: [57, 75] },
  { dx: 74, nodes: [66] },
];

/** Every edge between one layer and the next, as a single path. */
const EDGES = LAYERS.slice(0, -1)
  .flatMap((layer, index) =>
    layer.nodes.flatMap((from) =>
      LAYERS[index + 1].nodes.map((to) => `M${layer.dx} ${from}L${LAYERS[index + 1].dx} ${to}`),
    ),
  )
  .join("");

/** The chosen move, drawn as one cell with the disc that is about to fall in it. */
const CHIP: BoardGeometry = { x: -9, y: -9, cell: 18, cols: 1, rows: 1 };
const CHIP_REST = 200;

const BAR_X = 148;
const BAR_W = 26;
const BAR_FLOOR = 156;
/** The honest value, and the height the online net's own estimate stood at. */
const BAR_TOP = 124;
const GHOST_TOP = 102;

function Net({ x, colour, anim }: { x: number; colour: string; anim: string }) {
  return (
    <g transform={`translate(${x} 0)`}>
      <rect
        x={0}
        y={NET.y}
        width={NET.w}
        height={NET.h}
        rx={6}
        fill="var(--color-raised)"
        stroke="var(--color-rule-strong)"
      />
      <g data-anim={anim}>
        <path d={EDGES} fill="none" stroke={colour} strokeWidth={1} opacity={0.5} />
        {LAYERS.flatMap((layer) =>
          layer.nodes.map((cy) => <circle key={`${layer.dx}-${cy}`} cx={layer.dx} cy={cy} r={3.4} fill={colour} />),
        )}
      </g>
    </g>
  );
}

export function DqnArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-dqn",
        "Two copies of one network side by side: the live one hands its chosen column across, and the lagging copy returns a value that settles below the first network's own estimate",
        props,
      )}
    >
      <g fontFamily={ART_MONO} fontSize={10} fill="var(--color-ink-2)" textAnchor="middle">
        <text x={ONLINE_X + NET.w / 2} y={20}>
          online
        </text>
        <text x={TARGET_X + NET.w / 2} y={20}>
          target
        </text>
      </g>

      <Net x={ONLINE_X} colour="var(--color-accent)" anim="online" />
      <Net x={TARGET_X} colour="var(--color-accent-strong)" anim="target" />

      <g className="hand-over" fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.2} strokeLinecap="round">
        <path data-anim="across" d="M104 66H184M178 61l6 5l-6 5" />
      </g>
      <g transform={`translate(${CHIP_REST} 66)`}>
        <g data-anim="chip">
          <ArtBoard g={CHIP} />
          <ArtDisc value={4} col={0} row={0} g={CHIP} />
        </g>
      </g>

      <g className="answer" fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.2} strokeLinecap="round">
        <path data-anim="back" d="M264 106v16H180M186 117l-6 5l6 5" />
      </g>

      <g className="value">
        <line x1={BAR_X - 6} y1={BAR_FLOOR + 0.5} x2={BAR_X + BAR_W + 6} y2={BAR_FLOOR + 0.5} stroke="var(--color-rule-strong)" />
        <rect
          data-anim="ghost"
          x={BAR_X}
          y={GHOST_TOP}
          width={BAR_W}
          height={BAR_FLOOR - GHOST_TOP}
          rx={2}
          fill="none"
          stroke="var(--color-ink-4)"
          strokeWidth={1.2}
          strokeDasharray="3 3"
        />
        <rect
          className="bar"
          data-anim="bar"
          x={BAR_X}
          y={BAR_TOP}
          width={BAR_W}
          height={BAR_FLOOR - BAR_TOP}
          rx={2}
          fill="var(--color-accent-strong)"
        />
      </g>

      <g className="caption-a" data-anim="caption-a" opacity={0}>
        <text x={160} y={174} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          its own pick, its own price
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x={160} y={174} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          one net picks, the other prices
        </text>
      </g>
    </svg>
  );
}
