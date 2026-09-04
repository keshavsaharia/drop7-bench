/**
 * Figures for the evolution primer at content/learn/techniques/evolution.mdx.
 *
 * The toy is a hidden bumpy function of one number, sampled through a bell
 * curve that is refitted to the best samples each generation. Every dot,
 * curve and dice face here is the toy's own; the figures show a mechanism and
 * quote no research number.
 *
 * Server components: SVG with CSS keyframes from ./evolution.css on elements
 * marked data-anim. Only opacity animates; no text animates.
 */
import "./evolution.css";
import type { ReactNode } from "react";

const SANS = "var(--font-sans)";
const MONO = "var(--font-mono)";
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const INK_4 = "var(--color-ink-4)";
const RULE = "var(--color-rule)";
const SURFACE = "var(--color-surface)";
const RAISED = "var(--color-raised)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const S1 = "var(--color-series-1)";
const S2 = "var(--color-series-2)";
const S3 = "var(--color-series-3)";
const S4 = "var(--color-series-4)";

function Fig({ caption, children }: { caption: string; children: ReactNode }) {
  return (
    <figure className="fig">
      <div className="fig-frame">{children}</div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/** The hidden function: a small bump near 30 and the real hill near 86, on 0..120. */
function hill(x: number): number {
  const small = 0.42 * Math.exp(-(((x - 30) / 13) ** 2));
  const big = Math.exp(-(((x - 86) / 16) ** 2));
  return small + big;
}

function curvePath(xs: number[], px: (x: number) => number, py: (v: number) => number): string {
  return xs.map((x, i) => `${i === 0 ? "M" : "L"}${px(x).toFixed(1)},${py(hill(x)).toFixed(1)}`).join(" ");
}

/** Twelve sample positions per generation: a wide cloud that tightens on the hill. */
const GENERATIONS: { mean: number; spread: number; xs: number[] }[] = [
  { mean: 50, spread: 32, xs: [6, 14, 22, 31, 38, 46, 54, 61, 70, 79, 92, 111] },
  { mean: 84, spread: 14, xs: [60, 66, 71, 75, 79, 82, 85, 89, 93, 97, 103, 109] },
  { mean: 86, spread: 7, xs: [73, 76, 79, 81, 83, 85, 87, 89, 91, 94, 97, 101] },
  { mean: 86, spread: 3.5, xs: [80, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 93] },
];

function elitesOf(xs: number[], count = 3): number[] {
  return [...xs].sort((a, b) => hill(b) - hill(a)).slice(0, count);
}

const DOMAIN = Array.from({ length: 61 }, (_, i) => i * 2);

/* =========================================================================
 * 1. Cloud on a curve: sample, keep the best three, refit, repeat.
 * ========================================================================= */

export function EvolutionCloud() {
  const panelW = 136;
  const inner = 120;
  const top = 44;
  const baseline = 150;
  const bellTop = 168;
  const bellH = 30;
  return (
    <Fig caption="Four generations of the cross-entropy method on a hidden bumpy function. In each panel, twelve samples are drawn from the bell curve shown underneath, the three highest (ringed) are kept, and the bell is refitted to them: its mean moves to their average and its width to their spread. The cloud climbs the hill and tightens.">
      <svg
        className="primer-evo"
        viewBox="0 0 560 240"
        role="img"
        aria-label="Four panels showing a scatter of twelve samples on a bumpy curve tightening onto the highest peak over four generations"
      >
        {GENERATIONS.map((gen, g) => {
          const x0 = 8 + g * panelW;
          const px = (x: number) => x0 + 6 + (x / 120) * inner;
          const py = (v: number) => baseline - v * 100;
          const elites = elitesOf(gen.xs);
          const bell = Array.from({ length: 41 }, (_, i) => {
            const x = i * 3;
            const v = Math.exp(-0.5 * ((x - gen.mean) / gen.spread) ** 2);
            return `${i === 0 ? "M" : "L"}${px(x).toFixed(1)},${(bellTop + bellH - v * bellH).toFixed(1)}`;
          }).join(" ");
          return (
            <g key={g}>
              <text x={x0 + 6} y={18} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
                generation {g + 1}
              </text>
              <text x={x0 + 6} y={31} fontSize={8.5} fontFamily={MONO} fill={INK_3}>
                12 samples, keep 3
              </text>
              <path d={`M${px(0)},${baseline} H${px(120)}`} stroke={RULE} strokeWidth={1} />
              <path d={curvePath(DOMAIN, px, py)} fill="none" stroke={INK_2} strokeWidth={1.2} />
              <g data-anim="evo-gen" data-step={g + 1}>
                {gen.xs.map((x) => (
                  <circle key={x} cx={px(x)} cy={py(hill(x))} r={3.4} fill={S1} stroke={SURFACE} strokeWidth={1} />
                ))}
                {elites.map((x) => (
                  <circle key={`e${x}`} cx={px(x)} cy={py(hill(x))} r={6.5} fill="none" stroke={ACCENT} strokeWidth={1.6} />
                ))}
                <path d={bell} fill={ACCENT_SOFT} stroke={ACCENT} strokeWidth={1} />
              </g>
              <text x={x0 + 6} y={bellTop + bellH + 14} fontSize={8.5} fontFamily={SANS} fill={INK_3}>
                the bell curve samples are drawn from
              </text>
              {g < 3 && (
                <path d={`M${x0 + panelW - 6},${top + 50} l6,4 l-6,4`} fill="none" stroke={INK_4} strokeWidth={1.2} />
              )}
            </g>
          );
        })}
        <text x={8 + 6} y={228} fontSize={9} fontFamily={SANS} fill={INK_3}>
          ringed: the three elites; the next bell curve is refitted to them
        </text>
        <text x={400} y={top + 6} fontSize={8.5} fontFamily={MONO} fill={INK_4}>
          the hidden function
        </text>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 2. Common random numbers: two candidates, the same three dice, paired.
 * ========================================================================= */

const PIPS: Record<number, [number, number][]> = {
  1: [[50, 50]],
  2: [
    [30, 30],
    [70, 70],
  ],
  3: [
    [30, 30],
    [50, 50],
    [70, 70],
  ],
  4: [
    [30, 30],
    [70, 30],
    [30, 70],
    [70, 70],
  ],
  5: [
    [30, 30],
    [70, 30],
    [50, 50],
    [30, 70],
    [70, 70],
  ],
  6: [
    [30, 28],
    [70, 28],
    [30, 50],
    [70, 50],
    [30, 72],
    [70, 72],
  ],
};

function Die({ x, y, s, face }: { x: number; y: number; s: number; face: number }) {
  const k = s / 100;
  return (
    <g transform={`translate(${x} ${y})`}>
      <rect width={s} height={s} rx={s * 0.2} fill={RAISED} stroke={INK_3} strokeWidth={1} />
      {PIPS[face].map(([cx, cy], i) => (
        <circle key={i} cx={cx * k} cy={cy * k} r={s * 0.09} fill={INK} />
      ))}
    </g>
  );
}

export function EvolutionCommonRandomNumbers() {
  const games = [5, 2, 6];
  const scoresA = [12, 7, 15];
  const scoresB = [14, 6, 19];
  const colX = [200, 300, 400];
  const dieS = 30;
  const rowA = 96;
  const rowB = 136;
  const rowD = 182;
  return (
    <Fig caption="Two candidates each play the same three games, shown as the same three dice. Their scores are compared game by game, so each difference measures the candidates and not the dice they were dealt. Selecting on those paired differences is what common random numbers means; averaging each row separately would let the luck of the dice back in.">
      <svg
        className="primer-evo"
        viewBox="0 0 560 200"
        role="img"
        aria-label="A table with three identical dice as columns, two candidates as rows with a score per game, and a third row of paired differences"
      >
        <text x={40} y={24} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
          the same three games for everyone
        </text>
        {games.map((face, j) => (
          <g key={j}>
            <Die x={colX[j] - dieS / 2} y={38} s={dieS} face={face} />
            <text x={colX[j]} y={38 + dieS + 12} textAnchor="middle" fontSize={8.5} fontFamily={MONO} fill={INK_3}>
              game {j + 1}
            </text>
          </g>
        ))}
        <path d={`M40,${rowA - 20} H${colX[2] + 60}`} stroke={RULE} strokeWidth={1} />
        <text x={40} y={rowA + 4} fontSize={10} fontFamily={SANS} fill={INK_2}>
          candidate A
        </text>
        {scoresA.map((score, j) => (
          <text key={j} x={colX[j]} y={rowA + 4} textAnchor="middle" fontSize={11} fontFamily={MONO} fill={S1}>
            {score}
          </text>
        ))}
        <text x={40} y={rowB + 4} fontSize={10} fontFamily={SANS} fill={INK_2}>
          candidate B
        </text>
        {scoresB.map((score, j) => (
          <text key={j} x={colX[j]} y={rowB + 4} textAnchor="middle" fontSize={11} fontFamily={MONO} fill={S2}>
            {score}
          </text>
        ))}
        <path d={`M40,${rowD - 22} H${colX[2] + 60}`} stroke={RULE} strokeWidth={1} strokeDasharray="4 3" />
        <text x={40} y={rowD + 4} fontSize={10} fontFamily={SANS} fill={INK_2}>
          B minus A, same game
        </text>
        {scoresB.map((score, j) => {
          const d = score - scoresA[j];
          return (
            <text key={j} x={colX[j]} y={rowD + 4} textAnchor="middle" fontSize={11} fontFamily={MONO} fontWeight={700} fill={d >= 0 ? S3 : S4}>
              {d >= 0 ? `+${d}` : `−${Math.abs(d)}`}
            </text>
          );
        })}
        {colX.map((x) => (
          <path key={x} d={`M${x},${rowA + 12} V${rowB - 12}`} stroke={INK_4} strokeWidth={1} />
        ))}
        <text x={colX[2] + 40} y={rowA + 4} fontSize={9} fontFamily={SANS} fill={INK_3}>
          scores
        </text>
        <text x={colX[2] + 40} y={rowD + 4} fontSize={9} fontFamily={SANS} fill={INK_3}>
          paired differences
        </text>
      </svg>
    </Fig>
  );
}

/* =========================================================================
 * 3. Selection noise: fresh noise moves the elites; shared noise settles them.
 * ========================================================================= */

export function EvolutionSelectionNoise() {
  const panelW = 272;
  const top = 46;
  const baseline = 150;
  const band = 0.16;
  const xs = GENERATIONS[1].xs;
  const trueElites = elitesOf(xs);
  const luckySets: { id: string; xs: number[]; tone: string }[] = [
    { id: "evo-noise-1", xs: trueElites, tone: ACCENT },
    { id: "evo-noise-2", xs: [66, 103, 85], tone: S2 },
    { id: "evo-noise-3", xs: [71, 97, 93], tone: S4 },
  ];
  const panels = [
    { title: "fresh noise every generation", note: "the elites bounce with the noise" },
    { title: "the same noise for every candidate", note: "the elites settle" },
  ];
  return (
    <Fig caption="The same twelve samples on the same hill, evaluated with noise. On the left every generation draws fresh noise, so which three samples come out on top changes from draw to draw and the refit follows luck. On the right every candidate is scored on the same noise, the ranking is stable, and the three elites stay the three highest points.">
      <svg
        className="primer-evo"
        viewBox="0 0 560 230"
        role="img"
        aria-label="Two panels of the same samples on a curve inside a noise band; on the left the ringed elites keep changing, on the right they stay fixed"
      >
        {panels.map((panel, p) => {
          const x0 = 8 + p * panelW;
          const px = (x: number) => x0 + 8 + ((x - 50) / 70) * (panelW - 24);
          const py = (v: number) => baseline - v * 96;
          const domain = Array.from({ length: 36 }, (_, i) => 50 + i * 2);
          const upper = domain.map((x, i) => `${i === 0 ? "M" : "L"}${px(x).toFixed(1)},${py(hill(x) + band).toFixed(1)}`).join(" ");
          const lower = [...domain].reverse().map((x) => `L${px(x).toFixed(1)},${py(hill(x) - band).toFixed(1)}`).join(" ");
          return (
            <g key={p}>
              <text x={x0 + 8} y={20} fontSize={10.5} fontFamily={SANS} fontWeight={600} fill={INK}>
                {panel.title}
              </text>
              <path d={`${upper} ${lower} Z`} fill={ACCENT_SOFT} stroke="none" />
              <path d={curvePath(domain, px, py)} fill="none" stroke={INK_2} strokeWidth={1.2} />
              <path d={`M${px(50)},${baseline} H${px(120)}`} stroke={RULE} strokeWidth={1} />
              {xs.map((x) => (
                <circle key={x} cx={px(x)} cy={py(hill(x))} r={3.4} fill={S1} stroke={SURFACE} strokeWidth={1} />
              ))}
              {p === 0
                ? luckySets.map((set) => (
                    <g key={set.id} data-anim={set.id}>
                      {set.xs.map((x) => (
                        <circle key={x} cx={px(x)} cy={py(hill(x))} r={6.5} fill="none" stroke={set.tone} strokeWidth={1.6} />
                      ))}
                    </g>
                  ))
                : trueElites.map((x) => (
                    <circle key={x} cx={px(x)} cy={py(hill(x))} r={6.5} fill="none" stroke={ACCENT} strokeWidth={1.6} />
                  ))}
              <text x={x0 + 8} y={baseline + 20} fontSize={9.5} fontFamily={SANS} fill={INK_2}>
                {panel.note}
              </text>
              <text x={x0 + 8} y={top - 6} fontSize={8.5} fontFamily={MONO} fill={INK_4}>
                shaded: how far noise can move a score
              </text>
            </g>
          );
        })}
        <path d={`M${8 + panelW - 6},${top} V${baseline + 26}`} stroke={RULE} strokeWidth={1} strokeDasharray="3 3" />
        <text x={16} y={216} fontSize={9} fontFamily={SANS} fill={INK_3}>
          ringed: the three samples selected as elites
        </text>
      </svg>
    </Fig>
  );
}
