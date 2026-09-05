/**
 * Card art for `value-policy-learning/monte-carlo-return`: one game played all
 * the way to the end, the return that actually followed collapsed into a
 * single bar, and that bar poured into the one column the game played. On play
 * the trajectory draws itself move by move while the return fills behind it,
 * the game reaches its terminal board, and the whole episode funnels down into
 * one of the seven columns — the other six are never labelled.
 *
 * Server component. Motion lives in monte-carlo-return.css (transform,
 * opacity and stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./monte-carlo-return.css";

/** One point per move of the played-out game, wandering to the terminal board. */
const STEPS: readonly (readonly [number, number])[] = [
  [28, 40],
  [54, 32],
  [80, 42],
  [106, 34],
  [132, 44],
  [158, 36],
  [184, 42],
  [210, 32],
  [236, 40],
];

const TRAJECTORY = STEPS.map(([x, y], index) => `${index === 0 ? "M" : "L"}${x},${y}`).join("");
/** Comfortably longer than the wandering path, so offset 0 draws it whole. */
const TRAJECTORY_LENGTH = 224;

const END_X = 250;
const END_Y = 40;

const BAR_X = 28;
const BAR_Y = 54;
const BAR_W = 208;
const BAR_H = 14;

const SLOT_X = [86, 108, 130, 152, 174, 196, 218];
const SLOT_Y = 110;
const SLOT_W = 16;
const SLOT_H = 42;
/** The one column the game actually played, and so the only one labelled. */
const PLAYED = 3;

const FUNNEL = [
  `M${BAR_X},${BAR_Y + BAR_H}`,
  `L${SLOT_X[PLAYED]},${SLOT_Y}`,
  `L${SLOT_X[PLAYED] + SLOT_W},${SLOT_Y}`,
  `L${BAR_X + BAR_W},${BAR_Y + BAR_H}Z`,
].join("");

export function MonteCarloReturnArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-monte-carlo-return",
        "A whole game played to its terminal board, its return filling one bar, and that bar funnelling into the single column the game played",
        props,
      )}
    >
      <path
        className="trail"
        data-anim="trail"
        d={TRAJECTORY}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth={1.6}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray={TRAJECTORY_LENGTH}
        strokeDashoffset={0}
      />
      {[0, 3, 6].map((start, group) => (
        <g key={start} data-anim={`steps-${group + 1}`}>
          {STEPS.slice(start, start + 3).map(([x, y]) => (
            <circle key={x} cx={x} cy={y} r={3} fill="var(--color-accent)" />
          ))}
        </g>
      ))}
      <g className="end" data-anim="end" stroke="var(--color-ink-3)" strokeWidth={1.4} fill="none">
        <rect x={END_X - 6} y={END_Y - 6} width={12} height={12} rx={2} />
        <path d={`M${END_X - 3},${END_Y - 3}l6,6M${END_X + 3},${END_Y - 3}l-6,6`} strokeLinecap="round" />
      </g>
      <rect
        x={BAR_X}
        y={BAR_Y}
        width={BAR_W}
        height={BAR_H}
        rx={3}
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
      />
      <rect
        className="return"
        data-anim="return"
        x={BAR_X}
        y={BAR_Y}
        width={BAR_W}
        height={BAR_H}
        rx={3}
        fill="var(--color-accent-strong)"
      />
      <path className="funnel" data-anim="funnel" d={FUNNEL} fill="var(--color-accent-soft)" />
      <g fill="var(--color-cell)" stroke="var(--color-rule-strong)">
        {SLOT_X.map((x) => (
          <rect key={x} x={x} y={SLOT_Y} width={SLOT_W} height={SLOT_H} rx={3} />
        ))}
      </g>
      <rect
        className="played"
        data-anim="played"
        x={SLOT_X[PLAYED] + 1.5}
        y={SLOT_Y + 1.5}
        width={SLOT_W - 3}
        height={SLOT_H - 3}
        rx={2}
        fill="var(--color-accent-strong)"
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={14} y={18}>
          one game
        </text>
        <text x={244} y={BAR_Y + BAR_H - 3} fill="var(--color-ink-2)">
          return
        </text>
        <text x={SLOT_X[PLAYED] + SLOT_W / 2} y={SLOT_Y + SLOT_H + 14} textAnchor="middle" fill="var(--color-ink-2)">
          played
        </text>
      </g>
    </svg>
  );
}
