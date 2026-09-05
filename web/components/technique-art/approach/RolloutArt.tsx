/**
 * Card art for `heuristic-search/rollout`: the plain form of the idea. Three
 * candidate columns are ghosted onto one board, and each is rehearsed to the
 * right — twenty-five more moves, five rise cycles, under its own imagined
 * disc streams. On play the rehearsals draw themselves out, three die at a
 * rise and take a cross, and the candidate whose rehearsals all reached the
 * end keeps its ring while the other two dim: a column is judged by how the
 * rehearsals went, not by how the board looked afterwards.
 *
 * Server component. Motion lives in rollout.css (stroke-dashoffset and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtRing, type BoardGeometry } from "../board";
import "./rollout.css";

/** A slightly tighter board than the default, to leave the field its width. */
const G: BoardGeometry = { x: 10, y: 40, cell: 16, cols: 7, rows: 7 };

const CELLS = "0000000" + "0000000" + "0000000" + "0000010" + "0400580" + "0638247" + "2195632";

/** The visible next disc, ghosted into every column being tried. */
const NEXT = 3;

/** The three columns tried, the row the next disc lands on, and their lane. */
const CANDIDATES = [
  { col: 2, row: 4, y: 60 },
  { col: 4, row: 3, y: 100 },
  { col: 6, row: 4, y: 140 },
];

/** The lane whose rehearsals all survive, and so the column that gets played. */
const BEST = 1;

const FIELD_X = 130;
const FIELD_END = 298;
/** One tick per rise: twenty-five moves is five rise cycles. */
const RISES = [164, 197, 231, 264, 298];

interface Rehearsal {
  /** Index into CANDIDATES. */
  lane: number;
  /** Distance from the lane's centre line. */
  offset: number;
  /** Where the rehearsal stopped; short of FIELD_END means the branch died. */
  end: number;
  wobble: readonly number[];
}

const REHEARSALS: readonly Rehearsal[] = [
  { lane: 0, offset: -8, end: 231, wobble: [0, -3, 2, -4, 3, -2, 4, -3, 1, -4] },
  { lane: 0, offset: 0, end: FIELD_END, wobble: [0, 3, -2, 4, -3, 2, -4, 3, -1, 4] },
  { lane: 0, offset: 8, end: FIELD_END, wobble: [0, -2, 4, -3, 1, -4, 3, -2, 4, -1] },
  { lane: 1, offset: -8, end: FIELD_END, wobble: [0, 2, -3, 4, -2, 3, -4, 2, -3, 4] },
  { lane: 1, offset: 0, end: FIELD_END, wobble: [0, -4, 3, -1, 4, -3, 2, -4, 3, -2] },
  { lane: 1, offset: 8, end: FIELD_END, wobble: [0, 3, -4, 2, -3, 4, -1, 3, -4, 2] },
  { lane: 2, offset: -8, end: 197, wobble: [0, -3, 4, -2, 3, -4, 1, -3, 4, -2] },
  { lane: 2, offset: 0, end: 264, wobble: [0, 4, -3, 2, -4, 1, -3, 4, -2, 3] },
  { lane: 2, offset: 8, end: FIELD_END, wobble: [0, -2, 3, -4, 2, -1, 4, -3, 2, -4] },
];

const STEPS = 10;

/** One rehearsal as a polyline: a vertex every few moves, stopping where it stopped. */
function trace({ lane, offset, end, wobble }: Rehearsal): string {
  const base = CANDIDATES[lane].y + offset;
  const points = Array.from({ length: STEPS + 1 }, (_, i) => {
    const x = FIELD_X + ((end - FIELD_X) * i) / STEPS;
    const y = base + (i === 0 || i === STEPS ? 0 : wobble[i % wobble.length]);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });
  return `M${points.join("L")}`;
}

const RISE_LINES = RISES.map((x) => `M${x},44v112`).join("");

/** One lane's rehearsals: the traces, the crosses where a branch died, the survivors. */
function Lane({ lane }: { lane: number }) {
  const best = lane === BEST;
  const runs = REHEARSALS.filter((rehearsal) => rehearsal.lane === lane);
  return (
    <>
      <g className="tried">
        <ArtDisc value={NEXT} col={CANDIDATES[lane].col} row={CANDIDATES[lane].row} g={G} opacity={0.6} />
        <ArtRing col={CANDIDATES[lane].col} row={CANDIDATES[lane].row} g={G} />
      </g>
      {runs.map((rehearsal) => (
        <path
          key={`run-${rehearsal.offset}`}
          className={rehearsal.end < FIELD_END ? "run run-early" : "run run-full"}
          data-anim="run"
          d={trace(rehearsal)}
          fill="none"
          pathLength={1}
          strokeDasharray="1"
          stroke={best ? "var(--color-accent)" : "var(--color-ink-3)"}
          strokeWidth="1.2"
          strokeLinejoin="round"
        />
      ))}
      {runs.map((rehearsal) =>
        rehearsal.end < FIELD_END ? (
          <path
            key={`end-${rehearsal.offset}`}
            className="died"
            data-anim="died"
            d="M-3,-3l6,6M3,-3l-6,6"
            transform={`translate(${rehearsal.end + 5} ${CANDIDATES[lane].y + rehearsal.offset})`}
            fill="none"
            stroke="var(--color-ink-2)"
            strokeWidth="1.3"
          />
        ) : (
          <circle
            key={`end-${rehearsal.offset}`}
            className="alive"
            data-anim="alive"
            cx={FIELD_END + 6}
            cy={CANDIDATES[lane].y + rehearsal.offset}
            r="2.2"
            fill={best ? "var(--color-accent)" : "var(--color-ink-4)"}
          />
        ),
      )}
    </>
  );
}

export function RolloutArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-rollout",
        "Three candidate columns on one board, each rehearsed twenty-five moves further across five rises",
        props,
      )}
    >
      <ArtBoard g={G} />
      <ArtCells cells={CELLS} g={G} />
      <ArtDisc value={NEXT} col={3} row={-1} g={G} />
      <path d={RISE_LINES} fill="none" stroke="var(--color-rule-strong)" strokeWidth="1" strokeDasharray="2 4" />

      <g className="rest" data-anim="rest" opacity="0.42">
        <Lane lane={0} />
        <Lane lane={2} />
      </g>
      <g className="best" data-anim="best">
        <Lane lane={BEST} />
      </g>

      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={FIELD_X} y="20">
          3 streams each
        </text>
        <text x={FIELD_X} y="32">
          25 moves · 5 rises
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          play the column whose rehearsals lasted
        </text>
      </g>
    </svg>
  );
}
