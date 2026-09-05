/**
 * Card art for `d4-long-outcome/long-outcome`: the label is what happens far
 * away, not next move. One position forces every legal column, and all seven
 * lines are then played forward through the *same* imagined future — the tape
 * of discs across the top — until the horizon twenty-five moves on, where
 * their outcomes have fanned apart and one line has already died.
 *
 * Server component. Motion lives in long-outcome.css (transform, opacity and
 * stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, cellCenter, type BoardGeometry } from "../board";
import "./long-outcome.css";

/** The shared future: the same discs arrive for every column that is tried. */
const TAPE: BoardGeometry = { x: 94, y: 14, cell: 26, cols: 7, rows: 1 };
const FUTURE = [3, 6, 1, 5, 2, 7, 4];
const GUIDE_X = FUTURE.map((_, index) => cellCenter(index, 0, TAPE)[0]);
const START = { x: 34, y: 92 };
const HORIZON_X = 290;
const TRACK_TOP = 46;
const TRACK_BOTTOM = 146;

/**
 * One line per legal column: its height at each disc of the shared future and
 * then at the horizon. The last one has no horizon entry — it died at the
 * fifth disc, and the game charges a large penalty for that.
 */
const TRACKS: readonly (readonly number[])[] = [
  [88, 78, 82, 68, 64, 54, 56, 48],
  [92, 86, 78, 74, 72, 64, 66, 60],
  [90, 92, 88, 82, 78, 80, 74, 72],
  [96, 90, 94, 90, 88, 84, 86, 84],
  [94, 98, 92, 98, 94, 98, 94, 96],
  [98, 102, 100, 106, 104, 110, 108, 110],
  [102, 108, 116, 124, 132],
];
const DIED = TRACKS.length - 1;

function trackPath(profile: readonly number[]): string {
  const points = profile.map((y, index) => `${index < GUIDE_X.length ? GUIDE_X[index] : HORIZON_X},${y}`);
  return `M${START.x},${START.y}L${points.join("L")}`;
}

export function LongOutcomeArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-long-outcome",
        "Every legal column of one position is played forward through the same imagined future, and twenty-five moves on their outcomes have fanned apart and one line has died",
        props,
      )}
    >
      <text x={12} y={11} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        the same imagined future
      </text>
      <path
        d={GUIDE_X.map((x) => `M${x},${TRACK_TOP}V${TRACK_BOTTOM}`).join("")}
        fill="none"
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        strokeDasharray="2 4"
      />
      <g className="tape" data-anim="tape">
        <ArtBoard g={TAPE} />
        {FUTURE.map((value, index) => (
          <ArtDisc key={value} value={value} col={index} row={0} g={TAPE} />
        ))}
      </g>
      <g className="position">
        <rect
          x={16}
          y={83}
          width={18}
          height={18}
          rx={3}
          fill="var(--color-cell)"
          stroke="var(--color-rule-strong)"
        />
        <circle cx={21} cy={96} r={2.6} fill="var(--color-disc-3)" />
        <circle cx={28} cy={96} r={2.6} fill="var(--color-disc-5)" />
        <circle cx={21} cy={89} r={2.6} fill="var(--color-disc-2)" />
      </g>
      <text x={12} y={118} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        every column
      </text>
      {TRACKS.map((profile, index) => (
        <path
          key={index}
          className="track"
          data-anim="track"
          d={trackPath(profile)}
          fill="none"
          stroke={index === DIED ? "var(--color-ink-3)" : `var(--color-series-${index + 1})`}
          strokeWidth={1.6}
          strokeLinecap="round"
          strokeLinejoin="round"
          strokeDasharray={360}
        />
      ))}
      <g className="death" data-anim="death">
        <path
          d="M205,126l12,12M217,126l-12,12"
          fill="none"
          stroke="var(--color-danger)"
          strokeWidth={2}
          strokeLinecap="round"
        />
      </g>
      <line
        x1={HORIZON_X}
        y1={TRACK_TOP}
        x2={HORIZON_X}
        y2={TRACK_BOTTOM}
        stroke="var(--color-highlight)"
        strokeWidth={1.2}
        strokeDasharray="3 3"
      />
      <g className="ends" data-anim="ends">
        {TRACKS.map((profile, index) =>
          index === DIED ? null : (
            <circle
              key={index}
              cx={HORIZON_X}
              cy={profile[profile.length - 1]}
              r={4}
              fill={`var(--color-series-${index + 1})`}
            />
          ),
        )}
      </g>
      <text
        x={HORIZON_X - 4}
        y={160}
        textAnchor="end"
        fontFamily={ART_MONO}
        fontSize={9}
        fill="var(--color-highlight)"
      >
        25 moves on
      </text>
      <g className="tart-final" data-anim="caption">
        <text x={12} y={160} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          what each column was worth
        </text>
      </g>
    </svg>
  );
}
