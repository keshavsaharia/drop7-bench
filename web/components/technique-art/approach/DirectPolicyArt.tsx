/**
 * Card art for `value-policy-learning/direct-policy`: the policy is the whole
 * of the decision. One weighted sum per column hangs under the board, the
 * tallest column is played, and the search tree every sibling approach consults
 * sits beside it crossed out. On play the sums grow, the best column lights up,
 * the disc drops into it, and the pair it makes clears — while the tree is
 * never consulted.
 *
 * Server component. Motion lives in direct-policy.css (transform and opacity
 * only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtScore, columnX } from "../board";
import "./direct-policy.css";

/**
 * The position, with the cracked cover at column 4 drawn separately so it can
 * reveal. Checked against the rules: nothing is waiting to pop, and the two
 * dropped into column 5 lands beside the cover in a run of exactly two.
 */
const CELLS =
  "0000000" + "0000000" + "0000300" + "0020000" + "5080760" + "6063416" + "2415243";

/** The cracked cover the played disc reaches, and the number under it. */
const COVER = { col: 4, row: 3, value: 6 };

/** Where the disc goes, and what it is. */
const PLAY = { col: 5, row: 3, value: 2 };

/** One weighted sum per column, hung under the board it scores. */
const SUM_Y = 156;
const SUMS = [9, 5, 12, 7, 10, 16, 8];
const BEST = 5;

/** The tree this policy does not build: a root, three moves, six successors. */
const TREE_ROOT: readonly [number, number] = [236, 44];
const TREE_MID = [200, 236, 272];
const TREE_LEAF = [188, 212, 224, 248, 260, 284];
const TREE_EDGES = [
  ...TREE_MID.map((x) => `M${TREE_ROOT[0]},${TREE_ROOT[1]}L${x},80`),
  ...TREE_MID.flatMap((x, i) => [`M${x},80L${TREE_LEAF[i * 2]},112`, `M${x},80L${TREE_LEAF[i * 2 + 1]},112`]),
].join("");

export function DirectPolicyArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-direct-policy",
        "A weighted sum under each board column picks the column outright, beside a crossed-out search tree",
        props,
      )}
    >
      <ArtBoard>
        <ArtCells cells={CELLS} />
      </ArtBoard>

      <ArtGray data-anim="cover-out" opacity={0} cracked col={COVER.col} row={COVER.row} />
      <ArtDisc data-anim="cover-in" value={COVER.value} col={COVER.col} row={COVER.row} />

      <g data-anim="drop">
        <ArtDisc data-anim="clear" opacity={0} value={PLAY.value} col={PLAY.col} row={PLAY.row} />
      </g>
      <g className="tart-final" data-anim="score">
        <ArtScore depth={1} col={PLAY.col} row={PLAY.row} />
      </g>

      <g data-anim="sums" fill="var(--color-ink-3)">
        {SUMS.map((height, col) => (
          <rect key={col} x={columnX(col) - 5} y={SUM_Y} width={10} height={height} rx={2} />
        ))}
      </g>
      <rect
        data-anim="best"
        x={columnX(BEST) - 5}
        y={SUM_Y}
        width={10}
        height={SUMS[BEST]}
        rx={2}
        fill="var(--color-accent)"
      />

      <g className="tree" fill="var(--color-surface)" stroke="var(--color-ink-4)" strokeWidth={1}>
        <path d={TREE_EDGES} fill="none" />
        <circle cx={TREE_ROOT[0]} cy={TREE_ROOT[1]} r={4} />
        {TREE_MID.map((x) => (
          <circle key={x} cx={x} cy={80} r={3.4} />
        ))}
        {TREE_LEAF.map((x) => (
          <circle key={x} cx={x} cy={112} r={2.6} />
        ))}
      </g>
      <g data-anim="strike" stroke="var(--color-ink-2)" strokeWidth={1.6} strokeLinecap="round">
        <line x1={184} y1={34} x2={288} y2={122} />
        <line x1={288} y1={34} x2={184} y2={122} />
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={16} y={16}>
          weighted sum
        </text>
        <text x={236} y={148} textAnchor="middle">
          no search
        </text>
      </g>
    </svg>
  );
}
