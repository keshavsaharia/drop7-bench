/**
 * Policy gradient card art. Seven bars, one per column, that sum to one.
 * On play a sampled bar pulses, a plus badge appears above it and it grows
 * while the others shrink to keep the total; then a minus badge appears
 * over another bar, which shrinks while the rest grow back. At rest the
 * seven re-normalised bars stand with both badges.
 */
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./policy-gradient.css";

/** Final heights after both updates, in viewBox units (probability x 250). */
const HEIGHTS = [24, 15, 33, 94, 38, 28, 19];
const PLUS_INDEX = 3;
const MINUS_INDEX = 1;
const FLOOR = 146;
const BAR_W = 26;
const STEP = 38;
const LEFT = 36;

function barAnim(index: number): string {
  if (index === PLUS_INDEX) return "bar-plus";
  if (index === MINUS_INDEX) return "bar-minus";
  return "bar-rest";
}

function Badge({ index, sign, fill, ink }: { index: number; sign: string; fill: string; ink: string }) {
  const cx = LEFT + index * STEP + BAR_W / 2;
  return (
    <g data-anim={sign === "+" ? "plus" : "minus"}>
      <circle cx={cx} cy={34} r={9} fill={fill} />
      <text
        x={cx}
        y={34}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={12}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={ink}
      >
        {sign}
      </text>
    </g>
  );
}

export function PolicyGradientArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "policy-gradient",
        "Seven probability bars, one per column; a sampled bar grows after a plus and another shrinks after a minus, and the rest re-normalise",
        props,
      )}
    >
      <text x={294} y={24} textAnchor="end" fontSize={10} fontFamily={ART_MONO} fill="var(--color-ink-3)">
        p(column)
      </text>
      <g className="bars">
        {HEIGHTS.map((h, i) => (
          <rect
            key={i}
            data-anim={barAnim(i)}
            className="bar"
            x={LEFT + i * STEP}
            y={FLOOR - h}
            width={BAR_W}
            height={h}
            rx={2}
            fill="var(--color-accent-strong)"
          />
        ))}
      </g>
      <line x1={32} y1={FLOOR + 0.5} x2={294} y2={FLOOR + 0.5} stroke="var(--color-rule-strong)" />
      <g className="columns" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-3)" textAnchor="middle">
        {HEIGHTS.map((_, i) => (
          <text key={i} x={LEFT + i * STEP + BAR_W / 2} y={160}>
            {i + 1}
          </text>
        ))}
      </g>
      <g className="tart-final" data-anim="badges">
        <Badge index={PLUS_INDEX} sign="+" fill="var(--color-status-completed)" ink="var(--color-bg)" />
        <Badge index={MINUS_INDEX} sign="−" fill="var(--color-series-7)" ink="var(--color-accent-fg)" />
      </g>
    </svg>
  );
}
