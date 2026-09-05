/**
 * Card art for `ntuple-rl/torch-ppo`: the handover from copying to playing.
 * A two-move teacher on the left hands labelled positions down a leash into
 * the policy network; then the leash is dropped, the teacher goes quiet, and
 * the policy's own games loop back into it instead. On play two labels travel
 * the leash, the leash fades to a ghost with the teacher behind it, and the
 * return loop draws itself closed. At rest the teacher and the leash are both
 * ghosts and the loop is the only live thing feeding the policy.
 *
 * Server component. Motion lives in torch-ppo.css (transform, opacity and
 * stroke-dashoffset only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells } from "../board";
import "./torch-ppo.css";

/** The teacher: an exact search two moves deep. */
const ROOT: [number, number] = [40, 52];
const PLY_1 = [24, 56];
const PLY_2 = [14, 34, 46, 66];
const TEACHER_EDGES = [
  ...PLY_1.map((x) => `M${ROOT[0]},${ROOT[1] + 4}L${x},76`),
  ...PLY_1.flatMap((x, index) => [
    `M${x},84L${PLY_2[index * 2]},104`,
    `M${x},84L${PLY_2[index * 2 + 1]},104`,
  ]),
].join("");

/** The policy: three inputs, two hidden units, one answer per column. */
const NET_IN = [68, 82, 96];
const NET_HID = [75, 89];
const NET_EDGES = NET_IN.flatMap((y) => NET_HID.map((y2) => `M138,${y}L160,${y2}`))
  .concat(NET_HID.map((y) => `M160,${y}L182,82`))
  .join("");

/** The board the policy plays on once the teacher is gone. */
const GAME = { x: 236, y: 58, cell: 15, cols: 4, rows: 3 };
const GAME_CELLS = "0000" + "0000" + "3050";

/** The return path from the game back into the policy, and its drawn length. */
const LOOP = "M266,105V132Q266,140 258,140H168Q160,140 160,132V114";

export function TorchPpoArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-torch-ppo",
        "A policy network copies a two-move teacher, then the leash is dropped and its own games loop back into it",
        props,
      )}
    >
      <g data-anim="teacher" opacity={0.4}>
        <path d={TEACHER_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth={0.9} />
        <circle cx={ROOT[0]} cy={ROOT[1]} r={4.5} fill="var(--color-ink-2)" />
        <g fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth={1}>
          {PLY_1.map((x) => (
            <circle key={`a${x}`} cx={x} cy={80} r={3.4} />
          ))}
          {PLY_2.map((x) => (
            <circle key={`b${x}`} cx={x} cy={108} r={3.4} />
          ))}
        </g>
      </g>

      <g data-anim="leash" opacity={0.3}>
        <path
          d="M78,80H114"
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth={1.2}
          strokeDasharray="3 3"
        />
        <path d="M120,80l-7,-4v8z" fill="var(--color-ink-3)" />
      </g>
      <rect
        data-anim="label-1"
        opacity={0}
        x={100}
        y={76}
        width={14}
        height={8}
        rx={2}
        fill="var(--color-reads-teacher)"
      />
      <rect
        data-anim="label-2"
        opacity={0}
        x={100}
        y={76}
        width={14}
        height={8}
        rx={2}
        fill="var(--color-reads-teacher)"
      />

      <rect
        x={124}
        y={54}
        width={72}
        height={56}
        rx={4}
        fill="var(--color-surface)"
        stroke="var(--color-rule-strong)"
      />
      <path d={NET_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth={0.8} />
      <g fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth={1.1}>
        {NET_IN.map((y) => (
          <circle key={`i${y}`} cx={138} cy={y} r={3.4} />
        ))}
        {NET_HID.map((y) => (
          <circle key={`h${y}`} cx={160} cy={y} r={3.4} />
        ))}
        <circle cx={182} cy={82} r={4.4} />
      </g>

      <g data-anim="forward">
        <path d="M200,82h22" fill="none" stroke="var(--color-accent)" strokeWidth={1.6} strokeLinecap="round" />
        <path d="M228,82l-7,-4v8z" fill="var(--color-accent)" />
      </g>

      <ArtBoard g={GAME}>
        <ArtCells cells={GAME_CELLS} g={GAME} />
      </ArtBoard>

      <path
        data-anim="loop"
        d={LOOP}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={1.6}
        strokeDasharray={168}
      />
      <path data-anim="loop-head" d="M160,108l-4.5,7h9z" fill="var(--color-accent)" />

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={40} y={36} textAnchor="middle">
          teacher
        </text>
        <text x={97} y={66} textAnchor="middle">
          clone
        </text>
        <text x={160} y={48} textAnchor="middle">
          policy
        </text>
        <text x={216} y={130} textAnchor="middle">
          play
        </text>
      </g>

      <g className="tart-final" data-anim="caption">
        <text x={160} y={172} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          cloned first, then improved by playing
        </text>
      </g>
    </svg>
  );
}
