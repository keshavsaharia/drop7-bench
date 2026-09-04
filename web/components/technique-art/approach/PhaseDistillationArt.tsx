/**
 * Card art for `value-policy-learning/phase-distillation`: one teacher, two
 * students. The exact depth-3 search with its five chance strata sits under
 * the five-move rise clock it is conditioned on, and what it produces forks:
 * the upper student is handed the column the teacher chose, the lower one the
 * value the teacher put on the move. Two ways to copy the same search, and
 * the fork is the subject.
 *
 * Server component. Motion lives in phase-distillation.css (transform,
 * opacity and stroke-dashoffset only); the markup is the resting frame, with
 * both forks drawn and both students holding an answer.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./phase-distillation.css";

/** The five moves of a rise cycle; two of them spent. */
const CLOCK_X = [24, 36, 48, 60, 72];
const CLOCK_SPENT = 2;

/** The teacher: a root, its columns, the five chance strata, and one more ply. */
const ROOT: readonly [number, number] = [58, 46];
const PLY_1: ReadonlyArray<readonly [number, number]> = [
  [30, 70],
  [58, 70],
  [86, 70],
];
const STRATA: ReadonlyArray<readonly [number, number]> = [
  [34, 92],
  [46, 92],
  [58, 92],
  [70, 92],
  [82, 92],
];
const PLY_3: ReadonlyArray<readonly [number, number]> = [
  [34, 116],
  [58, 116],
  [82, 116],
];

function edges(from: readonly [number, number], to: ReadonlyArray<readonly [number, number]>): string {
  return to.map(([x, y]) => `M${from[0]},${from[1]}L${x},${y}`).join("");
}

const FORK_TOP = "M98,86C126,86 132,58 170,58m-6,-4l6,4l-6,4";
const FORK_BOTTOM = "M98,86C126,86 132,116 170,116m-6,-4l6,4l-6,4";

/** The seven columns the upper student answers with; it plays the fourth. */
const TICK_X = [232, 243, 254, 265, 276, 287, 298];
const CHOSEN = 3;

function Student({ y }: { y: number }) {
  const output: readonly [number, number] = [210, y + 14];
  const inputs = [y + 6, y + 14, y + 22];
  return (
    <g className="student">
      <rect x={174} y={y} width={46} height={28} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
      <path
        d={inputs.map((iy) => `M184,${iy}L${output[0]},${output[1]}`).join("")}
        stroke="var(--color-accent)"
        strokeWidth={0.9}
        fill="none"
      />
      {inputs.map((iy) => (
        <circle key={iy} cx={184} cy={iy} r={1.8} fill="var(--color-accent)" />
      ))}
      <circle cx={output[0]} cy={output[1]} r={2.6} fill="var(--color-accent)" />
    </g>
  );
}

export function PhaseDistillationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-phase-distillation",
        "A depth-3 search with five chance strata under a five-move rise clock, forking into one student handed the chosen column and one handed the value",
        props,
      )}
    >
      {CLOCK_X.map((cx, index) => (
        <circle
          key={cx}
          cx={cx}
          cy={22}
          r={3.6}
          fill={index < CLOCK_SPENT ? "var(--color-accent)" : "none"}
          stroke="var(--color-ink-4)"
          strokeWidth={1.2}
        />
      ))}
      <text x={82} y={26} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        phase
      </text>
      <path d={edges(ROOT, PLY_1)} stroke="var(--color-rule-strong)" strokeWidth={1} fill="none" />
      <circle cx={ROOT[0]} cy={ROOT[1]} r={5} fill="var(--color-reads-public)" />
      {PLY_1.map(([cx, cy]) => (
        <circle key={cx} cx={cx} cy={cy} r={4} fill="var(--color-reads-public)" />
      ))}
      <g className="search" data-anim="search">
        <path d={edges(PLY_1[1], STRATA)} stroke="var(--color-rule-strong)" strokeWidth={1} fill="none" />
        {STRATA.map(([cx, cy]) => (
          <circle key={cx} cx={cx} cy={cy} r={2.6} fill="var(--color-reads-public)" />
        ))}
        <path d={edges(STRATA[2], PLY_3)} stroke="var(--color-rule-strong)" strokeWidth={1} fill="none" />
        {PLY_3.map(([cx, cy]) => (
          <circle key={cx} cx={cx} cy={cy} r={3.5} fill="var(--color-reads-public)" />
        ))}
      </g>
      <text x={ROOT[0]} y={136} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-reads-public)">
        depth 3
      </text>
      <path
        className="fork-top"
        data-anim="fork-top"
        d={FORK_TOP}
        fill="none"
        stroke="var(--color-reads-public)"
        strokeWidth={1.6}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray={100}
      />
      <path
        className="fork-bottom"
        data-anim="fork-bottom"
        d={FORK_BOTTOM}
        fill="none"
        stroke="var(--color-reads-public)"
        strokeWidth={1.6}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray={100}
      />
      <Student y={44} />
      <Student y={102} />
      <path
        d={TICK_X.map((x) => `M${x},48V68`).join("")}
        stroke="var(--color-ink-4)"
        strokeWidth={2.2}
        strokeLinecap="round"
        fill="none"
      />
      <g className="column" data-anim="column">
        <path
          d={`M${TICK_X[CHOSEN]},48V68`}
          stroke="var(--color-highlight)"
          strokeWidth={3}
          strokeLinecap="round"
          fill="none"
        />
        <path
          d={`M${TICK_X[CHOSEN] - 6},42l6,-6l6,6`}
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
          strokeLinecap="round"
          strokeLinejoin="round"
          fill="none"
        />
      </g>
      <rect x={232} y={110} width={66} height={12} rx={3} fill="var(--color-cell)" stroke="var(--color-rule)" strokeWidth={1} />
      <rect
        className="value"
        data-anim="value"
        x={234}
        y={112}
        width={46}
        height={8}
        rx={2}
        fill="var(--color-accent)"
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)" textAnchor="middle">
        <text x={TICK_X[CHOSEN]} y={30}>
          column
        </text>
        <text x={265} y={102}>
          value
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={170} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          one phase teacher, two ways to copy it
        </text>
      </g>
    </svg>
  );
}
