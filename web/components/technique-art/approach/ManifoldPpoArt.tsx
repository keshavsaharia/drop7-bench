/**
 * Card art for `ntuple-rl/manifold-ppo`: the region of boards that long games
 * pass through, learned from a clairvoyant planner's trajectories, and a
 * public game walking inside it. On play the board steps from position to
 * position, reaches a point where two near-tied moves are open, and takes the
 * one that keeps it inside the region rather than the one that leaves.
 *
 * Server component. Motion lives in manifold-ppo.css (transform and opacity
 * only); the markup is the resting frame, with both branches drawn and the
 * board sitting on the one that stayed inside.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./manifold-ppo.css";

/** The learned region: the shape a board with a future has. */
const REGION =
  "M76,88 Q80,54 124,46 Q174,36 212,46 Q256,58 252,88 Q248,124 206,134 Q142,144 102,122 Q72,106 76,88 Z";
/** Positions the region was fitted from. */
const SEEDS: ReadonlyArray<readonly [number, number]> = [
  [98, 72],
  [132, 58],
  [172, 52],
  [208, 62],
  [236, 94],
  [150, 126],
];
/** One game's walk, all of it inside. */
const WALK: ReadonlyArray<readonly [number, number]> = [
  [108, 104],
  [134, 86],
  [160, 102],
  [188, 84],
];
const KEPT: readonly [number, number] = [216, 100];
const LEFT: readonly [number, number] = [278, 62];
const FORK = WALK[WALK.length - 1];
const WALK_PATH = WALK.map(([x, y], index) => `${index === 0 ? "M" : "L"}${x},${y}`).join("");

export function ManifoldPpoArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-manifold-ppo",
        "A learned region of long-lived boards with a game walking inside it, taking the near-tied move that stays in the region",
        props,
      )}
    >
      <path
        d={REGION}
        fill="var(--color-accent-soft)"
        stroke="var(--color-accent)"
        strokeWidth="1.4"
        strokeDasharray="5 4"
      />
      <g fill="var(--color-accent)" opacity="0.55">
        {SEEDS.map(([x, y]) => (
          <circle key={`${x}-${y}`} cx={x} cy={y} r="2.6" />
        ))}
      </g>
      <path d={WALK_PATH} fill="none" stroke="var(--color-ink-3)" strokeWidth="1.5" strokeLinejoin="round" />
      <g fill="var(--color-ink-3)">
        {WALK.map(([x, y]) => (
          <circle key={`${x}-${y}`} cx={x} cy={y} r="2.6" />
        ))}
      </g>
      <g data-anim="keep">
        <path
          d={`M${FORK[0]},${FORK[1]}L${KEPT[0]},${KEPT[1]}`}
          fill="none"
          stroke="var(--color-ink-2)"
          strokeWidth="1.8"
        />
      </g>
      <g data-anim="leave">
        <path
          d={`M${FORK[0]},${FORK[1]}L${LEFT[0]},${LEFT[1]}`}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth="1.4"
          strokeDasharray="4 3"
        />
        <path
          d={`M${LEFT[0] - 5},${LEFT[1] - 5}l10,10m0,-10l-10,10`}
          fill="none"
          stroke="var(--color-series-2)"
          strokeWidth="2"
          strokeLinecap="round"
        />
        <text
          x={LEFT[0]}
          y={LEFT[1] + 20}
          textAnchor="middle"
          fontFamily={ART_MONO}
          fontSize="9"
          fill="var(--color-series-2)"
        >
          outside
        </text>
      </g>
      <circle
        data-anim="ring"
        cx={KEPT[0]}
        cy={KEPT[1]}
        r="9.5"
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth="1.6"
      />
      <circle data-anim="walk" cx={KEPT[0]} cy={KEPT[1]} r="5" fill="var(--color-accent-strong)" />
      <text x="160" y="26" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        boards with a future
      </text>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="166" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          of two near-tied moves, the one that stays inside
        </text>
      </g>
    </svg>
  );
}
