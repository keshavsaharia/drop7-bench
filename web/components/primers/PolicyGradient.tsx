/**
 * Figures for the policy-gradient primer at
 * content/learn/techniques/policy-gradient.mdx.
 *
 * The toy is a two-lever machine, so every number in these figures is the
 * toy's own (a 50/50 start, a critic's estimate of 0.45, a clip ratio of 1.2).
 * No figure here shows a trained Drop7 policy's probabilities, because no
 * retained record publishes them; the seven-column panel shows the untrained
 * starting point, seven equal bars, and says so.
 *
 * Server components: SVG with CSS keyframes from ./policy-gradient.css on
 * elements marked data-anim. Bars move by transform, badges by opacity, and
 * labels swap by opacity; no text animates.
 */
import "./policy-gradient.css";
import type { ReactNode } from "react";

const SANS = "var(--font-sans)";
const MONO = "var(--font-mono)";
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const INK_4 = "var(--color-ink-4)";
const RULE = "var(--color-rule)";
const SURFACE = "var(--color-surface)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const S1 = "var(--color-series-1)";
const S2 = "var(--color-series-2)";
const S3 = "var(--color-series-3)";
const S7 = "var(--color-series-7)";

function Fig({ caption, children }: { caption: string; children: ReactNode }) {
  return (
    <figure className="fig">
      <div className="fig-frame">{children}</div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/* =========================================================================
 * 1. Probability bars: pull a lever, see a payout, move the bars.
 * ========================================================================= */

export function PolicyGradientBars() {
  const base = 200;
  const unit = 140;
  const barW = 44;
  const ax = 70;
  const bx = 150;
  const pA = 0.4;
  const pB = 0.6;
  const rings = [
    { id: "pg-ring-1", x: bx },
    { id: "pg-ring-2", x: ax },
    { id: "pg-ring-3", x: bx },
  ];
  const badges = [
    { id: "pg-badge-1", x: bx, text: "+1", fill: S3 },
    { id: "pg-badge-2", x: ax, text: "0", fill: INK_4 },
    { id: "pg-badge-3", x: bx, text: "+1", fill: S3 },
  ];
  const sevenX = 336;
  return (
    <Fig caption="Two levers and one parameter. The bars are the probabilities of pulling A and B, and they always add up to one. The loop starts at 50/50; a pull is marked with a ring, its payout with a badge, and each payout of 1 after pulling B raises B's bar and lowers A's. The right-hand panel is the Drop7 shape of the same thing: seven bars, one per column, equal before any training.">
      <svg
        className="primer-pg"
        viewBox="0 0 560 250"
        role="img"
        aria-label="Two probability bars for levers A and B change after sampled pulls and payouts; beside them seven equal bars for the seven Drop7 columns"
      >
        <text x={ax - 30} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the two-lever machine
        </text>
        <text x={ax - 30} y={38} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          A pays 1 with probability 0.3, B with 0.6; the player does not know that
        </text>
        <path d={`M${ax - 30},${base} H${bx + barW + 30}`} stroke={INK_2} strokeWidth={1} />
        <path d={`M${ax - 30},${base - 0.5 * unit} H${bx + barW + 30}`} stroke={INK_4} strokeWidth={1} strokeDasharray="3 3" />
        <text x={bx + barW + 34} y={base - 0.5 * unit + 3} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
          start 0.5
        </text>
        <text x={ax - 30} y={base - unit - 6} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
          probability of pulling
        </text>
        <rect data-anim="pg-bar-a" x={ax} y={base - pA * unit} width={barW} height={pA * unit} rx={3} fill={S1} />
        <rect data-anim="pg-bar-b" x={bx} y={base - pB * unit} width={barW} height={pB * unit} rx={3} fill={S2} />
        {rings.map((ring) => (
          <rect
            key={ring.id}
            data-anim={ring.id}
            x={ring.x - 5}
            y={base - unit - 4}
            width={barW + 10}
            height={unit + 8}
            rx={6}
            fill="none"
            stroke={ACCENT}
            strokeWidth={1.5}
            opacity={0}
          />
        ))}
        {badges.map((badge) => (
          <g key={badge.id} data-anim={badge.id} opacity={0}>
            <rect x={badge.x + barW / 2 - 14} y={base - unit - 30} width={28} height={18} rx={9} fill={badge.fill} />
            <text x={badge.x + barW / 2} y={base - unit - 17} textAnchor="middle" fontSize={10} fontFamily={MONO} fontWeight={700} fill={SURFACE}>
              {badge.text}
            </text>
          </g>
        ))}
        <text x={ax + barW / 2} y={base + 16} textAnchor="middle" fontSize={10} fontFamily={SANS} fill={INK_2}>
          lever A
        </text>
        <text x={bx + barW / 2} y={base + 16} textAnchor="middle" fontSize={10} fontFamily={SANS} fill={INK_2}>
          lever B
        </text>
        <text x={ax - 30} y={base + 36} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          ring: the lever pulled this time; badge: what it paid
        </text>
        {/* the Drop7 shape: seven columns, equal before training */}
        <text x={sevenX - 10} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the same shape in Drop7
        </text>
        <text x={sevenX - 10} y={38} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          seven bars, one per column, adding up to one
        </text>
        <path d={`M${sevenX - 10},${base} H${sevenX + 7 * 28 + 2}`} stroke={INK_2} strokeWidth={1} />
        {Array.from({ length: 7 }, (_, column) => (
          <g key={column}>
            <rect x={sevenX + column * 28} y={base - unit / 7} width={20} height={unit / 7} rx={3} fill={S1} />
            <text x={sevenX + column * 28 + 10} y={base + 16} textAnchor="middle" fontSize={9} fontFamily={MONO} fill={INK_3}>
              {column + 1}
            </text>
          </g>
        ))}
        <text x={sevenX - 10} y={base - unit / 7 - 10} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
          before training: 1/7 each
        </text>
        <text x={sevenX - 10} y={base + 36} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          column probabilities; the game only tells the learner one score
        </text>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 2. The critic as a baseline: only the surprise moves the policy.
 * ========================================================================= */

export function PolicyGradientBaseline() {
  const payouts = [1, 0, 0, 1, 1, 0, 1, 0];
  const estimate = 0.45;
  const x0 = 70;
  const step = 52;
  const y0 = 170;
  const unit = 110;
  const yOf = (v: number) => y0 - v * unit;
  const lineY = yOf(estimate);
  return (
    <Fig caption="Eight pulls, each paying 0 or 1, against the critic's running estimate of what a pull is worth, 0.45. A payout above the dashed line is a pleasant surprise and pushes the pulled lever's probability up by the gap; a payout below it pushes down. As pulls accumulate, the line itself drifts toward the running mean.">
      <svg
        className="primer-pg"
        viewBox="0 0 560 230"
        role="img"
        aria-label="Eight payouts of 0 or 1 plotted against a dashed baseline at 0.45, with upward arrows for payouts above it and downward arrows for payouts below it"
      >
        <text x={x0 - 40} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          reward only the surprise
        </text>
        <text x={x0 - 40} y={38} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          surprise = payout − the critic&apos;s estimate
        </text>
        <path d={`M${x0 - 20},${yOf(0)} H${x0 + 8 * step}`} stroke={RULE} strokeWidth={1} />
        <path d={`M${x0 - 20},${yOf(1)} H${x0 + 8 * step}`} stroke={RULE} strokeWidth={1} />
        <text x={x0 - 26} y={yOf(0) + 3} textAnchor="end" fontSize={9} fontFamily={MONO} fill={INK_3}>
          0
        </text>
        <text x={x0 - 26} y={yOf(1) + 3} textAnchor="end" fontSize={9} fontFamily={MONO} fill={INK_3}>
          1
        </text>
        <g data-anim="pg-critic">
          <path d={`M${x0 - 20},${lineY} H${x0 + 8 * step}`} stroke={ACCENT} strokeWidth={1.4} strokeDasharray="6 4" />
        </g>
        <g data-anim="pg-critic-label-a">
          <text x={x0 + 8 * step + 6} y={lineY + 3} fontSize={9} fontFamily={MONO} fill={ACCENT}>
            estimate 0.45
          </text>
        </g>
        <g data-anim="pg-critic-label-b" opacity={0}>
          <text x={x0 + 8 * step + 6} y={lineY - 3} fontSize={9} fontFamily={MONO} fill={ACCENT}>
            drifting to 0.50,
          </text>
          <text x={x0 + 8 * step + 6} y={lineY + 9} fontSize={9} fontFamily={MONO} fill={ACCENT}>
            the running mean
          </text>
        </g>
        {payouts.map((payout, i) => {
          const cx = x0 + i * step + step / 2;
          const cy = yOf(payout);
          const up = payout > estimate;
          const tone = up ? S3 : S7;
          const tip = up ? cy + 6 : cy - 6;
          return (
            <g key={i}>
              <path d={`M${cx},${lineY} L${cx},${tip}`} stroke={tone} strokeWidth={1.6} />
              <path d={up ? `M${cx - 4},${tip + 6} L${cx},${tip} L${cx + 4},${tip + 6}` : `M${cx - 4},${tip - 6} L${cx},${tip} L${cx + 4},${tip - 6}`} fill="none" stroke={tone} strokeWidth={1.6} />
              <circle cx={cx} cy={cy} r={5} fill={tone} stroke={SURFACE} strokeWidth={1.5} />
              <text x={cx} y={up ? cy - 10 : cy + 16} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={tone}>
                {up ? "+0.55" : "−0.45"}
              </text>
              <text x={cx} y={y0 + 22} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                pull {i + 1}
              </text>
            </g>
          );
        })}
        <g transform="translate(30 206)">
          <circle cx={6} cy={0} r={4} fill={S3} />
          <text x={16} y={3} fontSize={9} fontFamily={SANS} fill={INK_2}>
            above the line: push that lever&apos;s probability up
          </text>
          <circle cx={266} cy={0} r={4} fill={S7} />
          <text x={276} y={3} fontSize={9} fontFamily={SANS} fill={INK_2}>
            below the line: push it down
          </text>
        </g>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 3. The clip: one batch may not move a probability past a fixed ratio.
 * ========================================================================= */

export function PolicyGradientClip() {
  const x0 = 60;
  const scale = 400;
  const y = 84;
  const h = 34;
  const old = 0.55;
  const proposed = 0.75;
  const ratio = 1.2;
  const limit = old * ratio;
  const px = (p: number) => x0 + p * scale;
  return (
    <Fig caption="A bar for the probability of pulling B, at 0.55 before the update. One batch of lucky pulls proposes 0.75. The clip allows at most 1.2 times the old value, 0.66, so the update stops at the bracket and the hatched part is discarded. That is the proximal in proximal policy optimisation.">
      <svg
        className="primer-pg"
        viewBox="0 0 560 200"
        role="img"
        aria-label="A probability bar at 0.55 with a proposed extension to 0.75 that is cut off by a bracket at 0.66"
      >
        <defs>
          <pattern id="pg-hatch" width={6} height={6} patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
            <path d="M0,0 V6" stroke={INK_3} strokeWidth={1.5} />
          </pattern>
        </defs>
        <text x={x0} y={28} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the clip
        </text>
        <text x={x0} y={42} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          new probability ÷ old probability is held inside a band; here the band ends at 1.2
        </text>
        <path d={`M${px(0)},${y + h + 12} H${px(1)}`} stroke={INK_2} strokeWidth={1} />
        {[0, 0.25, 0.5, 0.75, 1].map((tick) => (
          <g key={tick}>
            <path d={`M${px(tick)},${y + h + 9} V${y + h + 15}`} stroke={INK_2} strokeWidth={1} />
            <text x={px(tick)} y={y + h + 28} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
              {tick.toFixed(2)}
            </text>
          </g>
        ))}
        <rect x={px(0)} y={y} width={px(old) - px(0)} height={h} rx={3} fill={S2} />
        <rect x={px(old)} y={y} width={px(limit) - px(old)} height={h} fill={ACCENT_SOFT} stroke={ACCENT} strokeWidth={1} />
        <rect x={px(limit)} y={y} width={px(proposed) - px(limit)} height={h} fill="url(#pg-hatch)" stroke={INK_3} strokeWidth={1} strokeDasharray="3 2" />
        {/* the bracket at the clip limit */}
        <path d={`M${px(limit) + 6},${y - 14} H${px(limit)} V${y + h + 4} H${px(limit) + 6}`} fill="none" stroke={ACCENT} strokeWidth={2} />
        <text x={px(limit)} y={y - 20} textAnchor="middle" fontSize={9} fontFamily={MONO} fontWeight={700} fill={ACCENT}>
          clip: 1.2 × 0.55 = 0.66
        </text>
        <text x={px(old) - 4} y={y + h / 2 + 3} textAnchor="end" fontSize={9.5} fontFamily={MONO} fontWeight={700} fill={SURFACE}>
          old 0.55
        </text>
        <text x={(px(old) + px(limit)) / 2} y={y + h + 46} textAnchor="middle" fontSize={8.5} fontFamily={SANS} fill={ACCENT}>
          allowed
        </text>
        <text x={(px(limit) + px(proposed)) / 2} y={y + h + 46} textAnchor="middle" fontSize={8.5} fontFamily={SANS} fill={INK_3}>
          blocked
        </text>
        <path d={`M${px(proposed)},${y - 6} V${y + h + 4}`} stroke={INK_3} strokeWidth={1} strokeDasharray="2 2" />
        <text x={px(proposed) + 4} y={y - 8} fontSize={9} fontFamily={MONO} fill={INK_2}>
          proposed 0.75
        </text>
        <text x={x0} y={182} fontSize={9.5} fontFamily={SANS} fill={INK_3}>
          A lucky batch can still move the policy, only never by more than the band allows in one step.
        </text>
      </svg>
    </Fig>
  );
}
