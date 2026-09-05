/**
 * Card art for `constructive-reservoir/tail-survival-cem`: a population of
 * weight vectors scattered along how long their games lived, five survival
 * milestones standing on that axis with rapidly increasing weight, and the
 * sampling distribution above them. On play the population is sampled, the
 * few that reached the far milestones are marked as the elites, and the
 * sampling distribution slides and narrows onto them — one generation of the
 * cross-entropy method, aimed at the tail rather than the mean.
 *
 * Server component. Motion lives in tail-survival-cem.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./tail-survival-cem.css";

const AXIS_Y = 134;
const AXIS_X0 = 24;
const AXIS_X1 = 300;
const BELL_BASE = 96;

/** The five survival milestones, each taller than the last because it counts for more. */
const GATES: readonly { x: number; h: number; o: number }[] = [
  { x: 92, h: 6, o: 0.32 },
  { x: 113, h: 9, o: 0.45 },
  { x: 155, h: 15, o: 0.6 },
  { x: 219, h: 24, o: 0.8 },
  { x: 283, h: 34, o: 1 },
];

/** One sampled weight vector, placed at how long its games lived. */
const POPULATION: readonly [number, number][] = [
  [36, 122],
  [44, 110],
  [51, 128],
  [58, 116],
  [62, 106],
  [67, 124],
  [73, 112],
  [79, 120],
  [86, 108],
  [94, 126],
  [103, 114],
  [118, 118],
  [137, 108],
  [168, 122],
  [214, 112],
  [268, 116],
];

/** The few kept and resampled toward: the ones that lived into the tail. */
const ELITES: readonly [number, number][] = [
  [137, 108],
  [168, 122],
  [214, 112],
  [268, 116],
];

/** The sampling distribution, as a closed curve standing on its own baseline. */
function bellPath(mu: number, sigma: number, peak: number): string {
  const points: string[] = [];
  for (let x = AXIS_X0; x <= AXIS_X1; x += 6) {
    const y = BELL_BASE - peak * Math.exp(-((x - mu) ** 2) / (2 * sigma * sigma));
    points.push(`${x},${y.toFixed(2)}`);
  }
  return `M${points.join("L")}L${AXIS_X1},${BELL_BASE}L${AXIS_X0},${BELL_BASE}z`;
}

const BELL = bellPath(180, 26, 50);

export function TailSurvivalCemArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-tail-survival-cem",
        "A sampled population spread along how long its games lived, with the sampling distribution moving onto the few that reached the far milestones",
        props,
      )}
    >
      <line x1={AXIS_X0} y1={AXIS_Y} x2={AXIS_X1} y2={AXIS_Y} stroke="var(--color-rule-strong)" strokeWidth={1} />
      {GATES.map((gate) => (
        <path
          key={gate.x}
          d={`M${gate.x},${AXIS_Y}v-${gate.h}`}
          stroke="var(--color-accent)"
          strokeWidth={3.5}
          strokeLinecap="round"
          opacity={gate.o}
        />
      ))}
      <path
        data-anim="shift"
        d={BELL}
        fill="var(--color-accent-soft)"
        fillOpacity={0.55}
        stroke="var(--color-accent)"
        strokeWidth={1.5}
      />

      <g data-anim="pop">
        {POPULATION.map(([x, y]) => (
          <circle key={`${x}-${y}`} cx={x} cy={y} r={2.6} fill="var(--color-ink-3)" />
        ))}
      </g>

      <g data-anim="elite">
        {ELITES.map(([x, y]) => (
          <g key={`${x}-${y}`}>
            <circle cx={x} cy={y} r={3} fill="var(--color-accent)" />
            <circle cx={x} cy={y} r={5.5} fill="none" stroke="var(--color-accent)" strokeWidth={1.2} />
          </g>
        ))}
      </g>

      <g data-anim="pull">
        <path d="M96,36h54M144,32l6,4l-6,4" fill="none" stroke="var(--color-accent)" strokeWidth={1.4} strokeLinecap="round" />
        <text x={96} y={28} fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
          resample
        </text>
      </g>

      <g fontFamily={ART_MONO} fontSize={9}>
        <text x={300} y={104} textAnchor="end" fill="var(--color-accent)">
          elites
        </text>
        <text x={24} y={148} fill="var(--color-ink-3)">
          milestones
        </text>
        <text x={300} y={148} textAnchor="end" fill="var(--color-ink-3)">
          moves lived
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={172} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          tuned for survival, not the mean
        </text>
      </g>
    </svg>
  );
}
