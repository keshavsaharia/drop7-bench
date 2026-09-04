/**
 * Card art for `value-policy-learning/monte-carlo-value`: one board, played
 * out from again and again, and the many different lengths those games ran
 * for averaged into the single number that becomes the board's value. On play
 * four replays leave the same state and stop at four different points, then
 * their ends are drawn together onto one mean and the value bar grows to it.
 *
 * The state is valued, not the move: nothing here picks a column.
 *
 * Server component. Motion lives in monte-carlo-value.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, type BoardGeometry } from "../board";
import "./monte-carlo-value.css";

/** A compact board on the left; the replays get the rest of the frame. */
const G: BoardGeometry = { x: 8, y: 30, cell: 16, cols: 7, rows: 7 };

/** A quiet mid-game board: no run on it is as long as its own disc's number. */
const CELLS = "0000000" + "0000000" + "0006000" + "0603700" + "5821310" + "2519645" + "6154263";

/** Where the replays leave the state, and where each one's game ran out. */
const FAN_X = 122;
const FAN_Y = 86;
const TRACK_X = 138;
const TRACKS = [
  { y: 46, end: 200 },
  { y: 68, end: 258 },
  { y: 90, end: 176 },
  { y: 112, end: 246 },
];
/** The one number the four outcomes average to; the value bar stops here. */
const MEAN = TRACKS.reduce((total, track) => total + track.end, 0) / TRACKS.length;
const LONGEST = Math.max(...TRACKS.map((track) => track.end));

const BAR_Y = 126;
const BAR_H = 12;

export function MonteCarloValueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-monte-carlo-value",
        "One board replayed four times, the four games running out at four different lengths, and their mean becoming the board's single value",
        props,
      )}
    >
      <ArtBoard g={G}>
        <ArtCells cells={CELLS} g={G} />
      </ArtBoard>
      <g className="fan" data-anim="fan" fill="none" stroke="var(--color-ink-3)" strokeWidth={1}>
        {TRACKS.map((track) => (
          <path key={track.y} d={`M${FAN_X},${FAN_Y}C${FAN_X + 8},${FAN_Y} ${FAN_X + 8},${track.y} ${TRACK_X},${track.y}`} />
        ))}
      </g>
      <circle cx={FAN_X} cy={FAN_Y} r={2.5} fill="var(--color-ink-2)" />
      {TRACKS.map((track, index) => (
        <g key={track.y} className="track" data-anim={`track-${index + 1}`}>
          <rect
            x={TRACK_X}
            y={track.y - 1.75}
            width={track.end - TRACK_X}
            height={3.5}
            rx={1.75}
            fill="var(--color-ink-2)"
          />
          <rect x={track.end - 1.25} y={track.y - 6} width={2.5} height={12} rx={1} fill="var(--color-ink-1)" />
        </g>
      ))}
      <g className="converge" data-anim="converge" stroke="var(--color-rule-strong)" strokeWidth={1}>
        {TRACKS.map((track) => (
          <line key={track.y} x1={track.end} y1={track.y + 6} x2={MEAN} y2={BAR_Y - 2} />
        ))}
      </g>
      <line
        className="mean"
        data-anim="mean"
        x1={MEAN}
        y1={36}
        x2={MEAN}
        y2={BAR_Y + BAR_H + 6}
        stroke="var(--color-highlight)"
        strokeWidth={1.2}
        strokeDasharray="3 4"
      />
      <rect
        x={TRACK_X}
        y={BAR_Y}
        width={LONGEST - TRACK_X}
        height={BAR_H}
        rx={3}
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
      />
      <rect
        className="value"
        data-anim="value"
        x={TRACK_X}
        y={BAR_Y}
        width={MEAN - TRACK_X}
        height={BAR_H}
        rx={3}
        fill="var(--color-accent)"
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={TRACK_X + 2} y={30}>
          moves left
        </text>
        <text x={TRACK_X + 2} y={BAR_Y + BAR_H + 14} fill="var(--color-ink-2)">
          value
        </text>
        <text x={G.x + G.cell * G.cols * 0.5} y={G.y + G.cell * G.rows + 14} textAnchor="middle" fill="var(--color-ink-2)">
          state
        </text>
      </g>
    </svg>
  );
}
