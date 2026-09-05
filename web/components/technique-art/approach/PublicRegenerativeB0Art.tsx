/**
 * Card art for `terminal-policy-iteration/public-regenerative-b0`: the budget
 * spent as a tournament rather than a survey. All seven columns get a short
 * look of five rise cycles, the three survivors get ten, the last one gets
 * fifteen, and every stage is run on a fresh panel of futures so a column that
 * got lucky cannot keep its luck. On play the field narrows twice and the last
 * challenger ends up alongside the reference, which ran the whole way.
 *
 * Server component. Motion lives in public-regenerative-b0.css
 * (stroke-dashoffset and opacity only); the markup is the resting frame.
 */
import { ART_MONO, artSvgProps } from "../FallbackArt";
import type { ArtProps } from "../registry";
import "./public-regenerative-b0.css";

const X0 = 48;
/** One tick per row rise; the stages end after five, ten and fifteen of them. */
const TICK = 17;
const GATES = [X0 + 5 * TICK, X0 + 10 * TICK, X0 + 15 * TICK];
const TICKS = Array.from({ length: 15 }, (_, i) => `M${X0 + (i + 1) * TICK},30v-4`).join("");
const TRACK_YS = [50, 62, 74, 86, 98, 110, 122];
const REFERENCE_Y = 140;
/** Which columns survive each stage; the rest are cut at that gate. */
const ROUND_2 = [1, 3, 5];
const WINNER = 3;
const STEPS = ["a", "b", "c"] as const;
const HORIZONS = ["25", "50", "75"];

export function PublicRegenerativeB0Art(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-public-regenerative-b0",
        "Seven columns run five rise cycles, three survivors run ten, one runs fifteen, each stage on a fresh panel, and the last one is set against the reference",
        props,
      )}
    >
      <g className="scale">
        <line x1={X0} y1="30" x2={GATES[2]} y2="30" stroke="var(--color-ink-4)" strokeWidth="1" />
        <path d={TICKS} fill="none" stroke="var(--color-ink-4)" strokeWidth="1" />
        {GATES.map((x) => (
          <line
            key={x}
            x1={x}
            y1="30"
            x2={x}
            y2="148"
            stroke="var(--color-rule-strong)"
            strokeWidth="1"
            strokeDasharray="3 4"
          />
        ))}
      </g>

      <g className="round-1">
        {TRACK_YS.map((y, index) =>
          ROUND_2.includes(index) ? (
            <line
              key={y}
              data-anim={`draw-${STEPS[index % STEPS.length]}`}
              x1={X0}
              y1={y}
              x2={GATES[0]}
              y2={y}
              pathLength={1}
              strokeDasharray="1"
              stroke="var(--color-ink-2)"
              strokeWidth="1.6"
            />
          ) : (
            <g key={y} data-anim="dim-1" opacity="0.35">
              <line
                data-anim={`draw-${STEPS[index % STEPS.length]}`}
                x1={X0}
                y1={y}
                x2={GATES[0]}
                y2={y}
                pathLength={1}
                strokeDasharray="1"
                stroke="var(--color-ink-2)"
                strokeWidth="1.6"
              />
            </g>
          ),
        )}
        <g className="cut-1" data-anim="cut-1" fill="none" stroke="var(--color-ink-4)" strokeWidth="1.3">
          {TRACK_YS.map((y, index) =>
            ROUND_2.includes(index) ? null : (
              <path key={y} d="M-3,-3l6,6M3,-3l-6,6" transform={`translate(${GATES[0] + 6} ${y})`} />
            ),
          )}
        </g>
      </g>

      <g className="round-2">
        {ROUND_2.map((index) =>
          index === WINNER ? (
            <line
              key={index}
              data-anim="draw-2"
              x1={GATES[0]}
              y1={TRACK_YS[index]}
              x2={GATES[1]}
              y2={TRACK_YS[index]}
              pathLength={1}
              strokeDasharray="1"
              stroke="var(--color-ink-2)"
              strokeWidth="1.6"
            />
          ) : (
            <g key={index} data-anim="dim-2" opacity="0.35">
              <line
                data-anim="draw-2"
                x1={GATES[0]}
                y1={TRACK_YS[index]}
                x2={GATES[1]}
                y2={TRACK_YS[index]}
                pathLength={1}
                strokeDasharray="1"
                stroke="var(--color-ink-2)"
                strokeWidth="1.6"
              />
            </g>
          ),
        )}
        <g className="cut-2" data-anim="cut-2" fill="none" stroke="var(--color-ink-4)" strokeWidth="1.3">
          {ROUND_2.filter((index) => index !== WINNER).map((index) => (
            <path
              key={index}
              d="M-3,-3l6,6M3,-3l-6,6"
              transform={`translate(${GATES[1] + 6} ${TRACK_YS[index]})`}
            />
          ))}
        </g>
      </g>

      <line
        data-anim="draw-3"
        x1={GATES[1]}
        y1={TRACK_YS[WINNER]}
        x2={GATES[2]}
        y2={TRACK_YS[WINNER]}
        pathLength={1}
        strokeDasharray="1"
        stroke="var(--color-accent)"
        strokeWidth="2.2"
      />

      <line
        data-anim="draw-a"
        x1={X0}
        y1={REFERENCE_Y}
        x2={GATES[2]}
        y2={REFERENCE_Y}
        pathLength={1}
        strokeDasharray="1"
        stroke="var(--color-highlight)"
        strokeWidth="1.6"
      />

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={X0 - 4} y="22" textAnchor="end">
          moves
        </text>
        {GATES.map((x, index) => (
          <text key={x} x={x} y="22" textAnchor="middle">
            {HORIZONS[index]}
          </text>
        ))}
        <text x={GATES[0] + 5} y="42">
          fresh panel
        </text>
        <text x={GATES[1] + 5} y="42">
          fresh panel
        </text>
        <text x={X0} y="154" fill="var(--color-highlight)">
          reference
        </text>
      </g>

      <g className="tart-final" data-anim="final">
        <path
          d={`M${GATES[2]},${TRACK_YS[WINNER]}h6v${REFERENCE_Y - TRACK_YS[WINNER]}h-6`}
          fill="none"
          stroke="var(--color-ink-2)"
          strokeWidth="1.2"
        />
        <text x="10" y="172" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          seven, then three, then one against the reference
        </text>
      </g>
    </svg>
  );
}
