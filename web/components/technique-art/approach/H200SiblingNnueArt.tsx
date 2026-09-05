/**
 * Card art for `d4-long-outcome/h200-sibling-nnue`: one root from a locked
 * panel fans into its legal columns, and the network's output is a residual
 * over depth-4 search that starts at exactly zero — so every sibling begins
 * sitting on the depth-4 line, and the untrained network is the reference
 * policy. On play training pushes the siblings off the line and the pick
 * moves to another column.
 *
 * Server component. Motion lives in h200-sibling-nnue.css (transform,
 * opacity and stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtDisc, cellCenter, type BoardGeometry } from "../board";
import "./h200-sibling-nnue.css";

/** The root's seven columns, and the disc waiting above them. */
const ROOT: BoardGeometry = { x: 111, y: 26, cell: 14, cols: 7, rows: 1 };
const NEXT: BoardGeometry = { x: 153, y: 5, cell: 14, cols: 1, rows: 1 };
const NEXT_DISC = 5;
/** The depth-4 line: the residual's zero, where every sibling starts. */
const LINE_Y = 104;
const MARK_X = [64, 102, 140, 178, 216, 254, 292];
/** Where training leaves each sibling; the resting frame shows those offsets. */
const RESIDUAL = [14, -18, 5, -9, 20, -4, -26];
/** The column depth 4 picks, and the one the trained residual lifts above it. */
const D4_PICK = 3;
const DRIFT_PICK = 6;

const FAN = MARK_X.map((x, index) => {
  const [cx] = cellCenter(index, 0, ROOT);
  return `M${cx},${ROOT.y + ROOT.cell}L${x},${LINE_Y}`;
}).join("");

export function H200SiblingNnueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-h200-sibling-nnue",
        "One root fans into its seven columns, all sitting exactly on the depth-4 line, and training pushes them off it until a different column is on top",
        props,
      )}
    >
      <text x={12} y={20} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        panel of roots
      </text>
      <g className="deck" fill="var(--color-cell)" stroke="var(--color-rule)">
        <rect x={105} y={19} width={98} height={14} rx={3} />
        <rect x={108} y={22} width={98} height={14} rx={3} />
      </g>
      <ArtDisc value={NEXT_DISC} col={0} row={0} g={NEXT} />
      <ArtBoard g={ROOT} />
      <path
        className="fan"
        data-anim="fan"
        d={FAN}
        fill="none"
        stroke="var(--color-ink-3)"
        strokeWidth={1}
        strokeDasharray={150}
      />
      <line x1={52} y1={LINE_Y} x2={306} y2={LINE_Y} stroke="var(--color-ink-2)" strokeWidth={1.4} />
      <text x={12} y={LINE_Y + 4} fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        depth 4
      </text>
      {MARK_X.map((x, index) => (
        <circle
          key={x}
          className="sibling"
          data-anim={`sib-${index}`}
          cx={x}
          cy={LINE_Y + RESIDUAL[index]}
          r={5.5}
          fill="var(--color-accent)"
        />
      ))}
      <circle
        className="pick-a"
        data-anim="pick-a"
        cx={MARK_X[D4_PICK]}
        cy={LINE_Y}
        r={10}
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth={1.6}
        opacity={0}
      />
      <g className="tart-final" data-anim="pick-b">
        <circle
          cx={MARK_X[DRIFT_PICK]}
          cy={LINE_Y + RESIDUAL[DRIFT_PICK]}
          r={10}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1.6}
        />
      </g>
      <g className="caption-a" data-anim="caption-a" opacity={0}>
        <text x={160} y={166} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          untrained, it is exactly depth 4
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x={160} y={166} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          training moves the ranking off it
        </text>
      </g>
    </svg>
  );
}
