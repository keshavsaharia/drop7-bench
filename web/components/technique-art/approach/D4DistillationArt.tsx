/**
 * Card art for `d4-long-outcome/d4-distillation`: the four plies of the
 * search folded into a model that answers in one step. On play the tree dims
 * from its deepest ply upward and each ply arrives as one layer of a folded
 * stack, which then names a column with no tree left to expand.
 *
 * Server component. Motion lives in d4-distillation.css (transform and
 * opacity only); the markup is the resting frame, where the tree survives
 * only as the ghost behind the stack it was compressed into.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./d4-distillation.css";

/** The tree: a root and four plies, each ply one node wider than the last. */
const ROOT_X = 64;
const ROW_Y = [28, 52, 76, 100, 124];
const SPACING = 20;
const NODE_R = 3.4;
const PLIES = [1, 2, 3, 4];

function nodeX(row: number, index: number): number {
  return ROOT_X + (index - row / 2) * SPACING;
}

/** The edges arriving at one ply from the row above it. */
function plyEdges(row: number): string {
  const parts: string[] = [];
  for (let i = 0; i < row; i += 1) {
    const from = `M${nodeX(row - 1, i)},${ROW_Y[row - 1] + NODE_R}`;
    parts.push(`${from}L${nodeX(row, i)},${ROW_Y[row] - NODE_R}`);
    parts.push(`${from}L${nodeX(row, i + 1)},${ROW_Y[row] - NODE_R}`);
  }
  return parts.join("");
}

/** The folded model: one layer per ply. */
const PLATE_X = 132;
const PLATE_W = 62;
const PLATE_H = 8;
const PLATE_Y = [70, 81, 92, 103];
const PLATE_MID = PLATE_X + PLATE_W / 2;

/** The answer: one column out of seven. */
const BAR_X = 216;
const BAR_STEP = 13;
const BAR_W = 8;
const BAR_Y = 80;
const BAR_H = 28;
const COLUMNS = [0, 1, 2, 3, 4, 5, 6];
const PICK = 3;
const PICK_X = BAR_X + PICK * BAR_STEP;
const PICK_MID = PICK_X + BAR_W / 2;

export function D4DistillationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-d4-distillation",
        "A four-ply search tree folded into a stack of four layers that names one column in a single step",
        props,
      )}
    >
      {PLIES.map((ply) => (
        <g key={ply} data-anim={`ply-${ply}`} opacity="0.3">
          <path d={plyEdges(ply)} fill="none" stroke="var(--color-ink-4)" strokeWidth="1" />
          {Array.from({ length: ply + 1 }, (_, index) => (
            <circle
              key={index}
              cx={nodeX(ply, index)}
              cy={ROW_Y[ply]}
              r={NODE_R}
              fill="var(--color-raised)"
              stroke="var(--color-ink-3)"
              strokeWidth="1"
            />
          ))}
        </g>
      ))}
      <circle cx={ROOT_X} cy={ROW_Y[0]} r="5" fill="var(--color-ink-2)" />
      {PLATE_Y.map((y, index) => (
        <rect
          key={y}
          data-anim={`plate-${index + 1}`}
          x={PLATE_X}
          y={y}
          width={PLATE_W}
          height={PLATE_H}
          rx="2"
          fill="var(--color-accent-soft)"
          stroke="var(--color-accent)"
          strokeWidth="1"
        />
      ))}
      <path d="M198,92h9" fill="none" stroke="var(--color-ink-3)" strokeWidth="1.2" />
      <path d="M213,92l-7,-4v8z" fill="var(--color-ink-3)" />
      {COLUMNS.map((index) => (
        <rect
          key={index}
          x={BAR_X + index * BAR_STEP}
          y={BAR_Y}
          width={BAR_W}
          height={BAR_H}
          rx="2"
          fill="var(--color-cell)"
          stroke="var(--color-rule-strong)"
          strokeWidth="1"
        />
      ))}
      <g data-anim="pick">
        <rect x={PICK_X} y={BAR_Y} width={BAR_W} height={BAR_H} rx="2" fill="var(--color-accent)" />
        <path d={`M${PICK_MID},56v13`} fill="none" stroke="var(--color-highlight)" strokeWidth="1.6" />
        <path d={`M${PICK_MID},76l-4.5,-7h9z`} fill="var(--color-highlight)" />
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={ROOT_X} y="16" textAnchor="middle">
          depth 4
        </text>
        <text x={PLATE_MID} y="126" textAnchor="middle">
          one step
        </text>
        <text x={PICK_MID} y="126" textAnchor="middle">
          column
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="166" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          four plies folded into one evaluation
        </text>
      </g>
    </svg>
  );
}
