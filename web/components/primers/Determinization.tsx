/**
 * Figures for the determinized-planning primer
 * (web/content/learn/techniques/determinization.mdx).
 *
 * Both draw the road-trip example from the page: three weather forecasts,
 * a plan solved inside each as if it were certain, and the trap (strategy
 * fusion) where the fused plan is credited with knowing at dawn which
 * forecast came true. Server components: inline SVG with CSS keyframes in
 * determinization.css on the data-anim elements. The animation runs by
 * default; under prefers-reduced-motion it is removed and the base styles
 * show the finished drawing. Only stroke-dashoffset, opacity and transform
 * animate, and no text moves.
 */
import "./determinization.css";

const MONO = "var(--font-mono)";

const LANES = [
  { y: 58, label: "forecast 1: pass open", day2: "pass 2 h", hours: "5 h" },
  { y: 118, label: "forecast 2: pass open", day2: "pass 2 h", hours: "5 h" },
  { y: 178, label: "forecast 3: pass closed", day2: "detour 5 h", hours: "8 h" },
];

/* -------------------------------------------------------------------------
 * 1. One start, three sampled futures, one plan solved inside each, and the
 *    average the planner reports.
 * ---------------------------------------------------------------------- */

export function DeterminizationThreeFutures({ caption }: { caption?: string }) {
  const startX = 42;
  const startY = 118;
  const lanePath = (y: number) => `M${startX} ${startY} L70 ${y} H400`;
  return (
    <figure className="fig fig--dz">
      <div className="fig-frame">
        <svg
          viewBox="0 0 560 250"
          role="img"
          aria-label="Three forecast lanes from one start, a route planned in each, and their average feeding one decision"
        >
          <rect x={startX - 8} y={startY - 8} width={16} height={16} rx={2} fill="var(--color-accent-strong)" />
          <text x={startX} y={startY + 26} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            start
          </text>

          {LANES.map((lane, k) => (
            <g key={lane.y}>
              <path d={lanePath(lane.y)} stroke="var(--color-rule-strong)" strokeWidth={1.5} fill="none" />
              <path
                data-anim={`dz-plan-${k + 1}`}
                d={lanePath(lane.y)}
                pathLength={100}
                strokeDasharray={100}
                stroke="var(--color-accent)"
                strokeWidth={2.5}
                strokeLinejoin="round"
                fill="none"
              />
              <text x={82} y={lane.y - 12} fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
                {lane.label}
              </text>
              <circle cx={150} cy={lane.y} r={5} fill="var(--color-series-1)" />
              <text x={150} y={lane.y + 18} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-2)">
                mountain 3 h
              </text>
              <circle cx={290} cy={lane.y} r={5} fill={k === 2 ? "var(--color-series-2)" : "var(--color-series-3)"} />
              <text x={290} y={lane.y + 18} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-2)">
                {lane.day2}
              </text>
              <text x={408} y={lane.y} dominantBaseline="central" fontSize={13} fontWeight={700} fontFamily={MONO} fill="var(--color-ink)">
                {lane.hours}
              </text>
              <path
                data-anim="dz-merge"
                d={`M436 ${lane.y} L470 118`}
                pathLength={100}
                strokeDasharray={100}
                stroke="var(--color-accent)"
                strokeWidth={1.5}
                fill="none"
              />
            </g>
          ))}

          <rect
            data-anim="dz-avg"
            x={478}
            y={96}
            width={72}
            height={44}
            rx={6}
            pathLength={100}
            strokeDasharray={100}
            fill="var(--color-accent-soft)"
            stroke="var(--color-accent)"
          />
          <text x={514} y={112} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-2)">
            mountain
          </text>
          <text x={514} y={130} textAnchor="middle" fontSize={13} fontWeight={700} fontFamily={MONO} fill="var(--color-accent)">
            6 h avg
          </text>
          <text x={514} y={166} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            coast 6.5 h
          </text>
          <path
            data-anim="dz-pick"
            d="M462 118 l8 -6 v12 z"
            fill="var(--color-accent)"
          />
        </svg>
      </div>
      <figcaption>
        {caption ??
          "The road trip solved one forecast at a time. In each lane the planner knows the weather, so it takes the pass when it will be open and the detour when it will be closed: five, five and eight hours. It averages them to six, beats the coast's six and a half, and picks the mountain."}
      </figcaption>
    </figure>
  );
}

/* -------------------------------------------------------------------------
 * 2. Strategy fusion: at dawn on day two the planner's three plans diverge
 *    on knowledge the driver will not have; one choice for all three costs
 *    more, and the decision flips.
 * ---------------------------------------------------------------------- */

