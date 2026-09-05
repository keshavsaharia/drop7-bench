/**
 * Card art for `ntuple-rl/rainbow-q`: the Rainbow bundle drawn as a stack. A
 * four-cell pattern window over a row of discs is the base — the hashed
 * n-tuple table that stands in for a deep network — and three named
 * refinements settle onto it one at a time. On play `double`, `5-step` and
 * `replay` slide in from alternating sides and land on the stack, and the
 * seven column values above sharpen a step with each one, until a ring marks
 * the column the maximum picks.
 *
 * Server component. Motion lives in rainbow-q.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, columnX, type BoardGeometry } from "../board";
import "./rainbow-q.css";

/** One row of board cells: the pattern the table is indexed by. */
const STRIP: BoardGeometry = { x: 76, y: 134, cell: 24, cols: 7, rows: 1 };
const PATTERN = [3, 5, 2, 4, 6, 2, 5];
/** The four-cell n-tuple window, given as a first column and a span. */
const WINDOW_FROM = 1;
const WINDOW_SPAN = 4;

/** One value bar per column, sitting directly over the column it scores. */
const BASELINE = 50;
const BAR_W = 14;
const BAR_H = [14, 22, 10, 26, 18, 30, 12];
const BEST = 5;

/**
 * The three Rainbow ingredients ported here, bottom of the stack upwards.
 * Each slides in from the side named by its `from`.
 */
const LAYERS = [
  { key: "double", label: "double", y: 108, colour: "var(--color-series-7)" },
  { key: "steps", label: "5-step", y: 82, colour: "var(--color-series-3)" },
  { key: "replay", label: "replay", y: 56, colour: "var(--color-series-2)" },
];
const LAYER_X = 84;
const LAYER_W = 152;
const LAYER_H = 22;

export function RainbowQArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-rainbow-q",
        "A four-cell pattern window carrying a stack of three named Q-learning refinements, with one value bar per column above it and a ring on the best",
        props,
      )}
    >
      <g className="values">
        <text x={68} y={BASELINE} textAnchor="end" fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-3)">
          Q
        </text>
        <line
          x1={STRIP.x}
          y1={BASELINE + 0.5}
          x2={STRIP.x + STRIP.cols * STRIP.cell}
          y2={BASELINE + 0.5}
          stroke="var(--color-rule-strong)"
        />
        {BAR_H.map((h, index) => (
          <rect
            key={index}
            className="bar"
            data-anim="bar"
            x={columnX(index, STRIP) - BAR_W / 2}
            y={BASELINE - h}
            width={BAR_W}
            height={h}
            rx={2}
            fill="var(--color-accent-strong)"
          />
        ))}
      </g>

      <g className="stack">
        {LAYERS.map((layer) => (
          <g key={layer.key} data-anim={`layer-${layer.key}`}>
            <rect
              x={LAYER_X}
              y={layer.y}
              width={LAYER_W}
              height={LAYER_H}
              rx={4}
              fill={layer.colour}
              fillOpacity={0.18}
              stroke={layer.colour}
              strokeWidth={1.3}
            />
            <circle cx={LAYER_X + 14} cy={layer.y + LAYER_H / 2} r={4} fill={layer.colour} />
            <text
              x={LAYER_X + 26}
              y={layer.y + LAYER_H / 2}
              dominantBaseline="central"
              fontSize={10}
              fontFamily={ART_MONO}
              fill="var(--color-ink-2)"
            >
              {layer.label}
            </text>
          </g>
        ))}
      </g>

      <g className="patterns">
        <ArtBoard g={STRIP} />
        {PATTERN.map((value, index) => (
          <ArtDisc key={index} value={value} col={index} row={0} g={STRIP} />
        ))}
        <rect
          x={STRIP.x + WINDOW_FROM * STRIP.cell}
          y={STRIP.y}
          width={WINDOW_SPAN * STRIP.cell}
          height={STRIP.cell}
          rx={2}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.6}
        />
      </g>

      <g className="tart-final" data-anim="pick">
        <rect
          x={columnX(BEST, STRIP) - BAR_W / 2 - 2}
          y={BASELINE - BAR_H[BEST] - 4}
          width={BAR_W + 4}
          height={BAR_H[BEST] + 8}
          rx={2}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
      </g>
    </svg>
  );
}
