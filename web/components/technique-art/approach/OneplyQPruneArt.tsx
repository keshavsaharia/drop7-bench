/**
 * Card art for `value-policy-learning/oneply-q-prune`: the root offers all
 * seven columns, a cheap one-ply value fills a gauge under each, and only the
 * three best are expanded — the other four are struck out before the deep
 * search ever touches them. On play the gauges rise, four branches are cut and
 * dim, and the expensive search fans out under the three that are left.
 *
 * The root keeps its full width; the pruning happens below it.
 *
 * Server component. Motion lives in oneply-q-prune.css (transform and opacity
 * only); the markup is the resting frame, so the four cut branches carry their
 * dimmed opacity there and the keyframes fade them down to it.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./oneply-q-prune.css";

const ROOT_X = 132;
const ROOT_Y = 20;
const ROOT_W = 20;
const ROOT_H = 14;
const ROOT_BOTTOM = ROOT_Y + ROOT_H;

/** One gauge per legal column. */
const SIB_X = [24, 60, 96, 132, 168, 204, 240];
const SIB_TOP = 52;
const SIB_H = 24;
const SIB_W = 14;
/** How high the cheap prior stands each column, as a share of its gauge. */
const PRIOR = [0.3, 0.7, 0.18, 0.92, 0.44, 0.8, 0.26];
/** The three the prior puts on top, and the four the search never expands. */
const KEPT = [1, 3, 5];
const CUT = [0, 2, 4, 6];

const GAUGE_BOTTOM = SIB_TOP + SIB_H - 1;
const GAUGE_SPAN = SIB_H - 2;

const WEDGE_TOP = 80;
const WEDGE_BOTTOM = 148;
const LEAF_Y = 156;

function edge(x: number): string {
  return `M${ROOT_X},${ROOT_BOTTOM}L${x},${SIB_TOP}`;
}

function gauge(index: number): { x: number; y: number; height: number } {
  const height = PRIOR[index] * GAUGE_SPAN;
  return { x: SIB_X[index] - SIB_W / 2 + 1, y: GAUGE_BOTTOM - height, height };
}

/** Two short ticks across a branch the prior has cut. */
function strike(x: number): string {
  const dx = x - ROOT_X;
  const dy = SIB_TOP - ROOT_BOTTOM;
  const length = Math.hypot(dx, dy);
  const px = (-dy / length) * 4;
  const py = (dx / length) * 4;
  return [0.44, 0.62]
    .map((t) => {
      const cx = ROOT_X + dx * t;
      const cy = ROOT_BOTTOM + dy * t;
      return `M${(cx - px).toFixed(1)},${(cy - py).toFixed(1)}L${(cx + px).toFixed(1)},${(cy + py).toFixed(1)}`;
    })
    .join("");
}

/** The deep search under a column the prior kept. */
function wedge(x: number): string {
  return `M${x - 5},${WEDGE_TOP}L${x - 24},${WEDGE_BOTTOM}L${x + 24},${WEDGE_BOTTOM}L${x + 5},${WEDGE_TOP}Z`;
}

export function OneplyQPruneArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-oneply-q-prune",
        "A root offering seven columns, a cheap value gauge under each, four branches struck out and only the three best expanded by the deep search",
        props,
      )}
    >
      <g className="wedges" data-anim="wedges" fill="var(--color-accent-soft)" stroke="var(--color-accent-strong)">
        {KEPT.map((index) => (
          <path key={index} d={wedge(SIB_X[index])} />
        ))}
      </g>
      <path
        data-anim="leaves"
        d={KEPT.map((index) => `M${SIB_X[index] - 24},${LEAF_Y}h48`).join("")}
        stroke="var(--color-ink-3)"
        strokeWidth={1.4}
        strokeDasharray="2 5"
        strokeLinecap="round"
      />
      <path
        d={KEPT.map((index) => edge(SIB_X[index])).join("")}
        stroke="var(--color-ink-2)"
        strokeWidth={1.2}
        fill="none"
      />
      <g className="cut" data-anim="cut" opacity={0.32}>
        <path d={CUT.map((index) => edge(SIB_X[index])).join("")} stroke="var(--color-ink-3)" strokeWidth={1.2} fill="none" />
        {CUT.map((index) => (
          <rect
            key={index}
            x={SIB_X[index] - SIB_W / 2}
            y={SIB_TOP}
            width={SIB_W}
            height={SIB_H}
            rx={3}
            fill="var(--color-cell)"
            stroke="var(--color-rule-strong)"
          />
        ))}
      </g>
      <g fill="var(--color-cell)" stroke="var(--color-rule-strong)">
        {KEPT.map((index) => (
          <rect key={index} x={SIB_X[index] - SIB_W / 2} y={SIB_TOP} width={SIB_W} height={SIB_H} rx={3} />
        ))}
      </g>
      <g className="fills" data-anim="fills-keep" fill="var(--color-accent)">
        {KEPT.map((index) => {
          const bar = gauge(index);
          return <rect key={index} x={bar.x} y={bar.y} width={SIB_W - 2} height={bar.height} rx={2} />;
        })}
      </g>
      <g className="fills" data-anim="fills-cut" fill="var(--color-ink-3)" opacity={0.3}>
        {CUT.map((index) => {
          const bar = gauge(index);
          return <rect key={index} x={bar.x} y={bar.y} width={SIB_W - 2} height={bar.height} rx={2} />;
        })}
      </g>
      <g data-anim="strikes" stroke="var(--color-ink-2)" strokeWidth={1.4} strokeLinecap="round">
        <path d={CUT.map((index) => strike(SIB_X[index])).join("")} />
      </g>
      <rect
        x={ROOT_X - ROOT_W / 2}
        y={ROOT_Y}
        width={ROOT_W}
        height={ROOT_H}
        rx={3}
        fill="var(--color-raised)"
        stroke="var(--color-accent)"
        strokeWidth={1.4}
      />
      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-3)">
        <text x={254} y={46}>
          pruned
        </text>
        <text x={254} y={72} fill="var(--color-ink-2)">
          prior
        </text>
        <text x={254} y={115} fill="var(--color-ink-2)">
          depth 4
        </text>
      </g>
    </svg>
  );
}