function Panel({
  x,
  title,
  splitX,
  honest,
}: {
  x: number;
  title: string;
  splitX: number;
  honest: boolean;
}) {
  const ys = [70, 120, 170];
  const left = x + 30;
  const right = x + 236;
  return (
    <g>
      <text x={x + 16} y={22} fontSize={12} fontFamily={MONO} fill="var(--color-ink-2)">
        {title}
      </text>
      <path
        d={`M${splitX} 44 V196`}
        stroke="var(--color-ink-3)"
        strokeWidth={1}
        strokeDasharray="4 3"
        fill="none"
      />
      {ys.map((y, k) => {
        const closed = k === 2;
        const up = `M${left} ${y} H${splitX} L${splitX + 40} ${y - 12} H${right}`;
        const down = `M${left} ${y} H${splitX} L${splitX + 40} ${y + 12} H${right}`;
        const hook = `M${left} ${y} H${splitX} L${splitX + 40} ${y - 12} H${splitX + 72} L${right} ${y + 10}`;
        const d = honest ? (closed ? hook : up) : closed ? down : up;
        const label = honest ? (closed ? "closed 8 h" : "pass 2 h") : closed ? "detour 5 h" : "pass 2 h";
        const labelY = honest ? (closed ? y + 24 : y - 18) : closed ? y + 26 : y - 18;
        return (
          <g key={y}>
            <path d={`M${left} ${y} H${right}`} stroke="var(--color-rule)" strokeWidth={1} fill="none" />
            <path
              data-anim={`${honest ? "dz-honest" : "dz-fuse"}-${k + 1}`}
              d={d}
              pathLength={100}
              strokeDasharray={100}
              stroke={honest && closed ? "var(--color-series-7)" : "var(--color-accent)"}
              strokeWidth={2.5}
              strokeLinejoin="round"
              fill="none"
            />
            <text x={right - 2} y={labelY} textAnchor="end" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
              {label}
            </text>
          </g>
        );
      })}
      <text x={left} y={54} fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
        mountain
      </text>
      <text x={splitX} y={212} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
        dawn, day 2
      </text>
      <text x={splitX} y={226} textAnchor="middle" fontSize={10} fontFamily={MONO} fill="var(--color-ink-3)">
        sky not yet visible
      </text>
    </g>
  );
}

export function DeterminizationStrategyFusion({ caption }: { caption?: string }) {
  return (
    <figure className="fig fig--dz">
      <div className="fig-frame">
        <svg
          viewBox="0 0 560 270"
          role="img"
          aria-label="Left: the planner's three routes diverge at dawn on day two on knowledge the driver will not have. Right: one route for all three forecasts, and the coast wins."
        >
          <Panel x={0} title="what the planner credits" splitX={135} honest={false} />
          <text x={135} y={38} textAnchor="middle" dominantBaseline="central" fontSize={15} fontWeight={700} fontFamily={MONO} fill="var(--color-accent)">
            ?
          </text>
          <circle data-anim="dz-ghost" cx={135} cy={38} r={11} fill="none" stroke="var(--color-accent)" strokeWidth={1.5} strokeDasharray="3 2" />
          <text x={150} y={252} textAnchor="middle" fontSize={13} fontWeight={700} fontFamily={MONO} fill="var(--color-accent)">
            average 6 h
          </text>

          <path d="M280 20 V240" stroke="var(--color-rule)" strokeWidth={1} fill="none" />

          <Panel x={290} title="what a driver can do" splitX={425} honest={true} />
          <text x={400} y={252} textAnchor="middle" fontSize={13} fontWeight={700} fontFamily={MONO} fill="var(--color-ink-2)">
            mountain 7 h
          </text>
          <text x={500} y={252} textAnchor="middle" fontSize={13} fontWeight={700} fontFamily={MONO} fill="var(--color-ink)">
            coast 6.5 h
          </text>
          <rect
            data-anim="dz-choice"
            x={456}
            y={240}
            width={90}
            height={18}
            rx={4}
            fill="none"
            stroke="var(--color-series-3)"
            strokeWidth={1.5}
          />
        </svg>
      </div>
      <figcaption>
        {caption ??
          "Strategy fusion in the road-trip example. On the left, each forecast's plan turns a different way at dawn on day two, before the sky can be seen; the dashed question mark is the knowledge the average of six hours quietly assumes. On the right, one choice has to serve all three forecasts: taking the pass every time costs two, two and eight hours, seven on average, so the coast at six and a half is the better road."}
      </figcaption>
    </figure>
  );
}
