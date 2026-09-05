/**
 * Card art for `heuristic-search/evolved-public-policy`: the information
 * boundary the evolved evaluator is fitted inside. Everything the policy may
 * read sits in one frame — the board, the disc that comes next, the moves left
 * before the rise — and everything it may not is outside it, struck out. On
 * play the frame closes, the outside is crossed off, and the policy commits to
 * a column using only what is inside.
 *
 * Server component. Motion lives in evolved-public-policy.css (transform and
 * opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing, BOARD } from "../board";
import "./evolved-public-policy.css";

/**
 * A mid-game position: uneven columns, one solid cover buried in the tall
 * column and one cracked cover in the wall beside it. Checked against the
 * rules — no run is its own length, so nothing is waiting to pop.
 */
const CELLS =
  "0000000" + "0000000" + "0003000" + "0308002" + "0647046" + "6561093" + "3224513";

/** The next disc, drawn in the strip above the board. */
const NEXT = { value: 6, cx: 26, cy: 15, r: 7.2 };

/** Moves left before the rise, as the console's own countdown row. */
const PIPS = [104, 113, 122, 131, 140];
const PIPS_LEFT = 2;

/** The column the evolved weights choose, and the cell the disc would land in. */
const PICK = { col: 4, row: 5 };

/** What the policy is not allowed to read, in the order it is struck off. */
const HIDDEN = [
  { label: "score", y: 90, half: 18 },
  { label: "level", y: 112, half: 18 },
  { label: "seed", y: 134, half: 15 },
];
/** Centre of the panel the excluded inputs sit in. */
const OUT_X = 236;

export function EvolvedPublicPolicyArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-evolved-public-policy",
        "The board, the next disc and the rise clock inside one frame; the score, level, seed and future discs outside it, struck out",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={CELLS} />
      </ArtBoard>

      <circle cx={NEXT.cx} cy={NEXT.cy} r={NEXT.r} fill={`var(--color-disc-${NEXT.value})`} />
      <text
        x={NEXT.cx}
        y={NEXT.cy}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize={9}
        fontWeight={700}
        fontFamily={ART_MONO}
        fill={`var(--color-disc-${NEXT.value}-fg)`}
      >
        {NEXT.value}
      </text>

      <g>
        {PIPS.map((x, i) => (
          <rect
            key={x}
            x={x}
            y={11}
            width={6}
            height={8}
            rx={1.5}
            fill={i < PIPS_LEFT ? "var(--color-accent)" : "none"}
            stroke={i < PIPS_LEFT ? "none" : "var(--color-ink-4)"}
            strokeWidth={1}
          />
        ))}
      </g>

      <rect
        data-anim="frame"
        x={6}
        y={6}
        width={146}
        height={156}
        rx={6}
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={1.4}
      />

      <g data-anim="hidden">
        <rect
          x={166}
          y={22}
          width={142}
          height={124}
          rx={6}
          fill="none"
          stroke="var(--color-ink-4)"
          strokeWidth={1}
          strokeDasharray="4 4"
        />
        <g fill="none" stroke="var(--color-ink-4)" strokeWidth={1.2} strokeDasharray="3 3">
          <circle cx={OUT_X - 14} cy={54} r={9} />
          <circle cx={OUT_X + 14} cy={54} r={9} />
        </g>
        <g fontFamily={ART_MONO} fontSize={10} fill="var(--color-ink-4)" textAnchor="middle">
          {HIDDEN.map(({ label, y }) => (
            <text key={label} x={OUT_X} y={y}>
              {label}
            </text>
          ))}
        </g>
      </g>

      <g data-anim="strike" stroke="var(--color-ink-3)" strokeWidth={1.4} strokeLinecap="round">
        <line x1={OUT_X - 23} y1={54} x2={OUT_X + 23} y2={54} />
        {HIDDEN.map(({ label, y, half }) => (
          <line key={label} x1={OUT_X - half} y1={y - 3} x2={OUT_X + half} y2={y - 3} />
        ))}
      </g>

      <text x={12} y={174} fontFamily={ART_MONO} fontSize={9} fill="var(--color-accent)">
        public
      </text>

      <g className="tart-final" data-anim="pick">
        <rect
          x={BOARD.x + PICK.col * BOARD.cell}
          y={BOARD.y}
          width={BOARD.cell}
          height={BOARD.rows * BOARD.cell}
          fill="var(--color-accent-soft)"
        />
        <ArtRing col={PICK.col} row={PICK.row} />
      </g>
    </svg>
  );
}
