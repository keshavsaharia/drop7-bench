/**
 * Card art for `value-policy-learning/denoised-value`: what a position is
 * worth, with the luck taken back out. One public board fans out into futures
 * that differ only in which discs arrive, and each future lands somewhere
 * along a lifetime axis. On play a single future goes out first and lands far
 * from the middle — the noisy label every earlier value model learned from —
 * then the rest of the fan opens, the scattered outcomes draw together, and
 * one averaged mark with its remaining spread is what survives.
 *
 * Server component. Motion lives in denoised-value.css (transform and opacity
 * only); the markup is the resting frame, so every dot is drawn where the
 * average leaves it and the stylesheet holds the scatter it came from.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, type BoardGeometry } from "../board";
import "./denoised-value.css";

/** A public position, a little smaller than the default board to leave the fan room. */
const POSITION: BoardGeometry = { x: 12, y: 32, cell: 16, cols: 7, rows: 7 };
const CELLS = ["0000000", "0000000", "0000000", "0000300", "0040500", "0680500", "0425352"].join("");

const FAN_X = 128;
const FAN_Y = 88;
const AXIS_X = 268;
const AXIS_TOP = 24;
const AXIS_BOTTOM = 154;

/** Where each simulated future ends up on the lifetime axis before averaging. */
const OUTCOME = [140, 46, 62, 34, 118, 88, 74];
/** Where the average leaves them: a small cloud about the mean, which is 80. */
const AVERAGED: readonly (readonly [number, number])[] = [
  [262, 74],
  [275, 84],
  [266, 79],
  [272, 88],
  [259, 82],
  [270, 72],
  [263, 87],
];
const MEAN = 80;
/** The spread the labels still carry once averaged, drawn as a soft band. */
const SPREAD = 12;

export function DenoisedValueArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-denoised-value",
        "One board fanning out into many played-out futures whose scattered lifetimes draw together into a single averaged mark",
        props,
      )}
    >
      <g className="position">
        <ArtBoard g={POSITION} />
        <ArtCells cells={CELLS} g={POSITION} />
      </g>

      <g className="fan" fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.2}>
        {OUTCOME.map((y, index) => (
          <path
            key={index}
            data-anim={index === 0 ? "path-first" : "path"}
            d={`M${FAN_X} ${FAN_Y}C176 ${FAN_Y} 214 ${y} 264 ${y}`}
            opacity={0.32}
          />
        ))}
      </g>

      <g className="axis">
        <line x1={AXIS_X} y1={AXIS_TOP} x2={AXIS_X} y2={AXIS_BOTTOM} stroke="var(--color-rule-strong)" />
        {[38, 70, 102, 134].map((y) => (
          <line key={y} x1={AXIS_X - 4} y1={y} x2={AXIS_X + 4} y2={y} stroke="var(--color-rule)" />
        ))}
        <text x={AXIS_X} y={16} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-3)">
          lifetime
        </text>
      </g>

      <g className="outcomes">
        {AVERAGED.map(([cx, cy], index) => (
          <circle
            key={index}
            className={`dot dot-${index}`}
            data-anim={index === 0 ? "dot-first" : "dot"}
            cx={cx}
            cy={cy}
            r={3.4}
            fill="var(--color-accent-strong)"
          />
        ))}
      </g>

      <g className="tart-final" data-anim="mean">
        <rect x={250} y={MEAN - SPREAD} width={38} height={2 * SPREAD} rx={4} fill="var(--color-accent-soft)" />
        <line
          x1={246}
          y1={MEAN}
          x2={292}
          y2={MEAN}
          stroke="var(--color-highlight)"
          strokeWidth={3}
          strokeLinecap="round"
        />
      </g>

      <g className="caption-a" data-anim="caption-a" opacity={0}>
        <text x={196} y={172} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          one future, one noisy label
        </text>
      </g>
      <g className="tart-final" data-anim="caption-b">
        <text x={196} y={172} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
          32 futures, one average
        </text>
      </g>
    </svg>
  );
}
