/**
 * Card art for `ntuple-rl/phase-blend`: two evaluators reading one board, and
 * a single coefficient weighing them. The hand-written rule sits on the left,
 * the learned table on the right, and the bar underneath is the value the two
 * make together. On play the coefficient walks its four settings and the
 * learned share of the bar grows with it, until the setting that won takes the
 * whole bar and the blend is no blend at all.
 *
 * Server component. Motion lives in phase-blend.css (transform, opacity and
 * one dash offset); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells } from "../board";
import "./phase-blend.css";

/** The one position both evaluators are asked about. */
const CELLS =
  "0000000" + "0000000" + "0000000" + "0010000" + "5060030" + "2047120" + "6385417";

/** The hand-written evaluator: a few terms, written out by a person. */
const RULE_ROWS = [
  { y: 44, w: 52 },
  { y: 55, w: 40 },
  { y: 66, w: 46 },
];

/** The learned evaluator: a table of weights, one cell per pattern. */
const TABLE_COLS = [0, 1, 2, 3];
const TABLE_ROWS = [0, 1, 2];

const RULE_X = 188;
const LEARNED_X = 272;
const TRACK_Y = 96;
const STOPS = [188, 216, 244, 272];
const MIX = { x: 152, y: 112, w: 156, h: 16 };

export function PhaseBlendArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-phase-blend",
        "A hand-written evaluator and a learned table reading the same board, with a coefficient sliding from one to the other and a value bar filling with the learned share",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={CELLS} />
      <g className="feed" data-anim="feed" fill="none" strokeDasharray={132}>
        <path d={`M144 88L${RULE_X} 78`} stroke="var(--color-ink-4)" strokeWidth={1.2} />
        <path d={`M144 88L${LEARNED_X} 78`} stroke="var(--color-ink-4)" strokeWidth={1.2} />
        <circle cx={144} cy={88} r={2.4} fill="var(--color-ink-4)" stroke="none" />
      </g>
      <g className="sources" fontSize={9} fontFamily={ART_MONO} textAnchor="middle">
        <text x={RULE_X} y={26} fill="var(--color-series-2)">
          heuristic
        </text>
        <text x={LEARNED_X} y={26} fill="var(--color-accent)">
          learned
        </text>
      </g>
      <rect x={152} y={32} width={72} height={44} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
      {RULE_ROWS.map((row) => (
        <rect key={row.y} x={160} y={row.y} width={row.w} height={5} rx={2} fill="var(--color-series-2)" />
      ))}
      <rect x={236} y={32} width={72} height={44} rx={4} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
      {TABLE_ROWS.map((row) =>
        TABLE_COLS.map((col) => (
          <rect
            key={`${col}-${row}`}
            x={244 + col * 15}
            y={42 + row * 11}
            width={12}
            height={7}
            rx={1}
            fill="var(--color-accent)"
            opacity={0.35 + 0.2 * ((col + row) % 3)}
          />
        )),
      )}
      <g className="track">
        <path
          d={`M${STOPS[0]} ${TRACK_Y}H${STOPS[3]}`}
          stroke="var(--color-rule-strong)"
          strokeWidth={2}
          strokeLinecap="round"
        />
        {STOPS.map((x) => (
          <path key={x} d={`M${x} ${TRACK_Y - 4}v8`} stroke="var(--color-ink-4)" strokeWidth={1.2} />
        ))}
      </g>
      <circle
        data-anim="knob"
        cx={STOPS[3]}
        cy={TRACK_Y}
        r={5.5}
        fill="var(--color-accent)"
        stroke="var(--color-bg)"
        strokeWidth={1.4}
      />
      <rect x={MIX.x} y={MIX.y} width={MIX.w} height={MIX.h} rx={3} fill="var(--color-series-2)" />
      <rect
        className="mix"
        data-anim="mix"
        x={MIX.x}
        y={MIX.y}
        width={MIX.w}
        height={MIX.h}
        rx={3}
        fill="var(--color-accent-strong)"
      />
      <text
        x={MIX.x + MIX.w / 2}
        y={144}
        textAnchor="middle"
        fontSize={9}
        fontFamily={ART_MONO}
        fill="var(--color-ink-3)"
      >
        value
      </text>
      <g className="tart-final" data-anim="frozen">
        <rect
          x={232}
          y={28}
          width={80}
          height={52}
          rx={5}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
      </g>
    </svg>
  );
}
