/**
 * Card art for `oracle-curriculum/oracle-dagger`: the correction arrives on
 * the student's own trajectory. The teacher's games run along the top; the
 * student takes the first two states with it and then drifts away downward,
 * and the teacher reaches down onto each drifted state and labels it. The
 * further the student has drifted, the longer the reach.
 *
 * Server component. Motion lives in oracle-dagger.css (stroke-dashoffset and
 * opacity only); the markup is the resting frame, with the drifted branch
 * drawn, every correction landed and every labelled state ringed.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./oracle-dagger.css";

/** The teacher's own trajectory: seven states along one of its games. */
const TEACHER_X = [56, 94, 132, 170, 208, 246, 284];
const TEACHER_Y = 40;
/** Where the student ends up once it stops matching the teacher. */
const DRIFT: ReadonlyArray<readonly [number, number]> = [
  [132, 78],
  [170, 112],
  [208, 140],
];
/** Dash lengths covering each correction and its head, longest reach last. */
const REACH = [40, 76, 104];
const NODE = 11;
/** The student's path: two shared states, then away. */
const TRAJECTORY = "M56,40H94Q118,42 132,78Q146,106 170,112Q194,118 208,140";
/** One correction per drifted state, dropping from the teacher's line. */
const CORRECTIONS = DRIFT.map(([x, y]) => `M${x},49V${y - 9}m-4,-6l4,6l4,-6`);

function Node({ x, y, fill }: { x: number; y: number; fill: string }) {
  return <rect x={x - NODE / 2} y={y - NODE / 2} width={NODE} height={NODE} rx={2.5} fill={fill} />;
}

export function OracleDaggerArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-oracle-dagger",
        "A student trajectory drifting below the teacher's, with the teacher's labels dropping onto the states the drift reached",
        props,
      )}
    >
      <line
        x1={TEACHER_X[0]}
        y1={TEACHER_Y}
        x2={TEACHER_X[TEACHER_X.length - 1]}
        y2={TEACHER_Y}
        stroke="var(--color-reads-teacher)"
        strokeWidth={1.6}
      />
      {TEACHER_X.map((x) => (
        <Node key={x} x={x} y={TEACHER_Y} fill="var(--color-reads-teacher)" />
      ))}
      <path
        className="drift"
        data-anim="drift"
        d={TRAJECTORY}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={2}
        strokeLinecap="round"
        strokeDasharray={210}
      />
      {DRIFT.map(([x, y]) => (
        <Node key={`${x}-${y}`} x={x} y={y} fill="var(--color-accent)" />
      ))}
      {CORRECTIONS.map((d, index) => (
        <path
          key={d}
          className={`fix fix-${index}`}
          data-anim={`fix-${index}`}
          d={d}
          fill="none"
          stroke="var(--color-reads-teacher)"
          strokeWidth={1.6}
          strokeLinecap="round"
          strokeLinejoin="round"
          strokeDasharray={REACH[index]}
        />
      ))}
      <g className="marks" data-anim="marks">
        {DRIFT.map(([x, y]) => (
          <rect
            key={`${x}-${y}`}
            x={x - 8.5}
            y={y - 8.5}
            width={17}
            height={17}
            rx={3}
            fill="none"
            stroke="var(--color-highlight)"
            strokeWidth={1.6}
          />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize={9}>
        <text x={8} y={43} fill="var(--color-reads-teacher)">
          teacher
        </text>
        <text x={80} y={124} fill="var(--color-accent)">
          student
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={172} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the teacher labels where the student went
        </text>
      </g>
    </svg>
  );
}
