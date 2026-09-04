/**
 * Card art for evolutionary search: twelve candidates scattered over a fitness
 * landscape re-form tighter and higher over four generations, and the best
 * three are ringed after each generation settles.
 *
 * Server component. Motion lives in evolution.css (transform and opacity
 * only); the markup is the resting frame, so a reader without motion sees the
 * converged population and the `.tart-final` annotation.
 */
import "./evolution.css";

type ArtProps = {
  mode?: "hover" | "loop" | "once" | "static";
  title?: string;
  className?: string;
};

/** The landscape, sampled every 6 units from a two-peak function. */
const CURVE =
  "M16,145L22,148.9L28,150.2L34,148.1L40,146.1L46,146.9L52,148.4L58,145.6L64,136L70,123.2L76,113.4L82,109.9L88,110.5L94,110.9L100,110.5L106,112.5L112,119.8L118,130.2L124,137.8L130,138.6L136,134.6L142,131.1L148,131L154,132L160,129.7L166,122.9L172,115.1L178,110.2L184,107.4L190,102.2L196,91.2L202,76.7L208,65L214,60.2L220,61L226,63.3L232,65.5L238,70.3L244,81.1L250,96.7L256,111.3L262,119.8L268,122.6L274,124.5L280,129.5L286,136.2L292,140.8L298,141.4L304,141.4";

/** Resting (generation four) position of each candidate; earlier generations are keyframe offsets. */
const DOTS: ReadonlyArray<readonly [number, number]> = [
  [220.7, 65.6],
  [218.3, 66.3],
  [222.1, 65.8],
  [207.2, 71.4],
  [229.1, 70.1],
  [214.1, 64.2],
  [233.3, 69.7],
  [215.8, 65.6],
  [225.3, 66.4],
  [211, 67.3],
  [229.5, 67.2],
  [216.2, 63.4],
];

const GENERATIONS = ["gen 0", "gen 1", "gen 2", "gen 3", "gen 4"] as const;

export function EvolutionArt({ mode = "hover", title, className }: ArtProps) {
  return (
    <svg
      className={["tart", "tart--evolution", className].filter(Boolean).join(" ")}
      data-mode={mode}
      viewBox="0 0 320 180"
      role="img"
      aria-label={title ?? "Evolution: a population climbs a fitness landscape over four generations"}
    >
      <g className="landscape">
        <line x1="16" y1="156" x2="304" y2="156" stroke="var(--color-rule-strong)" strokeWidth="1" />
        <path d={CURVE} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.5" strokeLinejoin="round" />
      </g>
      <g className="population">
        {DOTS.map(([x, y], index) => (
          <circle
            key={index}
            className={`dot dot-${index}`}
            data-anim="dot"
            cx={x}
            cy={y}
            r="3.4"
            fill="var(--color-series-1)"
          />
        ))}
      </g>
      <g className="elite">
        {DOTS.slice(0, 3).map(([x, y], index) => (
          <circle
            key={index}
            className={`ring ring-${index}`}
            data-anim="ring"
            cx={x}
            cy={y}
            r="6.5"
            fill="none"
            stroke="var(--color-accent)"
            strokeWidth="1.5"
          />
        ))}
      </g>
      <g className="counter" fontFamily="var(--font-mono)" fontSize="10" fill="var(--color-ink-2)">
        {GENERATIONS.map((label, index) => (
          <text key={label} className={`gen gen-${index}`} data-anim="gen" x="16" y="22" opacity={index === 4 ? 1 : 0}>
            {label}
          </text>
        ))}
      </g>
      <text x="16" y="171" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
        fitness landscape
      </text>
      <g className="tart-final">
        <text x="304" y="22" textAnchor="end" fontFamily="var(--font-mono)" fontSize="9" fill="var(--color-ink-3)">
          top three ringed, cloud near the peak
        </text>
      </g>
    </svg>
  );
}
