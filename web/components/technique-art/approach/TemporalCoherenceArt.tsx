/**
 * Card art for `ntuple-rl/temporal-coherence`: the step that overshot, and the
 * clock the tables now hang off. Three windows over the board — a vertical
 * four, a horizontal four and a 2x2 square — all cover the same cell, so all
 * three read the same weight; the bank they write into is the one the rise
 * clock selects. On play the naive step runs far past the amount that was
 * asked for, and the corrected step, counting each weight once, lands exactly
 * on it.
 *
 * Server component. Motion lives in temporal-coherence.css (transform, opacity
 * and one dash offset); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import { ArtBoard, ArtCells, ArtRing } from "../board";
import "./temporal-coherence.css";

/** A late board; the three windows meet over the disc in the bottom corner. */
const CELLS =
  "0000000" + "0000002" + "0005416" + "0032685" + "0147363" + "2635142" + "5127463";

/** The three placements that all contain cell (6, 4). */
const WINDOWS = [
  { key: "tall", x: 124, y: 44, w: 18, h: 72, colour: "var(--color-accent)" },
  { key: "square", x: 106, y: 80, w: 36, h: 36, colour: "var(--color-series-3)" },
  { key: "wide", x: 70, y: 98, w: 72, h: 18, colour: "var(--color-series-2)" },
];

/** Each window's line out of the board, into the one weight all three share. */
const LINKS = [
  { key: "tall", d: "M142 50L176 64", colour: "var(--color-accent)" },
  { key: "square", d: "M142 88L176 64", colour: "var(--color-series-3)" },
  { key: "wide", d: "M142 110L176 64", colour: "var(--color-series-2)" },
];

/** One bank of tables for each position in the five-drop rise cycle. */
const BANK_X = 176;
const BANK_W = 124;
const BANK_Y = [36, 48, 60, 72, 84];
const LIVE = 2;

/** The step gauge: the track ends exactly where the update asked to move to. */
const GAUGE_Y = 110;
const GAUGE_H = 12;
const ASKED_X = 268;

export function TemporalCoherenceArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-temporal-coherence",
        "Three overlapping windows reading one shared weight in the table bank the rise clock selects, with a step that overshoots what was asked for and is then corrected to land on it",
        props,
      )}
    >
      <ArtBoard />
      <ArtCells cells={CELLS} />
      <g className="windows" data-anim="windows" fill="none" strokeWidth={1.6}>
        {WINDOWS.map((w) => (
          <rect key={w.key} x={w.x} y={w.y} width={w.w} height={w.h} rx={3} stroke={w.colour} />
        ))}
      </g>
      <ArtRing col={6} row={4} data-anim="shared" />
      <g className="links" data-anim="links" fill="none" strokeWidth={1.4} strokeDasharray={60}>
        {LINKS.map((link) => (
          <path key={link.key} d={link.d} stroke={link.colour} />
        ))}
      </g>
      <text x={BANK_X} y={30} fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-3)">
        rise phase
      </text>
      <g className="banks">
        {BANK_Y.map((y) => (
          <rect
            key={y}
            x={BANK_X}
            y={y}
            width={BANK_W}
            height={8}
            rx={2}
            fill="var(--color-cell)"
            stroke="var(--color-rule)"
          />
        ))}
      </g>
      <rect
        data-anim="bank"
        x={BANK_X}
        y={BANK_Y[LIVE]}
        width={BANK_W}
        height={8}
        rx={2}
        fill="var(--color-accent)"
      />
      <text x={BANK_X} y={102} fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-3)">
        step
      </text>
      <rect
        x={BANK_X}
        y={GAUGE_Y}
        width={ASKED_X - BANK_X}
        height={GAUGE_H}
        rx={2}
        fill="var(--color-cell)"
        stroke="var(--color-rule)"
      />
      <rect
        className="gauge-bar"
        data-anim="overshoot"
        x={BANK_X}
        y={GAUGE_Y}
        width={128}
        height={GAUGE_H}
        rx={2}
        fill="var(--color-series-2)"
        opacity={0}
      />
      <rect
        className="gauge-bar"
        data-anim="corrected"
        x={BANK_X}
        y={GAUGE_Y}
        width={ASKED_X - BANK_X}
        height={GAUGE_H}
        rx={2}
        fill="var(--color-accent-strong)"
      />
      <path
        d={`M${ASKED_X} 102V140`}
        stroke="var(--color-ink-2)"
        strokeWidth={1.2}
        strokeDasharray="3 3"
        fill="none"
      />
      <text x={ASKED_X} y={152} textAnchor="middle" fontSize={9} fontFamily={ART_MONO} fill="var(--color-ink-2)">
        asked
      </text>
      <g className="tart-final" data-anim="lands">
        <circle cx={ASKED_X} cy={GAUGE_Y + GAUGE_H / 2} r={4} fill="var(--color-highlight)" />
      </g>
    </svg>
  );
}
