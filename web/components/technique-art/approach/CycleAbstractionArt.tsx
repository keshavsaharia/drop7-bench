/**
 * Card art for `heuristic-search/cycle-abstraction`: five drops compressed into
 * one decision. Between two rises there are exactly five drops, and each one
 * fans out over seven columns; on play those five fans are gathered under a
 * single bracket, and the whole cycle resolves to one of five ready-made plans
 * instead of five separate column choices.
 *
 * Server component. Motion lives in cycle-abstraction.css (transform, opacity
 * and stroke-dashoffset only); the markup is the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./cycle-abstraction.css";

const RISE_LEFT = 26;
const RISE_RIGHT = 294;
/** Five drops between the two rises, each fanning over the seven columns. */
const DROPS = [53, 106, 160, 214, 267];
const FAN_OFFSETS = [-18, -12, -6, 0, 6, 12, 18];
const FAN_TOP = 40;
const FAN_BOTTOM = 62;

function fan(cx: number): string {
  return FAN_OFFSETS.map((dx) => `M${cx},${FAN_TOP}L${cx + dx},${FAN_BOTTOM}`).join("");
}

const CHIP_W = 48;
const CHIP_Y = 110;
const CHIP_H = 18;
const CHIPS = [30, 83, 136, 189, 242];
/** The plan the cycle settles on, and how far the pointer travels to reach it. */
const CHOSEN = 1;
const POINTER_X = CHIPS[CHOSEN] + CHIP_W / 2;

export function CycleAbstractionArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-cycle-abstraction",
        "Five drops between two rises, each fanning over seven columns, gathered under one bracket that picks one of five plans",
        props,
      )}
    >
      <g stroke="var(--color-rule-strong)" strokeWidth={1} strokeDasharray="3 4" fill="none">
        <path d={`M${RISE_LEFT},22V100`} />
        <path d={`M${RISE_RIGHT},22V100`} />
      </g>
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)" textAnchor="middle">
        <text x={RISE_LEFT} y={14}>
          rise
        </text>
        <text x={RISE_RIGHT} y={14}>
          rise
        </text>
      </g>
      {DROPS.map((cx) => (
        <circle key={cx} cx={cx} cy={34} r={5} fill="var(--color-ink-4)" />
      ))}
      {DROPS.map((cx, index) => (
        <path
          key={cx}
          data-anim={`fan-${index + 1}`}
          d={fan(cx)}
          fill="none"
          stroke="var(--color-rule-strong)"
          strokeWidth={1}
        />
      ))}
      <path
        data-anim="bracket"
        d="M32 80V74H288V80"
        fill="none"
        stroke="var(--color-accent)"
        strokeWidth={1.6}
        strokeDasharray={268}
      />
      <g data-anim="pointer">
        <path
          d={`M${POINTER_X} 74V93`}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth={1.6}
        />
        <path
          d={`M${POINTER_X} 102L${POINTER_X - 4.5} 93L${POINTER_X + 4.5} 93Z`}
          fill="var(--color-accent)"
        />
      </g>
      <g data-anim="chips">
        {CHIPS.map((x) => (
          <rect
            key={x}
            x={x}
            y={CHIP_Y}
            width={CHIP_W}
            height={CHIP_H}
            rx={4}
            fill="none"
            stroke="var(--color-rule-strong)"
          />
        ))}
      </g>
      <g data-anim="pick">
        <rect
          x={CHIPS[CHOSEN]}
          y={CHIP_Y}
          width={CHIP_W}
          height={CHIP_H}
          rx={4}
          fill="var(--color-accent-strong)"
          stroke="var(--color-accent)"
        />
        <text
          x={POINTER_X}
          y={CHIP_Y + CHIP_H / 2}
          textAnchor="middle"
          dominantBaseline="central"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-accent-fg)"
        >
          build
        </text>
      </g>
      <g className="tart-final" data-anim="rest">
        <text
          x={160}
          y={152}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize={9}
          fill="var(--color-ink-2)"
        >
          one plan for the whole cycle
        </text>
      </g>
    </svg>
  );
}
