/**
 * Card art for `fair-expectimax/fair-policy`: the leaf evaluator's short list
 * of features, read off one real board. On play each term lights in turn and
 * rings the part of the board it reads — a structure about to fire, one a step
 * further away, a cracked cover, a column grown too tall — the roughness term
 * arrives switched off, and the five together resolve into one unlabelled
 * total bar.
 *
 * Server component. Motion lives in fair-policy.css (opacity and one scaled
 * bar only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing } from "../board";
import "./fair-policy.css";

/**
 * A spiky board of the kind the evaluator is allowed to build, since its
 * tidiness term is zero: a tall column four, a one-disc column two, a cracked
 * cover part way up column one. No disc on it is already clearing.
 */
const CELLS =
  "0000000" + "0000300" + "0600500" + "0401800" + "0603205" + "5905431" + "4362215";

/** The zero of the term ledger; positive terms run right, penalties left. */
const AXIS = 250;

/**
 * The features the leaf sums, each with the cell it is read from. Bar lengths
 * are relative weights of the formula, not a measured quantity, and carry no
 * number. Roughness is the term deliberately set to zero.
 */
const TERMS: {
  label: string;
  y: number;
  width: number;
  sign: number;
  ring: [number, number] | null;
}[] = [
  { label: "readiness", y: 42, width: 58, sign: 1, ring: [5, 5] },
  { label: "latent", y: 60, width: 30, sign: 1, ring: [6, 4] },
  { label: "cover", y: 78, width: 18, sign: 1, ring: [1, 5] },
  { label: "height", y: 96, width: 34, sign: -1, ring: [4, 1] },
  { label: "roughness", y: 114, width: 0, sign: 0, ring: null },
];

function barFill(sign: number): string {
  if (sign > 0) return "var(--color-accent-strong)";
  if (sign < 0) return "var(--color-series-7)";
  return "var(--color-ink-4)";
}

export function FairPolicyArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-fair-policy",
        "A board read by the leaf evaluator's short list of features, each ringing the cell it comes from, summed into one value",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={CELLS} />
      {TERMS.map((term, index) =>
        term.ring ? (
          <ArtRing key={term.label} col={term.ring[0]} row={term.ring[1]} data-anim={`term-${index + 1}`} />
        ) : null,
      )}
      <line x1={AXIS} y1={32} x2={AXIS} y2={124} stroke="var(--color-rule-strong)" strokeWidth={1} />
      <g className="terms">
        {TERMS.map((term, index) => (
          <g key={term.label} data-anim={`term-${index + 1}`}>
            <text
              x={152}
              y={term.y + 3.5}
              fontSize={9}
              fontFamily={ART_MONO}
              fill={term.sign === 0 ? "var(--color-ink-3)" : "var(--color-ink-2)"}
            >
              {term.label}
            </text>
            <rect
              x={term.sign < 0 ? AXIS - term.width : term.sign > 0 ? AXIS : AXIS - 2}
              y={term.y - 4.5}
              width={term.sign === 0 ? 4 : term.width}
              height={9}
              rx={2}
              fill={barFill(term.sign)}
            />
          </g>
        ))}
      </g>
      <line x1={152} y1={130} x2={312} y2={130} stroke="var(--color-rule-strong)" />
      <rect
        x={152}
        y={136}
        width={160}
        height={12}
        rx={3}
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
      />
      <rect data-anim="total" x={152} y={136} width={124} height={12} rx={3} fill="var(--color-accent-strong)" />
      <g className="tart-final" data-anim="caption">
        <text x={160} y={168} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          one board, one number
        </text>
      </g>
    </svg>
  );
}
