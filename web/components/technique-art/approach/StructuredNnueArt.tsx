/**
 * Card art for `value-policy-learning/structured-nnue`: what the network is
 * shown. A flat row of hand-chosen summaries drops away and the input takes
 * the shape of the board instead — one plane per cell position, stacked into
 * depth, so every cell carries its own vector for whichever of the ten tokens
 * is sitting there. One cell is ringed on the board and again through the
 * stack, and the three heads light at the end.
 *
 * Server component. Motion lives in structured-nnue.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing, cellCenter, type BoardGeometry } from "../board";
import "./structured-nnue.css";

/** A settled board: nothing on it is standing in a run equal to its own value. */
const BOARD_G: BoardGeometry = { x: 8, y: 44, cell: 17, cols: 7, rows: 5 };
const CELLS = "0000000" + "0000000" + "0065000" + "0856300" + "6546596";

/** The input layer, in the board's own shape, and the blank planes behind it. */
const PLANE: BoardGeometry = { x: 150, y: 52, cell: 12, cols: 7, rows: 5 };
const BEHIND = [18, 9].map((offset) => ({
  ...PLANE,
  x: PLANE.x + offset,
  y: PLANE.y - offset,
  opacity: offset === 9 ? 0.5 : 0.3,
}));

/** The cell whose own vector is followed through the stack. */
const RING = { col: 2, row: 2 };

/** The flat vector of hand-chosen summaries the shaped input replaces. */
const SUMMARY_BARS = Array.from({ length: 12 }, (_, index) => 150 + index * 7);
const SUMMARY_Y = PLANE.y + 22;

/** Three heads over the same trunk: remaining lifetime and two survival horizons. */
const HEADS = [96, 112, 128];
const HEAD_WIRES = HEADS.map((y) => `M236 108L264 ${y + 5}`).join("");

const [RING_X, RING_Y] = cellCenter(RING.col, RING.row, PLANE);

/** The colour a token contributes to the input: a disc's own, or the gray pair's. */
function tokenFill(token: number): string {
  if (token === 9) return "var(--color-disc-gray-core)";
  if (token === 8) return "var(--color-disc-gray)";
  return `var(--color-disc-${token})`;
}

/** One cell's token, drawn as a swatch rather than a disc: this is the input, not the board. */
function Tile({ token, col, row }: { token: number; col: number; row: number }) {
  const [cx, cy] = cellCenter(col, row, PLANE);
  return (
    <rect
      x={cx - 4}
      y={cy - 4}
      width={8}
      height={8}
      rx={1.5}
      fill={tokenFill(token)}
      stroke={token === 9 ? "var(--color-disc-gray)" : "none"}
      strokeWidth={1.4}
    />
  );
}

export function StructuredNnueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-structured-nnue",
        "A flat row of hand-chosen summaries gives way to an input shaped like the board itself, stacked into depth so every cell carries its own vector, feeding three heads",
        props,
      )}
    >
      <ArtBoard g={BOARD_G} />
      <ArtCells cells={CELLS} g={BOARD_G} />
      {HEADS.map((y) => (
        <rect key={y} x={266} y={y} width={42} height={10} rx={2} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
      ))}
      <text x={287} y={88} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        heads
      </text>
      <g data-anim="flat" opacity={0}>
        {SUMMARY_BARS.map((x) => (
          <rect key={x} x={x} y={SUMMARY_Y} width={5} height={18} rx={1} fill="var(--color-ink-4)" />
        ))}
      </g>
      <g data-anim="planes">
        <g data-anim="depth">
          {BEHIND.map((plane) => (
            <rect
              key={plane.x}
              x={plane.x}
              y={plane.y}
              width={plane.cols * plane.cell}
              height={plane.rows * plane.cell}
              rx={4}
              fill="var(--color-cell)"
              stroke="var(--color-rule-strong)"
              opacity={plane.opacity}
            />
          ))}
        </g>
        <ArtBoard g={PLANE} />
        <path d={HEAD_WIRES} fill="none" stroke="var(--color-rule-strong)" strokeWidth={1} />
        <g data-anim="tiles">
          {[...CELLS].map(Number).map((token, index) =>
            token === 0 ? null : (
              <Tile
                key={index}
                token={token}
                col={index % PLANE.cols}
                row={Math.floor(index / PLANE.cols)}
              />
            ),
          )}
        </g>
      </g>
      <g data-anim="link">
        <ArtRing col={RING.col} row={RING.row} g={BOARD_G} />
        <path
          d={`M${RING_X} ${RING_Y}L${RING_X + 18} ${RING_Y - 18}`}
          fill="none"
          stroke="var(--color-highlight)"
          strokeWidth={1}
          opacity={0.7}
        />
        <ArtRing col={RING.col} row={RING.row} g={PLANE} />
        {BEHIND.map((plane) => (
          <ArtRing key={plane.x} col={RING.col} row={RING.row} g={plane} opacity={0.6} />
        ))}
      </g>
      <g data-anim="heads">
        {HEADS.map((y) => (
          <rect key={y} x={267} y={y + 1} width={40} height={8} rx={1.5} fill="var(--color-accent)" fillOpacity={0.7} />
        ))}
      </g>
      <g data-anim="summary-caption" opacity={0}>
        <text x={160} y={156} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          hand-chosen summaries
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x={160} y={156} textAnchor="middle" fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
          a vector for every cell and token
        </text>
      </g>
    </svg>
  );
}
