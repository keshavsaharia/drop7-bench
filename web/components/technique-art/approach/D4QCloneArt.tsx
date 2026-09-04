/**
 * Card art for `value-policy-learning/d4-q-clone`: the whole row is copied,
 * not the winner. The depth-4 search finishes with a value for every one of
 * the seven columns, and those seven bars — bracketed together, not the
 * tallest one alone — are what crosses to the clone. The clone's copy sits
 * inside a frame of its own because each position's row is rescaled on its
 * own: the absolute scale is thrown away and only the ordering survives.
 *
 * Server component. Motion lives in d4-q-clone.css (transform, opacity and
 * stroke-dashoffset only); the markup is the resting frame, with both rows
 * standing and the bracket and arrow drawn.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./d4-q-clone.css";

const BAR = 12;
const GAP = 5;
const COLUMNS = [1, 2, 3, 4, 5, 6, 7];

/** The search's row: seven values close together on a scale of its own. */
const TEACHER_X = 18;
const TEACHER_BASE = 132;
const TEACHER_HEIGHT = [62, 70, 58, 78, 66, 54, 74];

/** The same row after per-position rescaling: same order, full spread. */
const FRAME = { x: 176, y: 48, width: 126, height: 88 };
const CLONE_X = 182;
const CLONE_BASE = 133;
const CLONE_HEIGHT = [30, 52, 19, 74, 41, 8, 63];

const BRACKET = "M138,52h6v76h-6";
const ARROW = "M146,90h22m-6,-5l6,5l-6,5";

function barX(origin: number, index: number): number {
  return origin + index * (BAR + GAP);
}

export function D4QCloneArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-d4-q-clone",
        "A depth-4 search's seven column values bracketed together and copied whole into a clone, rescaled so only the ordering survives",
        props,
      )}
    >
      <g className="teacher" data-anim="teacher">
        {TEACHER_HEIGHT.map((height, index) => (
          <rect
            key={index}
            x={barX(TEACHER_X, index)}
            y={TEACHER_BASE - height}
            width={BAR}
            height={height}
            rx={2}
            fill="var(--color-reads-public)"
          />
        ))}
      </g>
      <line
        x1={TEACHER_X - 4}
        y1={TEACHER_BASE + 0.5}
        x2={barX(TEACHER_X, 6) + BAR + 4}
        y2={TEACHER_BASE + 0.5}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)" textAnchor="middle">
        {COLUMNS.map((column, index) => (
          <text key={column} x={barX(TEACHER_X, index) + BAR / 2} y={TEACHER_BASE + 13}>
            {column}
          </text>
        ))}
      </g>
      <text x={TEACHER_X - 4} y={30} fontFamily={ART_MONO} fontSize={9} fill="var(--color-reads-public)">
        depth 4
      </text>
      <path
        className="bracket"
        data-anim="bracket"
        d={BRACKET}
        fill="none"
        stroke="var(--color-ink-2)"
        strokeWidth={1.4}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray={92}
      />
      <path
        className="carry"
        data-anim="carry"
        d={ARROW}
        fill="none"
        stroke="var(--color-ink-2)"
        strokeWidth={1.6}
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeDasharray={44}
      />
      <rect
        className="frame"
        data-anim="frame"
        x={FRAME.x}
        y={FRAME.y}
        width={FRAME.width}
        height={FRAME.height}
        rx={3}
        fill="none"
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
        strokeDasharray="4 3"
      />
      <g className="clone" data-anim="clone">
        {CLONE_HEIGHT.map((height, index) => (
          <rect
            key={index}
            x={barX(CLONE_X, index)}
            y={CLONE_BASE - height}
            width={BAR}
            height={height}
            rx={2}
            fill="var(--color-accent)"
          />
        ))}
      </g>
      <text
        x={FRAME.x + FRAME.width / 2}
        y={FRAME.y - 6}
        textAnchor="middle"
        fontFamily={ART_MONO}
        fontSize={9}
        fill="var(--color-ink-2)"
      >
        normalised
      </text>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={170} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          the whole row, not just the winner
        </text>
      </g>
    </svg>
  );
}
