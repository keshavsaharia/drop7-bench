/**
 * Card art for `lifetime-objective/planner-distill`: the teacher is a plan,
 * not a search. Each guessed hidden board (the covered disc at the head of a
 * row) is solved five moves deep, and every one of those chains has to be
 * paid for at every decision. On play a marker walks each five-move plan and
 * is absorbed into a single evaluator, which then answers with a value for
 * every legal column at once.
 *
 * Server component. Motion lives in planner-distill.css (transform and
 * opacity only); the markup is the resting frame, with the plans standing and
 * the compressed call holding its column values.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./planner-distill.css";

/** Three guessed hidden boards, each with the same five-move window. */
const ROW_MID = [47, 81, 115];
const STEP_X = [28, 50, 72, 94, 116];
const STEP = 14;
const CHAIN_MID = 79;

/** The student: one call on the board that results from a move. */
const BOX = { x: 176, y: 70, w: 62, h: 44 };
const BOX_MID_X = BOX.x + BOX.w / 2;
const BOX_MID_Y = BOX.y + BOX.h / 2;
const NET_IN = [82, 92, 102];
const NET_HID = [87, 97];
const NET_EDGES = NET_IN.flatMap((y) => NET_HID.map((y2) => `M188,${y}L206,${y2}`))
  .concat(NET_HID.map((y) => `M206,${y}L224,92`))
  .join("");

/** The answer: a value for each of the seven columns, none of them missing. */
const VALUE_X = 250;
const VALUE_STEP = 8;
const VALUE_W = 6;
const VALUE_BASE = 120;
const VALUES = [16, 24, 11, 28, 20, 13, 22];
const VALUE_MID = VALUE_X + (VALUES.length - 1) * VALUE_STEP * 0.5 + VALUE_W / 2;

const STEP_LINKS = ROW_MID.flatMap((mid) =>
  STEP_X.slice(0, -1).map((x) => `M${x + STEP + 0.5},${mid}h3.5`),
).join("");
const STEP_HEADS = ROW_MID.flatMap((mid) =>
  STEP_X.slice(0, -1).map((x) => `M${x + STEP + 8},${mid}l-4,-2.5v5z`),
).join("");
const FUNNEL = ROW_MID.map(
  (mid) => `M132,${mid}C154,${mid} 158,${BOX_MID_Y} ${BOX.x},${BOX_MID_Y}`,
).join("");

export function PlannerDistillArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-planner-distill",
        "Three guessed boards each solved five moves deep, compressed into one call that values every column",
        props,
      )}
    >
      <path d={FUNNEL} fill="none" stroke="var(--color-ink-4)" strokeWidth="1" />
      {ROW_MID.map((mid) => (
        <g key={mid}>
          <circle cx="16" cy={mid} r="5.5" fill="var(--color-disc-gray)" />
          <circle cx="16" cy={mid} r="3.4" fill="var(--color-disc-gray-core)" />
          {STEP_X.map((x) => (
            <rect
              key={x}
              x={x}
              y={mid - STEP / 2}
              width={STEP}
              height={STEP}
              rx="3"
              fill="var(--color-cell)"
              stroke="var(--color-rule-strong)"
              strokeWidth="1"
            />
          ))}
        </g>
      ))}
      <path d={STEP_LINKS} fill="none" stroke="var(--color-ink-4)" strokeWidth="1" />
      <path d={STEP_HEADS} fill="var(--color-ink-4)" />
      {ROW_MID.map((mid, index) => (
        <circle
          key={mid}
          data-anim={`token-${index + 1}`}
          cx="35"
          cy={mid}
          r="4"
          fill="var(--color-accent-strong)"
          opacity="0"
        />
      ))}
      <rect
        x={BOX.x}
        y={BOX.y}
        width={BOX.w}
        height={BOX.h}
        rx="6"
        fill="var(--color-raised)"
        stroke="var(--color-rule-strong)"
        strokeWidth="1"
      />
      <path d={NET_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth="0.8" />
      <g fill="var(--color-cell)" stroke="var(--color-ink-3)" strokeWidth="1">
        {NET_IN.map((y) => (
          <circle key={`i${y}`} cx="188" cy={y} r="2.4" />
        ))}
        {NET_HID.map((y) => (
          <circle key={`h${y}`} cx="206" cy={y} r="2.4" />
        ))}
      </g>
      <g data-anim="call">
        <rect
          x={BOX.x}
          y={BOX.y}
          width={BOX.w}
          height={BOX.h}
          rx="6"
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="1.6"
        />
        <circle cx="224" cy="92" r="3" fill="var(--color-accent)" />
      </g>
      <g data-anim="values">
        {VALUES.map((height, index) => (
          <rect
            key={index}
            x={VALUE_X + index * VALUE_STEP}
            y={VALUE_BASE - height}
            width={VALUE_W}
            height={height}
            rx="1.5"
            fill="var(--color-accent)"
          />
        ))}
      </g>
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={CHAIN_MID} y="136" textAnchor="middle">
          five-move window
        </text>
        <text x={BOX_MID_X} y="136" textAnchor="middle">
          one call
        </text>
        <text x={VALUE_MID} y="136" textAnchor="middle">
          every column
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="166" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          the whole plan, compressed into one call
        </text>
      </g>
    </svg>
  );
}
