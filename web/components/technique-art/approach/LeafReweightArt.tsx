/**
 * Card art for `lifetime-objective/leaf-reweight`: the frozen leaf's list of
 * board-scoring terms is left exactly as it is and only the number beside each
 * one is refitted. The terms are drawn as a fixed column of names; each weight
 * is a bar either side of a zero axis, with the frozen vector left behind as an
 * outline. On play a band passes down the list and every bar resizes inside its
 * outline — one of them crossing the axis — while not a single name moves.
 *
 * Server component. Motion lives in leaf-reweight.css (transform and opacity
 * only); the markup is the resting frame, which is the refitted vector.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./leaf-reweight.css";

/** Zero of the weight axis, in the art's 320x180 user space. */
const AXIS = 160;
const ROW_Y = [48, 72, 96, 120, 144];
const BAR_H = 10;

/**
 * Five of the leaf's terms, named as the evaluator names them. The bar lengths
 * are drawing sizes, not the weight values: what the art is about is that the
 * left column holds still while the right column is rewritten.
 */
const TERMS: { term: string; frozen: number; refit: number }[] = [
  { term: "open_columns", frozen: 22, refit: 48 },
  { term: "height_load", frozen: -40, refit: -18 },
  { term: "roughness", frozen: 14, refit: 36 },
  { term: "triple_twos", frozen: -26, refit: -52 },
  { term: "rise_pressure", frozen: 18, refit: -20 },
];

/** Left edge and width of a bar of signed length `v` hung off the axis. */
function bar(v: number): { x: number; width: number } {
  return { x: v >= 0 ? AXIS : AXIS + v, width: Math.abs(v) };
}

export function LeafReweightArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-leaf-reweight",
        "A fixed list of the leaf's scoring terms, with the weight beside each one resizing from the frozen vector to a refitted one",
        props,
      )}
    >
      <g className="pass" data-anim="pass" opacity={0}>
        <rect
          x={12}
          y={ROW_Y[0] - BAR_H}
          width={296}
          height={BAR_H * 2}
          rx={3}
          fill="var(--color-accent-soft)"
        />
        <path
          d={`M6 ${ROW_Y[0] - 5}L12 ${ROW_Y[0]}L6 ${ROW_Y[0] + 5}z`}
          fill="var(--color-accent)"
        />
      </g>
      <line
        x1={AXIS}
        y1={ROW_Y[0] - 18}
        x2={AXIS}
        y2={ROW_Y[ROW_Y.length - 1] + 14}
        stroke="var(--color-rule-strong)"
        strokeWidth={1}
      />
      <g className="frozen" fill="none" stroke="var(--color-ink-4)" strokeWidth={1.1}>
        {TERMS.map(({ term, frozen }, i) => (
          <rect key={term} {...bar(frozen)} y={ROW_Y[i] - BAR_H / 2} height={BAR_H} rx={2} />
        ))}
      </g>
      <g className="refit" fill="var(--color-accent)">
        {TERMS.map(({ term, refit }, i) => (
          <rect
            key={term}
            data-anim={`w${i + 1}`}
            {...bar(refit)}
            y={ROW_Y[i] - BAR_H / 2}
            height={BAR_H}
            rx={2}
          />
        ))}
      </g>
      <g className="terms" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        {TERMS.map(({ term }, i) => (
          <text key={term} x={16} y={ROW_Y[i]} dominantBaseline="central">
            {term}
          </text>
        ))}
      </g>
      <g className="key" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={16} y={24}>
          leaf terms
        </text>
        <rect
          x={196}
          y={17}
          width={12}
          height={8}
          rx={2}
          fill="none"
          stroke="var(--color-ink-4)"
          strokeWidth={1.1}
        />
        <text x={213} y={24}>
          frozen
        </text>
        <rect x={256} y={17} width={12} height={8} rx={2} fill="var(--color-accent)" />
        <text x={273} y={24}>
          refit
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text
          x={160}
          y={172}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          terms fixed, weights refitted
        </text>
      </g>
    </svg>
  );
}
