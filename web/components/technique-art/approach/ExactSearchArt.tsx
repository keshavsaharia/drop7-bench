/**
 * Card art for `heuristic-search/exact-search`: nothing is shortlisted and
 * nothing is left half-searched, three times over. Three members hang the same
 * complete tree from their own root — every legal column at the node row, and
 * under it a solid block of the plies below, filled edge to edge rather than
 * sampled down to a handful. On play the three blocks complete one after
 * another, each casts its vote, and the column two of them agree on is kept.
 *
 * Server component. Motion lives in exact-search.css (transform and opacity
 * only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./exact-search.css";

/** Three members of the ensemble, differing only in their chance-sampling salt. */
const MEMBER_X = [72, 160, 248];
const COLUMNS = 7;

const ROOT_Y = 18;
const ROOT_SIZE = 15;
const NODE_Y = 58;
const BLOCK_Y = 64;
const BLOCK_H = 38;
const BLOCK_HALF = 34;
const COMB_Y = BLOCK_Y + BLOCK_H;
const COMB_H = 7;
const COMB_TICKS = 15;

const SLOT_Y = 124;
const SLOT_W = 16;
const SLOT_H = 13;
const SLOT_X0 = 98;
const SLOT_STEP = 18;
/** Two members name this column; the third names another, so this one is kept. */
const MAJORITY = 3;
const VOTES = [MAJORITY, MAJORITY, 5];
/** The two agreeing votes land side by side, so the majority is two marks. */
const VOTE_OFFSET = [-4, 4, 0];

function slotCentre(index: number): number {
  return SLOT_X0 + index * SLOT_STEP + SLOT_W / 2;
}

function nodeX(cx: number, column: number): number {
  return cx - 30 + column * 10;
}

function memberEdges(cx: number): string {
  return Array.from({ length: COLUMNS }, (_, j) => `M${cx},${ROOT_Y + ROOT_SIZE}L${nodeX(cx, j)},${NODE_Y - 3}`).join("");
}

function memberDividers(cx: number): string {
  return Array.from(
    { length: COLUMNS - 1 },
    (_, k) => `M${(cx - BLOCK_HALF + ((k + 1) * 2 * BLOCK_HALF) / COLUMNS).toFixed(2)},${BLOCK_Y}v${BLOCK_H}`,
  ).join("");
}

function memberComb(cx: number): string {
  return Array.from(
    { length: COMB_TICKS },
    (_, k) => `M${(cx - BLOCK_HALF + (k * 2 * BLOCK_HALF) / (COMB_TICKS - 1)).toFixed(2)},${COMB_Y}v${COMB_H}`,
  ).join("");
}

export function ExactSearchArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-exact-search",
        "Three complete searches side by side, each expanding every legal column at every ply, voting on one column",
        props,
      )}
    >
      {MEMBER_X.map((cx) => (
        <g key={cx} className="member">
          <path d={memberEdges(cx)} fill="none" stroke="var(--color-ink-3)" strokeWidth="1" />
          <rect
            x={cx - ROOT_SIZE / 2}
            y={ROOT_Y}
            width={ROOT_SIZE}
            height={ROOT_SIZE}
            rx="3"
            fill="var(--color-raised)"
            stroke="var(--color-ink-2)"
            strokeWidth="1.2"
          />
          {Array.from({ length: COLUMNS }, (_, j) => (
            <circle key={j} cx={nodeX(cx, j)} cy={NODE_Y} r="2.4" fill="var(--color-ink-3)" />
          ))}
        </g>
      ))}
      {MEMBER_X.map((cx, index) => (
        <g key={cx} className="block" data-anim={`block-${index + 1}`}>
          <rect
            x={cx - BLOCK_HALF}
            y={BLOCK_Y}
            width={BLOCK_HALF * 2}
            height={BLOCK_H}
            rx="2"
            fill="var(--color-ink-4)"
            fillOpacity="0.34"
            stroke="var(--color-ink-3)"
            strokeWidth="0.9"
          />
          <path d={memberDividers(cx)} fill="none" stroke="var(--color-ink-3)" strokeWidth="0.7" opacity="0.7" />
          <path d={memberComb(cx)} fill="none" stroke="var(--color-ink-3)" strokeWidth="0.9" />
        </g>
      ))}
      <g className="slots" fill="var(--color-cell)" stroke="var(--color-rule-strong)" strokeWidth="0.9">
        {Array.from({ length: COLUMNS }, (_, j) => (
          <rect key={j} x={SLOT_X0 + j * SLOT_STEP} y={SLOT_Y} width={SLOT_W} height={SLOT_H} rx="2" />
        ))}
      </g>
      <g className="votes" data-anim="votes">
        <path
          d={MEMBER_X.map(
            (cx, index) => `M${cx},${COMB_Y + COMB_H + 3}L${slotCentre(VOTES[index]) + VOTE_OFFSET[index]},116`,
          ).join("")}
          fill="none"
          stroke="var(--color-ink-3)"
          strokeWidth="1.1"
        />
        <path
          d={VOTES.map(
            (slot, index) => `M${slotCentre(slot) + VOTE_OFFSET[index] - 3.4},116h6.8l-3.4,5.5z`,
          ).join("")}
          fill="var(--color-ink-2)"
        />
      </g>
      <g className="kept" data-anim="kept">
        <rect
          x={SLOT_X0 + MAJORITY * SLOT_STEP}
          y={SLOT_Y}
          width={SLOT_W}
          height={SLOT_H}
          rx="2"
          fill="var(--color-accent-strong)"
          stroke="var(--color-accent)"
          strokeWidth="1.2"
        />
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x="8" y="16">
          3 members
        </text>
        <text x="228" y="134">
          vote
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="158" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          every column, every ply, three times
        </text>
      </g>
    </svg>
  );
}
