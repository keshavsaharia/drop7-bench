/**
 * Card art for `ntuple-rl/curriculum-option-ppo`: the two places an episode is
 * allowed to begin. An almost empty opening board on the left, a crowded
 * mid-game restart position on the right, and one training round as a bar
 * below them. On play the opening board flashes and an episode drops into the
 * round's first half; then the restart board flashes and an episode fills the
 * second half. At rest the round stands half from each starting position.
 *
 * Both boards are legal Drop7 positions with nothing pending, checked against
 * the engine's run rule (gray discs count toward a run length).
 *
 * Server component. Motion lives in curriculum-option-ppo.css (transform and
 * opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells } from "../board";
import "./curriculum-option-ppo.css";

/** Two five-row boards, one on each side of the frame. */
const OPENING = { x: 12, y: 40, cell: 15, cols: 7, rows: 5 };
const RESTART = { x: 203, y: 40, cell: 15, cols: 7, rows: 5 };

/** A fresh opening: three discs on the floor and nothing else. */
const OPENING_CELLS = "0000000" + "0000000" + "0000000" + "0000000" + "0304050";
/** A restart position: crowded to the ceiling, two covers still buried. */
const RESTART_CELLS = "0602000" + "0786070" + "7223590" + "4436263" + "2654635";

/** The round: one bar, filled from the left by one source and the right by the other. */
const BAR = { x: 56, y: 132, w: 208, h: 16 };
const BAR_MID = BAR.x + BAR.w / 2;
const BAR_TICKS = [82, 108, 134, 186, 212, 238];

function BoardPanel({
  g,
  cells,
  label,
  flash,
}: {
  g: typeof OPENING;
  cells: string;
  label: string;
  flash: string;
}) {
  return (
    <g>
      <ArtBoard g={g}>
        <ArtCells cells={cells} g={g} />
      </ArtBoard>
      <rect
        data-anim={flash}
        opacity={0}
        x={g.x - 3}
        y={g.y - 3}
        width={g.cols * g.cell + 6}
        height={g.rows * g.cell + 6}
        rx={6}
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth={1.4}
      />
      <text
        x={g.x + (g.cols * g.cell) / 2}
        y={g.y - 8}
        textAnchor="middle"
        fontFamily={ART_MONO}
        fontSize={9}
        fill="var(--color-ink-3)"
      >
        {label}
      </text>
    </g>
  );
}

export function CurriculumOptionPpoArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-curriculum-option-ppo",
        "An empty opening board and a crowded restart board, each filling half of one training round",
        props,
      )}
    >
      <BoardPanel g={OPENING} cells={OPENING_CELLS} label="opening" flash="flash-opening" />
      <BoardPanel g={RESTART} cells={RESTART_CELLS} label="restart" flash="flash-restart" />

      <rect
        x={BAR.x}
        y={BAR.y}
        width={BAR.w}
        height={BAR.h}
        rx={3}
        fill="var(--color-cell)"
        stroke="var(--color-rule-strong)"
      />
      <rect
        className="half"
        data-anim="fill-opening"
        x={BAR.x}
        y={BAR.y}
        width={BAR.w / 2}
        height={BAR.h}
        rx={3}
        fill="var(--color-accent-strong)"
      />
      <rect
        className="half half--right"
        data-anim="fill-restart"
        x={BAR_MID}
        y={BAR.y}
        width={BAR.w / 2}
        height={BAR.h}
        rx={3}
        fill="var(--color-series-4)"
      />
      <path
        d={BAR_TICKS.map((x) => `M${x},${BAR.y + 2}v${BAR.h - 4}`).join("")}
        stroke="var(--color-bg)"
        strokeWidth={1}
        fill="none"
      />
      <line
        x1={BAR_MID}
        y1={BAR.y - 6}
        x2={BAR_MID}
        y2={BAR.y + BAR.h + 6}
        stroke="var(--color-ink-3)"
        strokeWidth={1}
      />

      <rect
        data-anim="episode-opening"
        opacity={0}
        x={BAR.x + 44}
        y={BAR.y + 3.5}
        width={16}
        height={9}
        rx={2}
        fill="var(--color-accent)"
      />
      <rect
        data-anim="episode-restart"
        opacity={0}
        x={BAR_MID + 44}
        y={BAR.y + 3.5}
        width={16}
        height={9}
        rx={2}
        fill="var(--color-series-4)"
      />

      <g className="tart-final" data-anim="caption">
        <text x={160} y={170} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          half of every round starts mid-game
        </text>
      </g>
    </svg>
  );
}
