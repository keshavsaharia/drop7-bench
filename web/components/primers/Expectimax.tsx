/**
 * Figures for the expectimax primer (web/content/learn/techniques/expectimax.mdx).
 *
 * The toy is the two-door coin game from the primer: door A pays 6, door B
 * pays 0 or 10 on a fair coin. Nothing here is a Drop7 position or a research
 * number. Server components: SVG with CSS keyframes in ./expectimax.css,
 * marked with data-anim; every figure is complete at its first frame and
 * rests on a designed frame under prefers-reduced-motion.
 */
import type { ReactNode } from "react";
import "./expectimax.css";

const MONO = "var(--font-mono)";
const SANS = "var(--font-sans)";
const INK = "var(--color-ink)";
const INK2 = "var(--color-ink-2)";
const INK3 = "var(--color-ink-3)";
const RULE = "var(--color-rule)";
const RULE_STRONG = "var(--color-rule-strong)";
const SURFACE = "var(--color-surface)";
const RAISED = "var(--color-raised)";
const ACCENT = "var(--color-accent)";
const CHOICE = "var(--color-series-1)";
const CHANCE = "var(--color-series-2)";

function Fig({ viewBox, label, caption, children }: { viewBox: string; label: string; caption: string; children: ReactNode }) {
  return (
    <figure className="fig primer-expectimax">
      <div className="fig-frame">
        <svg viewBox={viewBox} role="img" aria-label={label}>
          {children}
        </svg>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/** A choice node: a square. */
function Square({ x, y, size = 34 }: { x: number; y: number; size?: number }) {
  return <rect x={x - size / 2} y={y - size / 2} width={size} height={size} rx={5} fill={SURFACE} stroke={CHOICE} strokeWidth={2} />;
}

/** A chance node: a circle, optionally with its value inside. */
function Coin({ x, y, r = 17, value }: { x: number; y: number; r?: number; value?: string }) {
  return (
    <g>
      <circle cx={x} cy={y} r={r} fill={SURFACE} stroke={CHANCE} strokeWidth={2} />
      {value !== undefined && (
        <text x={x} y={y + 1} textAnchor="middle" dominantBaseline="central" fontFamily={MONO} fontSize={r * 0.72} fontWeight={700} fill={INK}>
          {value}
        </text>
      )}
    </g>
  );
}

/** A leaf: a known payout. */
function Leaf({ x, y, value, w = 40, h = 28 }: { x: number; y: number; value: string; w?: number; h?: number }) {
  return (
    <g>
      <rect x={x - w / 2} y={y - h / 2} width={w} height={h} rx={6} fill={RAISED} stroke={RULE_STRONG} />
      <text x={x} y={y + 1} textAnchor="middle" dominantBaseline="central" fontFamily={MONO} fontSize={h * 0.5} fontWeight={700} fill={INK}>
        {value}
      </text>
    </g>
  );
}

function Edge({ x1, y1, x2, y2, bold = false }: { x1: number; y1: number; x2: number; y2: number; bold?: boolean }) {
  return <line x1={x1} y1={y1} x2={x2} y2={y2} stroke={bold ? ACCENT : INK3} strokeWidth={bold ? 3 : 1.5} strokeLinecap="round" />;
}

/* =========================================================================
 * 1. The two-door tree, valued bottom-up. The finished tree is always drawn;
 *    the animation is a highlight that walks from the leaves to the root.
 * ========================================================================= */

export function ExpectimaxTwoDoors({ caption }: { caption?: string }) {
  return (
    <Fig
      viewBox="0 0 560 270"
      label="The two-door coin game as a tree: a square for your choice, a circle for the coin, and three leaves"
      caption={
        caption ??
        "The two-door coin game as a tree. Door A leads to a known 6. Door B leads to a coin, whose two leaves pay 0 and 10; the circle averages them to 5. The square takes the larger of 6 and 5, so the chosen branch is door A. The highlight walks the values up from the leaves in the order the search fills them in."
      }
    >
      {/* edges */}
      <Edge x1={268} y1={52} x2={166} y2={204} bold />
      <Edge x1={292} y1={52} x2={390} y2={116} />
      <Edge x1={392} y1={146} x2={354} y2={206} />
      <Edge x1={408} y1={146} x2={446} y2={206} />
      {/* edge labels */}
      <text x={200} y={124} textAnchor="end" fontFamily={MONO} fontSize={10.5} fill={INK2}>door A</text>
      <text x={350} y={80} textAnchor="start" fontFamily={MONO} fontSize={10.5} fill={INK2}>door B</text>
      <text x={358} y={182} textAnchor="end" fontFamily={MONO} fontSize={10} fill={INK3}>tails ½</text>
      <text x={442} y={182} textAnchor="start" fontFamily={MONO} fontSize={10} fill={INK3}>heads ½</text>
      {/* nodes */}
      <Square x={280} y={40} />
      <Coin x={400} y={130} />
      <Leaf x={160} y={220} value="6" />
      <Leaf x={350} y={220} value="0" />
      <Leaf x={450} y={220} value="10" />
      {/* node annotations */}
      <text x={252} y={36} textAnchor="end" fontFamily={MONO} fontSize={10} fill={INK3}>you choose</text>
      <text x={252} y={50} textAnchor="end" fontFamily={MONO} fontSize={10} fill={INK3}>take the best</text>
      <text x={306} y={34} fontFamily={MONO} fontSize={10} fill={INK3}>best of 6 and 5</text>
      <text x={306} y={50} fontFamily={MONO} fontSize={12.5} fontWeight={700} fill={INK}>= 6, door A</text>
      <text x={426} y={114} fontFamily={MONO} fontSize={10} fill={INK3}>the coin: average</text>
      <text x={426} y={132} fontFamily={MONO} fontSize={12.5} fontWeight={700} fill={INK}>(0 + 10) ÷ 2 = 5</text>
      <text x={160} y={246} textAnchor="middle" fontFamily={MONO} fontSize={10} fill={INK3}>always</text>
      {/* legend */}
      <g transform="translate(20 258)">
        <rect x={0} y={-8} width={10} height={10} rx={2} fill={SURFACE} stroke={CHOICE} strokeWidth={1.5} />
        <text x={16} y={0} fontFamily={SANS} fontSize={10.5} fill={INK2}>choice: take the best</text>
        <circle cx={150} cy={-3} r={5} fill={SURFACE} stroke={CHANCE} strokeWidth={1.5} />
        <text x={161} y={0} fontFamily={SANS} fontSize={10.5} fill={INK2}>chance: take the average</text>
      </g>
      {/* animated highlights, bottom-up */}
      <g data-anim="leaves" fill="none" stroke={ACCENT} strokeWidth={2}>
        <rect x={134} y={200} width={52} height={40} rx={9} />
        <rect x={324} y={200} width={52} height={40} rx={9} />
        <rect x={424} y={200} width={52} height={40} rx={9} />
      </g>
      <g data-anim="circle" fill="none" stroke={ACCENT} strokeWidth={2}>
        <circle cx={400} cy={130} r={24} />
        <rect x={420} y={118} width={132} height={22} rx={6} />
      </g>
      <g data-anim="root">
        <rect x={254} y={14} width={52} height={52} rx={9} fill="none" stroke={ACCENT} strokeWidth={2} />
        <rect x={300} y={38} width={104} height={20} rx={6} fill="none" stroke={ACCENT} strokeWidth={2} />
        <line x1={268} y1={52} x2={166} y2={204} stroke={ACCENT} strokeWidth={9} strokeLinecap="round" opacity={0.3} />
      </g>
    </Fig>
  );
}

/* =========================================================================
 * 2. The same tree valued by an optimist, a pessimist, and a fair player.
 * ========================================================================= */

export function ExpectimaxThreeWays({ caption }: { caption?: string }) {
  const panels = [
    { title: "Optimist", sub: "circle takes the max", value: "10", rule: "max", pickB: true, verdict: "picks door B: 10 beats 6" },
    { title: "Pessimist", sub: "circle takes the min", value: "0", rule: "min", pickB: false, verdict: "picks door A: 6 beats 0" },
    { title: "Fair", sub: "circle takes the mean", value: "5", rule: "mean", pickB: false, verdict: "picks door A: 6 beats 5" },
  ];
  return (
    <Fig
      viewBox="0 0 560 230"
      label="The two-door tree valued three ways: optimist, pessimist and fair"
      caption={
        caption ??
        "The same tree valued three ways. The optimist assumes heads and values door B at 10, so it walks through B. The pessimist assumes tails, values B at 0, and takes A. The fair player averages to 5 and also takes A, for a different reason. Only the average is what a long run of coin flips would actually pay."
      }
    >
      {panels.map((p, i) => {
        const ox = i * 192;
        return (
          <g key={p.title} transform={`translate(${ox} 0)`}>
            <rect x={0} y={4} width={176} height={220} rx={8} fill={SURFACE} stroke={RULE} />
            <text x={12} y={26} fontFamily={SANS} fontSize={13} fontWeight={700} fill={INK}>{p.title}</text>
            <text x={12} y={42} fontFamily={MONO} fontSize={10} fill={INK3}>{p.sub}</text>
            <Edge x1={80} y1={84} x2={50} y2={148} bold={!p.pickB} />
            <Edge x1={96} y1={84} x2={122} y2={100} bold={p.pickB} />
            <Edge x1={122} y1={124} x2={108} y2={150} />
            <Edge x1={134} y1={124} x2={150} y2={150} />
            <Square x={88} y={72} size={26} />
            <Coin x={128} y={112} r={13} value={p.value} />
            <text x={146} y={108} fontFamily={MONO} fontSize={9.5} fill={INK3}>{p.rule}</text>
            <Leaf x={44} y={162} value="6" w={32} h={22} />
            <Leaf x={104} y={162} value="0" w={30} h={22} />
            <Leaf x={152} y={162} value="10" w={30} h={22} />
            <text x={30} y={104} textAnchor="end" fontFamily={MONO} fontSize={9.5} fill={INK3}>A</text>
            <text x={118} y={92} fontFamily={MONO} fontSize={9.5} fill={INK3}>B</text>
            <text x={12} y={204} fontFamily={MONO} fontSize={11} fontWeight={700} fill={ACCENT}>{p.verdict}</text>
          </g>
        );
      })}
    </Fig>
  );
}

/* =========================================================================
 * 3. Why the tree explodes: branching 2 for four rounds beside branching 7.
 * ========================================================================= */

export function ExpectimaxBranching({ caption }: { caption?: string }) {
  const levelsY = [44, 86, 128, 170, 212];
  const binary: { x: number; y: number; px?: number; py?: number }[] = [];
  let previous: { x: number; y: number }[] = [];
  for (let level = 0; level < levelsY.length; level += 1) {
    const count = 2 ** level;
    const spacing = 180 / count;
    const current: { x: number; y: number }[] = [];
    for (let i = 0; i < count; i += 1) {
      const node = { x: 120 + (i - (count - 1) / 2) * spacing, y: levelsY[level] };
      const parent = previous[Math.floor(i / 2)];
      binary.push(parent ? { ...node, px: parent.x, py: parent.y } : node);
      current.push(node);
    }
    previous = current;
  }
  const fanRoot = { x: 400, y: 44 };
  const fan1 = Array.from({ length: 7 }, (_, i) => ({ x: 400 + (i - 3) * 36, y: 98 }));
  const fan2 = fan1.flatMap((p) => Array.from({ length: 7 }, (_, j) => ({ x: p.x + (j - 3) * 5, y: 152, px: p.x, py: p.y })));
  const fan3 = fan2.flatMap((p) => Array.from({ length: 7 }, (_, j) => ({ x: p.x + (j - 3) * 1.4, y: 240, px: p.x, py: p.y })));
  return (
    <Fig
      viewBox="0 0 560 240"
      label="A tree with two branches per node over four rounds, beside a tree with seven branches per node that runs off the frame"
      caption={
        caption ??
        "On the left, the two-door game played for four rounds: each round doubles the leaves, from 2 to 16. On the right, seven options followed by seven equally likely outcomes, the shape of a Drop7 turn: each round multiplies the leaves by 49, and the third round already runs off the frame. A real search stops after a few rounds and asks an evaluator about the rest."
      }
    >
      <text x={120} y={22} textAnchor="middle" fontFamily={SANS} fontSize={11.5} fill={INK2}>two doors, a coin each round</text>
      {binary.map((n, i) => n.px !== undefined && <line key={`b${i}`} x1={n.px} y1={n.py} x2={n.x} y2={n.y} stroke={INK3} strokeWidth={1} />)}
      {binary.map((n, i) => (
        <circle key={`bn${i}`} cx={n.x} cy={n.y} r={n.y === levelsY[0] ? 4 : 2.6} fill={n.y === levelsY[0] ? CHOICE : RAISED} stroke={n.y === levelsY[0] ? CHOICE : INK3} />
      ))}
      <text x={120} y={234} textAnchor="middle" fontFamily={MONO} fontSize={11} fill={INK}>leaves: 2 → 4 → 8 → 16</text>

      <text x={400} y={22} textAnchor="middle" fontFamily={SANS} fontSize={11.5} fill={INK2}>seven columns, then seven discs</text>
      {fan3.map((n, i) => <line key={`f3${i}`} x1={n.px} y1={n.py} x2={n.x} y2={n.y} stroke={INK3} strokeWidth={0.5} opacity={0.5} />)}
      {fan2.map((n, i) => <line key={`f2${i}`} x1={n.px} y1={n.py} x2={n.x} y2={n.y} stroke={INK3} strokeWidth={0.8} />)}
      {fan1.map((n, i) => <line key={`f1${i}`} x1={fanRoot.x} y1={fanRoot.y} x2={n.x} y2={n.y} stroke={INK3} strokeWidth={1} />)}
      {fan2.map((n, i) => <circle key={`f2n${i}`} cx={n.x} cy={n.y} r={1.6} fill={RAISED} stroke={INK3} strokeWidth={0.6} />)}
      {fan1.map((n, i) => <circle key={`f1n${i}`} cx={n.x} cy={n.y} r={3} fill={SURFACE} stroke={CHANCE} strokeWidth={1.5} />)}
      <circle cx={fanRoot.x} cy={fanRoot.y} r={4} fill={CHOICE} stroke={CHOICE} />
      <rect x={366} y={214} width={190} height={22} rx={5} fill={SURFACE} stroke={RULE} />
      <text x={461} y={229} textAnchor="middle" fontFamily={MONO} fontSize={11} fill={INK}>leaves: 7 → 49 → 2,401 → …</text>
    </Fig>
  );
}
