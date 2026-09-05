/**
 * Card art for `lifetime-objective/risk-calibration`: three numbers that were
 * baked into the reference search, drawn as three dials that can now be moved.
 * A small mark under each track is where the frozen search left it. On play the
 * death-penalty dial runs the whole length of its track and comes home to the
 * same mark, and its row goes quiet — nothing it can be set to changes a
 * decision. The other two step across their notches and stay there: depth to
 * four, chance samples to seven, one per disc value the game can deal.
 *
 * Server component. Motion lives in risk-calibration.css (transform and
 * opacity only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./risk-calibration.css";

const TRACK_X = 112;
const TRACK_W = 188;
const LABEL_X = 104;

/** Notch centres for a dial with `count` settings, evenly across the track. */
function notches(count: number): number[] {
  return Array.from({ length: count }, (_, i) => TRACK_X + ((i + 0.5) * TRACK_W) / count);
}

const DEPTH_NOTCH = notches(5);
const SAMPLE_NOTCH = notches(7);

const ROWS = [
  {
    key: "penalty",
    y: 40,
    label: "death penalty",
    /** No notches: a magnitude, swept and found flat. */
    ticks: [] as number[],
    frozen: 200,
    handle: 200,
  },
  {
    key: "depth",
    y: 90,
    label: "depth",
    ticks: DEPTH_NOTCH,
    frozen: DEPTH_NOTCH[2],
    handle: DEPTH_NOTCH[3],
  },
  {
    key: "samples",
    y: 140,
    label: "chance samples",
    ticks: SAMPLE_NOTCH,
    frozen: SAMPLE_NOTCH[4],
    handle: SAMPLE_NOTCH[6],
  },
];

/**
 * The setting each moved dial left, and the one it took. A setting that is
 * gone by the end of the cycle carries `rest: 0`, so the resting frame shows
 * only where the two dials ended.
 */
const READOUTS = [
  { key: "depth-was", x: DEPTH_NOTCH[2], y: 76, text: "3", tone: "var(--color-ink-3)", rest: 0 },
  { key: "depth-now", x: DEPTH_NOTCH[3], y: 76, text: "4", tone: "var(--color-accent)", rest: 1 },
  { key: "samples-was", x: SAMPLE_NOTCH[4], y: 126, text: "5", tone: "var(--color-ink-3)", rest: 0 },
  { key: "samples-now", x: SAMPLE_NOTCH[6], y: 126, text: "7", tone: "var(--color-accent)", rest: 1 },
];

function Row({ row }: { row: (typeof ROWS)[number] }) {
  return (
    <g
      className={`row row-${row.key}`}
      data-anim={row.key === "penalty" ? "settled" : undefined}
      opacity={row.key === "penalty" ? 0.45 : undefined}
    >
      <rect
        x={TRACK_X}
        y={row.y - 3}
        width={TRACK_W}
        height="6"
        rx="3"
        fill="var(--color-cell)"
        stroke="var(--color-rule-strong)"
        strokeWidth="0.9"
      />
      {row.ticks.length > 0 && (
        <path
          d={row.ticks.map((x) => `M${x.toFixed(2)},${row.y + 6}v4`).join("")}
          fill="none"
          stroke="var(--color-ink-4)"
          strokeWidth="1"
        />
      )}
      <rect
        className="fill"
        data-anim={`fill-${row.key}`}
        x={TRACK_X}
        y={row.y - 3}
        width={row.handle - TRACK_X}
        height="6"
        rx="3"
        fill="var(--color-accent-strong)"
      />
      <path
        d={`M${row.frozen},${row.y + 12}l3.4,5.4h-6.8z`}
        fill="none"
        stroke="var(--color-ink-4)"
        strokeWidth="1"
      />
      <g className="handle" data-anim={`move-${row.key}`}>
        <rect
          x={row.handle - 5}
          y={row.y - 9}
          width="10"
          height="18"
          rx="3"
          fill="var(--color-raised)"
          stroke="var(--color-ink-2)"
          strokeWidth="1.3"
        />
        <path
          d={`M${row.handle},${row.y - 5}v10`}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1.6"
          strokeLinecap="round"
        />
      </g>
      <text x={LABEL_X} y={row.y + 3.5} textAnchor="end" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        {row.label}
      </text>
    </g>
  );
}

export function RiskCalibrationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-risk-calibration",
        "Three of the reference search's constants as dials: one sweeps its track and comes home, two step to a new setting",
        props,
      )}
    >
      {ROWS.map((row) => (
        <Row key={row.key} row={row} />
      ))}
      {READOUTS.map((readout) => (
        <text
          key={readout.key}
          data-anim={readout.key}
          opacity={readout.rest}
          x={readout.x}
          y={readout.y}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize="11"
          fontWeight={700}
          fill={readout.tone}
        >
          {readout.text}
        </text>
      ))}
      <g className="tart-final" data-anim="caption">
        <text x="160" y="172" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          one constant at its stop, two with room
        </text>
      </g>
    </svg>
  );
}
