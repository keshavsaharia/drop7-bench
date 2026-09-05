/**
 * Card art for `fair-expectimax/cem`: the cross-entropy method drawn as the
 * thing that actually changes, the sampling distribution over one coefficient.
 * The first frame is the wide bell and the population drawn from it; on play
 * the samples nearest the top are kept as elites and the distribution is
 * refitted to them, twice, so the three bells nest inside one another. The
 * resting frame is all three curves at once, which is the narrowing itself
 * rather than a snapshot of it.
 *
 * Server component. Motion lives in cem.css (transform and opacity only); the
 * SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./cem.css";

/** Baseline of the coefficient axis, in the art's 320x180 user space. */
const AXIS_Y = 146;

/**
 * Three Gaussians over the same axis, sampled every 5 units and drawn as
 * polylines: mean 140 spread 46, mean 164 spread 32, mean 178 spread 20. Each
 * refit is narrower and taller than the one it replaces, so they nest.
 */
const BELL_WIDE =
  "M20,144.3L25,143.8L30,143.1L35,142.3L40,141.3L45,140.1L50,138.6L55,136.9L60,135L65,132.8L70,130.3L75,127.6L80,124.6L85,121.5L90,118.3L95,115L100,111.7L105,108.6L110,105.6L115,102.9L120,100.5L125,98.6L130,97.2L135,96.3L140,96L145,96.3L150,97.2L155,98.6L160,100.5L165,102.9L170,105.6L175,108.6L180,111.7L185,115L190,118.3L195,121.5L200,124.6L205,127.6L210,130.3L215,132.8L220,135L225,136.9L230,138.6L235,140.1L240,141.3L245,142.3L250,143.1L255,143.8L260,144.3L265,144.8L270,145.1L275,145.3L280,145.5L285,145.7L290,145.8L295,145.8L300,145.9";
const BELL_MID =
  "M20,146L25,146L30,146L35,146L40,146L45,145.9L50,145.9L55,145.8L60,145.7L65,145.5L70,145.2L75,144.7L80,144L85,143.1L90,141.7L95,139.9L100,137.6L105,134.7L110,131.1L115,126.8L120,121.9L125,116.5L130,110.7L135,104.9L140,99.2L145,94L150,89.7L155,86.4L160,84.5L165,84L170,85.1L175,87.6L180,91.3L185,96L190,101.4L195,107.2L200,113.1L205,118.7L210,123.9L215,128.6L220,132.6L225,135.9L230,138.6L235,140.7L240,142.3L245,143.5L250,144.3L255,144.9L260,145.3L265,145.6L270,145.7L275,145.8L280,145.9L285,146L290,146L295,146L300,146";
const BELL_NARROW =
  "M20,146L25,146L30,146L35,146L40,146L45,146L50,146L55,146L60,146L65,146L70,146L75,146L80,146L85,146L90,146L95,146L100,146L105,145.9L110,145.7L115,145.3L120,144.6L125,143.2L130,140.7L135,136.7L140,130.5L145,121.9L150,110.7L155,97.5L160,83.3L165,69.9L170,59.2L175,53.1L180,52.5L185,57.6L190,67.5L195,80.5L200,94.7L205,108.2L210,119.9L215,129L220,135.6L225,140.1L230,142.8L235,144.4L240,145.2L245,145.7L250,145.9L255,145.9L260,146L265,146L270,146L275,146L280,146L285,146L290,146L295,146L300,146";

/** Where the population fell on the axis, and which three were kept. */
const SAMPLES = [62, 88, 112, 133, 152, 172, 196, 224];
const ELITES = [152, 172, 196];

export function CemArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-cem",
        "A sampling distribution over one coefficient is refitted twice to its elite samples, each bell narrower than the last",
        props,
      )}
    >
      <line x1={20} y1={AXIS_Y} x2={300} y2={AXIS_Y} stroke="var(--color-rule-strong)" strokeWidth={1} />

      <path
        d={BELL_WIDE}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth={1.4}
        strokeDasharray="4 3"
      />

      <g fill="var(--color-ink-3)">
        {SAMPLES.map((x) => (
          <rect key={x} x={x - 1.2} y={AXIS_Y + 2} width={2.4} height={9} rx={1.2} />
        ))}
      </g>

      <g data-anim="elite" fill="var(--color-accent)">
        {ELITES.map((x) => (
          <rect key={x} x={x - 1.8} y={AXIS_Y + 2} width={3.6} height={13} rx={1.8} />
        ))}
      </g>

      <g data-anim="mid" fill="none" stroke="var(--color-ink-2)" strokeWidth={1.4}>
        <path d={BELL_MID} />
      </g>

      <g data-anim="narrow">
        <path d={BELL_NARROW} fill="var(--color-accent-soft)" stroke="var(--color-accent)" strokeWidth={1.8} />
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={20} y={172}>
          samples
        </text>
        <text x={174} y={172} textAnchor="middle" fill="var(--color-accent)">
          elite
        </text>
        <text x={300} y={172} textAnchor="end">
          coefficient
        </text>
      </g>

      <g className="tart-final" data-anim="refit">
        <text
          x={178}
          y={44}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-accent)"
        >
          refit
        </text>
      </g>
    </svg>
  );
}
