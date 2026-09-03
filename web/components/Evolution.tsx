/**
 * Figures for the nnue-evolution approach page.
 *
 * Data figures read the committed run snapshot
 * (web/content/figures/nnue-evolution/<run-id>.json, written by
 * web/scripts/extract-nnue-evolution.ts from the run's artifacts) and hand its
 * rows to the client charts in ./charts/EvolutionCharts.tsx. Every number they
 * print is a value in that snapshot. A stage whose artifacts do not exist yet
 * renders a visible "not recorded yet" note, never a placeholder number.
 *
 * The explanatory diagrams (pipeline, generation loop, network shape, paired
 * seeds) are schematic: they carry the protocol's fixed constants and no
 * measured value.
 *
 * Server components; styled by the `.research-fig` and `.evo-*` blocks in
 * globals.css.
 */
import type { ReactNode } from "react";
import { readRepoFile } from "@/lib/repo";
import { formatSigned, formatValue } from "@/lib/charts/spec";
import { ARM_LABELS, CONTRAST_LABELS, SNAPSHOT_FORMAT, type EvolutionSnapshot } from "@/lib/charts/evolution";
import { CorpusScores, EvolutionCurve, FitnessSwarm, PretrainCurve, ScreenPairs } from "./charts/EvolutionCharts";

const RUN = /^RUN-[A-Za-z0-9-]+$/;
const INK = "#fafafa";
const INK_2 = "#a1a1aa";
const INK_3 = "#71717a";
const LINE = "#3f3f46";
const BLUE = "#3987e5";
const AMBER = "#f59e0b";
const GREEN = "#22c55e";
const PINK = "#e879f9";
const FONT = "ui-sans-serif, system-ui, -apple-system, 'Segoe UI', sans-serif";

function loadSnapshot(run: string): EvolutionSnapshot | null {
  if (!RUN.test(run)) return null;
  const raw = readRepoFile(`web/content/figures/nnue-evolution/${run}.json`);
  if (!raw) return null;
  try {
    const parsed = JSON.parse(raw) as EvolutionSnapshot;
    return parsed.format === SNAPSHOT_FORMAT ? parsed : null;
  } catch {
    return null;
  }
}

function snapshotPath(run: string) {
  return `web/content/figures/nnue-evolution/${run}.json`;
}

function Frame({ children, caption, run, sources, className }: { children: ReactNode; caption?: string; run?: string; sources?: string[]; className?: string }) {
  return (
    <figure className={`research-fig${className ? ` ${className}` : ""}`}>
      {children}
      {caption && <figcaption>{caption}</figcaption>}
      {run && (
        <details className="research-fig-data">
          <summary>Source</summary>
          <p className="research-fig-notes">
            Values copied from the run snapshot <code>{snapshotPath(run)}</code>, which
            {" "}
            <code>web/scripts/extract-nnue-evolution.ts</code> writes from the run&apos;s artifacts
            {sources && sources.length > 0 ? ":" : "."}
          </p>
          {sources && sources.length > 0 && (
            <ul className="evo-sources">
              {sources.map((s) => (
                <li key={s}>
                  <code>{s}</code>
                </li>
              ))}
            </ul>
          )}
        </details>
      )}
    </figure>
  );
}

function Absent({ run, stage, caption }: { run: string; stage: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  return (
    <figure className="research-fig research-fig-missing">
      <p>
        {snapshot ? (
          <>
            The {stage} stage of <code>{run}</code> had produced no artifacts when the snapshot was taken ({snapshot.capturedAt}). Re-run{" "}
            <code>node --experimental-strip-types web/scripts/extract-nnue-evolution.ts --run {run}</code> once it has.
          </>
        ) : (
          <>
            No snapshot for <code>{run}</code> is present at <code>{snapshotPath(run)}</code>.
          </>
        )}
      </p>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

function Stats({ items }: { items: { label: string; value: string }[] }) {
  return (
    <dl className="evo-stats">
      {items.map((item) => (
        <div key={item.label}>
          <dt>{item.label}</dt>
          <dd>{item.value}</dd>
        </div>
      ))}
    </dl>
  );
}

function hours(seconds: number): string {
  return seconds >= 3600 ? `${(seconds / 3600).toFixed(1)} h` : `${(seconds / 60).toFixed(0)} min`;
}

/* ============================================================ status strip */

type StageState = "done" | "running" | "pending";

export function EvolutionStatus({ run }: { run: string }) {
  const snapshot = loadSnapshot(run);
  if (!snapshot) return <Absent run={run} stage="whole" />;
  const { corpus, pretrain, evolve, select, screen } = snapshot;
  const generationsPlanned = typeof evolve?.config?.generations === "number" ? (evolve.config.generations as number) : 60;
  const stages: { name: string; state: StageState; detail: string }[] = [
    {
      name: "A · teacher corpus",
      state: corpus ? (pretrain ? "done" : "running") : "pending",
      detail: corpus ? `${formatValue(corpus.games)} complete games, ${formatValue(corpus.roots)} labelled roots` : "no games yet",
    },
    {
      name: "B · supervised warm start",
      state: pretrain ? "done" : corpus ? "pending" : "pending",
      detail: pretrain ? `best epoch ${pretrain.report.bestEpoch} of ${pretrain.report.epochs}, validation Huber ${pretrain.report.bestValHuber.toFixed(4)}` : "waits for the corpus",
    },
    {
      name: "C · evolution",
      state: evolve ? (select ? "done" : "running") : "pending",
      detail: evolve ? `${evolve.generationsCompleted} of ${generationsPlanned} generations completed` : "waits for the warm start",
    },
    {
      name: "C · elite re-selection",
      state: select ? "done" : "pending",
      detail: select?.candidateSha256 ? `candidate frozen, SHA-256 ${select.candidateSha256.slice(0, 12)}…` : "waits for the last generation",
    },
    {
      name: "D · held-out screen",
      state: screen ? "done" : "pending",
      detail: screen ? (screen.gate ? `preregistered gate ${screen.gate.allPassed ? "passed" : "not passed"}` : "played; contrasts pending") : "opens once, after the candidate is frozen",
    },
  ];
  return (
    <Frame run={run} className="evo-status">
      <div className="evo-status-head">
        <span>
          Run <code>{snapshot.runId}</code>
          {snapshot.experimentId && (
            <>
              {" "}
              under <code>{snapshot.experimentId}</code>
            </>
          )}
          {snapshot.runLifecycle && <> · run record lifecycle: {snapshot.runLifecycle}</>}
        </span>
        <span className="evo-status-time">snapshot {snapshot.capturedAt}</span>
      </div>
      <ol className="evo-stages">
        {stages.map((stage) => (
          <li key={stage.name} className={`is-${stage.state}`}>
            <span className="evo-stage-dot" aria-hidden="true" />
            <span className="evo-stage-name">{stage.name}</span>
            <span className="evo-stage-state">{stage.state}</span>
            <span className="evo-stage-detail">{stage.detail}</span>
          </li>
        ))}
      </ol>
    </Frame>
  );
}

/* ================================================================ figures */

export function CorpusFigure({ run, caption }: { run: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const corpus = snapshot?.corpus;
  if (!snapshot || !corpus) return <Absent run={run} stage="teacher corpus" caption={caption} />;
  const cap = typeof snapshot.evolve?.config?.moveCap === "number" ? (snapshot.evolve.config.moveCap as number) : undefined;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("corpus") || s.endsWith("analysis.json"))}>
      <h4 className="rchart-title">Every teacher game the depth-5 search has completed</h4>
      <CorpusScores games={corpus.perGame} median={corpus.score.median} cap={cap} />
      <Stats
        items={[
          { label: "complete games", value: formatValue(corpus.games) },
          { label: "labelled roots", value: formatValue(corpus.roots) },
          { label: "mean score", value: formatValue(corpus.score.mean) },
          { label: "median score", value: formatValue(corpus.score.median) },
          { label: "best game", value: formatValue(corpus.score.max) },
          { label: "mean moves", value: corpus.moves.mean.toFixed(1) },
          { label: "games stopped at the cap", value: formatValue(corpus.censoredGames) },
          { label: "teacher time per root", value: corpus.secondsPerRoot !== null ? `${corpus.secondsPerRoot.toFixed(1)} s` : "n/a" },
          { label: "mean time per game", value: hours(corpus.wallSecondsPerGame.mean) },
        ]}
      />
    </Frame>
  );
}

export function PretrainFigure({ run, caption }: { run: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const pretrain = snapshot?.pretrain;
  if (!snapshot || !pretrain) return <Absent run={run} stage="supervised warm-start" caption={caption} />;
  const r = pretrain.report;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("pretrain") || s.endsWith("analysis.json"))}>
      <h4 className="rchart-title">Supervised warm start: loss by epoch on a whole-origin split</h4>
      {pretrain.epochs.length > 0 ? <PretrainCurve epochs={pretrain.epochs} bestEpoch={r.bestEpoch} /> : <p className="rchart-empty">The epoch log was not retained; the report below is the recorded summary.</p>}
      <Stats
        items={[
          { label: "roots / games", value: `${formatValue(r.roots)} / ${formatValue(r.games)}` },
          { label: "train / validation roots", value: `${formatValue(r.trainRoots)} / ${formatValue(r.valRoots)}` },
          { label: "epoch frozen", value: `${r.bestEpoch} of ${r.epochs}` },
          { label: "validation Huber", value: r.bestValHuber.toFixed(4) },
          ...(pretrain.probe
            ? [
                { label: "ordering probe roots", value: formatValue(pretrain.probe.probeRoots) },
                { label: "top-1 agreement with teacher", value: `${(pretrain.probe.top1 * 100).toFixed(1)}%` },
                { label: "mean teacher-value regret", value: `${formatValue(pretrain.probe.meanTeacherValueRegret)} points` },
              ]
            : []),
        ]}
      />
    </Frame>
  );
}

export function EvolutionFigure({ run, caption }: { run: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const evolve = snapshot?.evolve;
  if (!snapshot || !evolve || evolve.generations.length === 0) return <Absent run={run} stage="evolution" caption={caption} />;
  const check = evolve.trainingSignalCheck;
  const last = evolve.generations[evolve.generations.length - 1];
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("evolve") || s.endsWith("analysis.json"))}>
      <h4 className="rchart-title">Fitness by generation against the two controls that played the same games</h4>
      <EvolutionCurve generations={evolve.generations} />
      <Stats
        items={[
          { label: "generations completed", value: formatValue(evolve.generationsCompleted) },
          { label: "latest best candidate", value: formatValue(last.best) },
          { label: "latest population mean", value: formatValue(last.mean) },
          { label: "latest fair-leaf control", value: formatValue(last.controlFair) },
          { label: "latest init control", value: formatValue(last.controlInit) },
          { label: `mean above fair, last ${check.window.length}`, value: `${check.generationsMeanAboveFair} of ${check.window.length}` },
          { label: "mean margin over fair", value: check.meanMarginLast10 !== null ? formatSigned(check.meanMarginLast10) : "n/a" },
          { label: "training-signal falsifier", value: check.passed === null ? "pending" : check.passed ? "signal present" : "no signal" },
          ...(typeof last.controlBaseline === "number"
            ? [
                { label: "latest first-run-candidate control", value: formatValue(last.controlBaseline) },
                { label: "latest mean minus first run's candidate", value: formatSigned(last.mean - last.controlBaseline) },
              ]
            : []),
          ...(typeof last.sigmaRel === "number" ? [{ label: "mutation sigma (latest)", value: last.sigmaRel.toFixed(4) }] : []),
          ...(evolve.plateauChecks && evolve.plateauChecks.length > 0
            ? [
                {
                  label: `plateau check after gen ${evolve.plateauChecks[evolve.plateauChecks.length - 1].generation}`,
                  value: `slope ${formatSigned(evolve.plateauChecks[evolve.plateauChecks.length - 1].slopePerGeneration)}/gen, lower bound ${formatSigned(evolve.plateauChecks[evolve.plateauChecks.length - 1].lowerBound95)} → ${evolve.plateauChecks[evolve.plateauChecks.length - 1].stop ? "stop" : "continue"}`,
                },
              ]
            : []),
          ...(evolve.stoppedOnPlateau ? [{ label: "stopped by the plateau rule", value: "yes" }] : []),
          { label: "illegal / incomplete decisions", value: `${evolve.artifactIntegrity.illegalDecisions} / ${evolve.artifactIntegrity.incompleteDecisions}` },
        ]}
      />
    </Frame>
  );
}

export function SwarmFigure({ run, caption }: { run: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const evolve = snapshot?.evolve;
  if (!snapshot || !evolve || !evolve.generations.some((g) => g.fitness.length > 0)) return <Absent run={run} stage="evolution" caption={caption} />;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("progress.jsonl"))}>
      <h4 className="rchart-title">Every candidate in every generation</h4>
      <FitnessSwarm generations={evolve.generations} />
    </Frame>
  );
}

export function ScreenFigure({ run, contrast = "candidate-vs-fair-d3s7", caption }: { run: string; contrast?: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const screen = snapshot?.screen;
  const paired = screen?.paired[contrast];
  if (!snapshot || !screen || !paired) return <Absent run={run} stage="held-out screen" caption={caption} />;
  const candidateLabel = ARM_LABELS[paired.candidateArm] ?? paired.candidateArm;
  const referenceLabel = ARM_LABELS[paired.referenceArm] ?? paired.referenceArm;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("screen"))}>
      <h4 className="rchart-title">Held-out screen: {CONTRAST_LABELS[contrast] ?? contrast}</h4>
      {paired.perSeed.length > 0 ? (
        <ScreenPairs contrast={paired} arms={screen.arms} candidateLabel={candidateLabel} referenceLabel={referenceLabel} />
      ) : (
        <p className="rchart-empty">Per-game rows were not retained in the snapshot; the recorded contrast is below.</p>
      )}
      <Stats
        items={[
          { label: "paired mean difference", value: `${formatSigned(paired.meanDelta)} points` },
          { label: "bootstrap 95% lower bound", value: formatSigned(paired.bootstrapLower95) },
          { label: "Student-t 95% lower bound", value: formatSigned(paired.studentTLower95) },
          { label: "wins / ties / losses", value: paired.wtl.join(" / ") },
          { label: "first half / second half", value: `${formatSigned(paired.halves[0])} / ${formatSigned(paired.halves[1])}` },
          { label: "paired sd", value: formatValue(paired.pairedSd) },
          { label: "detection floor", value: formatValue(paired.detectionFloor) },
          { label: "lower-quartile difference", value: formatSigned(paired.q25Delta) },
        ]}
      />
    </Frame>
  );
}

export function ScreenGateTable({ run }: { run: string }) {
  const snapshot = loadSnapshot(run);
  const screen = snapshot?.screen;
  if (!snapshot || !screen) return <Absent run={run} stage="held-out screen" />;
  const arms = Object.entries(screen.arms);
  return (
    <Frame run={run} sources={snapshot.sources.filter((s) => s.includes("screen"))} className="evo-gate">
      <h4 className="rchart-title">The four arms on the same 64 held-out games, and the preregistered gate</h4>
      <div className="research-fig-scroll">
        <table>
          <thead>
            <tr>
              <th>arm</th>
              <th className="num">mean</th>
              <th className="num">median</th>
              <th className="num">lower quartile</th>
              <th className="num">best game</th>
              <th className="num">mean moves</th>
              <th className="num">censored</th>
            </tr>
          </thead>
          <tbody>
            {arms.map(([name, arm]) => (
              <tr key={name}>
                <td>{ARM_LABELS[name] ?? name}</td>
                <td className="num">{formatValue(arm.mean)}</td>
                <td className="num">{formatValue(arm.median)}</td>
                <td className="num">{formatValue(arm.q25)}</td>
                <td className="num">{formatValue(arm.max)}</td>
                <td className="num">{arm.movesMean.toFixed(1)}</td>
                <td className="num">{arm.censored}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {screen.gate ? (
        <ul className="evo-checks">
          {screen.gate.checks.map((check) => (
            <li key={check.criterion} className={check.passed ? "is-pass" : "is-fail"}>
              <span className="evo-check-mark" aria-hidden="true">
                {check.passed ? "✓" : "✗"}
              </span>
              <span>{check.criterion}</span>
            </li>
          ))}
          <li className={screen.gate.allPassed ? "is-pass is-total" : "is-fail is-total"}>
            <span className="evo-check-mark" aria-hidden="true">
              {screen.gate.allPassed ? "✓" : "✗"}
            </span>
            <span>{screen.gate.allPassed ? "every preregistered criterion passed" : "the preregistered gate was not passed"}</span>
          </li>
        </ul>
      ) : (
        <p className="rchart-empty">The compare reports have not been written yet.</p>
      )}
    </Frame>
  );
}

/* =============================================================== diagrams */

function Box({ x, y, w, h, title, lines, accent = LINE }: { x: number; y: number; w: number; h: number; title: string[]; lines?: string[]; accent?: string }) {
  const titleLines = title.length;
  const bodyLines = lines?.length ?? 0;
  const total = titleLines * 14 + (bodyLines ? 4 + bodyLines * 12 : 0);
  let cursor = y + (h - total) / 2 + 11;
  const nodes: ReactNode[] = [];
  title.forEach((line, i) => {
    nodes.push(
      <text key={`t${i}`} x={x + w / 2} y={cursor} textAnchor="middle" fontSize={11.5} fontWeight={600} fill={INK}>
        {line}
      </text>,
    );
    cursor += 14;
  });
  if (lines) {
    cursor += 2;
    lines.forEach((line, i) => {
      nodes.push(
        <text key={`l${i}`} x={x + w / 2} y={cursor} textAnchor="middle" fontSize={9.5} fill={INK_2}>
          {line}
        </text>,
      );
      cursor += 12;
    });
  }
  return (
    <g>
      <rect x={x} y={y} width={w} height={h} rx={8} fill="#141418" stroke={accent} strokeWidth={1.2} />
      {nodes}
    </g>
  );
}

function Arrow({ d, color = INK_3 }: { d: string; color?: string }) {
  return <path d={d} fill="none" stroke={color} strokeWidth={1.4} markerEnd="url(#evo-arrow)" />;
}

function Defs() {
  return (
    <defs>
      <marker id="evo-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
        <path d="M 0 0 L 10 5 L 0 10 z" fill={INK_3} />
      </marker>
    </defs>
  );
}

export function EvolutionPipeline({ caption }: { caption?: string }) {
  const W = 760;
  const H = 210;
  const boxes = [
    { title: ["Depth-5 teacher", "plays games"], lines: ["frozen fair leaf, 7 strata", "training-lease seeds"], accent: AMBER },
    { title: ["Labelled roots"], lines: ["every position it faced,", "with all 7 column values"], accent: AMBER },
    { title: ["Supervised", "warm start"], lines: ["NNUE fits the teacher's", "values; whole-origin split"], accent: BLUE },
    { title: ["Evolution"], lines: ["60 generations of paired", "games inside depth 3"], accent: GREEN },
    { title: ["Elite", "re-selection"], lines: ["top 8 replay 128 fresh", "games; winner frozen"], accent: GREEN },
    { title: ["Held-out screen"], lines: ["64 never-read games,", "played exactly once"], accent: PINK },
  ];
  const w = 112;
  const gap = 14;
  const x0 = (W - (boxes.length * w + (boxes.length - 1) * gap)) / 2;
  const y = 28;
  const h = 84;
  return (
    <figure className="research-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="The four stages of the experiment, from teacher games to the held-out screen" style={{ fontFamily: FONT }}>
        <Defs />
        {boxes.map((box, i) => {
          const x = x0 + i * (w + gap);
          return (
            <g key={box.title.join(" ")}>
              <Box x={x} y={y} w={w} h={h} title={box.title} lines={box.lines} accent={box.accent} />
              {i < boxes.length - 1 && <Arrow d={`M ${x + w + 1} ${y + h / 2} L ${x + w + gap - 2} ${y + h / 2}`} />}
            </g>
          );
        })}
        {/* data roles */}
        <g fontSize={10} fill={INK_2}>
          <path d={`M ${x0} ${y + h + 18} L ${x0} ${y + h + 26} L ${x0 + 5 * w + 4 * gap} ${y + h + 26} L ${x0 + 5 * w + 4 * gap} ${y + h + 18}`} fill="none" stroke={LINE} />
          <text x={x0 + (5 * w + 4 * gap) / 2} y={y + h + 42} textAnchor="middle">
            training seeds 0xa52e0300 onward: teacher games, every fitness block, the re-selection
          </text>
          <path d={`M ${x0 + 5 * (w + gap)} ${y + h + 18} L ${x0 + 5 * (w + gap)} ${y + h + 26} L ${x0 + 6 * w + 5 * gap} ${y + h + 26} L ${x0 + 6 * w + 5 * gap} ${y + h + 18}`} fill="none" stroke={PINK} />
          <text x={x0 + 5 * (w + gap) + w / 2} y={y + h + 42} textAnchor="middle" fill={PINK}>
            screen seeds
          </text>
          <text x={x0 + 5 * (w + gap) + w / 2} y={y + h + 55} textAnchor="middle" fill={PINK}>
            0xa52e1300, once
          </text>
          <text x={W / 2} y={H - 10} textAnchor="middle" fill={INK_3}>
            stage A · stage B · stage C · stage D — the candidate never sees a screen seed before it is frozen
          </text>
        </g>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

export function GenerationLoop({ caption }: { caption?: string }) {
  const W = 760;
  const H = 340;
  return (
    <figure className="research-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="One generation of the evolutionary loop" style={{ fontFamily: FONT }}>
        <Defs />
        <Box x={40} y={24} w={220} h={64} title={["1 · Population"]} lines={["32 candidate weight sets", "(generation 0: the warm start plus 31 noisy copies)"]} accent={BLUE} />
        <Arrow d="M 262 56 L 296 56" />
        <Box x={300} y={24} w={200} h={64} title={["2 · One fresh seed block"]} lines={["32 seeds nobody has played,", "the same 32 for every candidate"]} accent={AMBER} />
        <Arrow d="M 502 56 L 536 56" />
        <Box x={540} y={24} w={190} h={92} title={["3 · Everyone plays"]} lines={["each candidate is the leaf of the", "depth-3 search on all 32 games;", "fitness = mean final score"]} accent={GREEN} />
        <Arrow d="M 635 118 L 635 168" />
        <Box x={540} y={172} w={190} h={64} title={["4 · Keep the best four"]} lines={["copied unchanged into the", "next generation (elites)"]} accent={GREEN} />
        <Arrow d="M 538 204 L 504 204" />
        <Box x={270} y={160} w={230} h={100} title={["5 · Fill the other 28 slots"]} lines={["draw 3 at random, keep the fittest;", "copy it; add Gaussian noise to every", "weight: σ = 5% of its tensor's spread"]} accent={PINK} />
        <Arrow d="M 268 210 L 150 210 L 150 92" />
        <text x={158} y={130} textAnchor="start" fontSize={10} fill={INK_2}>
          next generation
        </text>
        <text x={158} y={143} textAnchor="start" fontSize={10} fill={INK_2}>
          (4 elites + 28 mutants)
        </text>
        {/* controls */}
        <rect x={40} y={262} width={460} height={58} rx={8} fill="none" stroke={LINE} strokeDasharray="4 3" />
        <text x={56} y={282} fontSize={11} fontWeight={600} fill={INK}>
          Two controls play the same 32 seeds every generation
        </text>
        <text x={56} y={298} fontSize={9.5} fill={INK_2}>
          the frozen fair leaf (is the population beating what it replaced?) and the unevolved warm start
        </text>
        <text x={56} y={310} fontSize={9.5} fill={INK_2}>
          (did evolution move anything at all?). Neither is ever selected or mutated.
        </text>
        <text x={640} y={262} textAnchor="middle" fontSize={9.5} fill={INK_3}>
          checkpoint: population written
        </text>
        <text x={640} y={275} textAnchor="middle" fontSize={9.5} fill={INK_3}>
          before the generation is marked done
        </text>
        <text x={640} y={300} textAnchor="middle" fontSize={9.5} fill={INK_3}>
          after generation 59: top 8 replay
        </text>
        <text x={640} y={313} textAnchor="middle" fontSize={9.5} fill={INK_3}>
          128 fresh games; the winner is frozen
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

export function NnueSketch({ caption }: { caption?: string }) {
  const W = 760;
  const H = 236;
  const cell = 9;
  const gx = 40;
  const gy = 40;
  return (
    <figure className="research-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="How the NNUE turns a position into one number" style={{ fontFamily: FONT }}>
        <Defs />
        {/* schematic board outline: cells only, no disc values */}
        <g>
          {Array.from({ length: 7 }, (_, r) =>
            Array.from({ length: 7 }, (_, c) => (
              <rect key={`${r}-${c}`} x={gx + c * cell} y={gy + r * cell} width={cell - 1} height={cell - 1} fill={r >= 4 && (c + r) % 3 !== 0 ? "#2b2b33" : "#141418"} stroke={LINE} strokeWidth={0.5} />
            )),
          )}
          <text x={gx + 3.5 * cell} y={gy + 7 * cell + 14} textAnchor="middle" fontSize={9.5} fill={INK_2}>
            visible board
          </text>
          <text x={gx + 3.5 * cell} y={gy + 7 * cell + 26} textAnchor="middle" fontSize={9.5} fill={INK_2}>
            next disc · moves to rise
          </text>
        </g>
        <Arrow d={`M ${gx + 7 * cell + 8} ${gy + 3.5 * cell} L 150 ${gy + 3.5 * cell}`} />
        {/* feature column */}
        <g>
          {Array.from({ length: 22 }, (_, i) => (
            <rect key={i} x={158} y={22 + i * 6.4} width={26} height={5} fill={[2, 5, 9, 12, 16, 19].includes(i) ? BLUE : "#1f1f25"} />
          ))}
          <text x={171} y={182} textAnchor="middle" fontSize={9.5} fill={INK_2}>
            8,902 features
          </text>
          <text x={171} y={194} textAnchor="middle" fontSize={9.5} fill={BLUE}>
            135 are on
          </text>
        </g>
        <Arrow d="M 190 92 L 226 92" />
        <Box x={230} y={58} w={150} h={68} title={["Sum 135 rows"]} lines={["of a table with one 64-wide", "row per feature (the big part:", "570k of the 572k weights)"]} accent={BLUE} />
        <Arrow d="M 382 92 L 418 92" />
        <Box x={422} y={66} w={96} h={52} title={["ReLU → 32"]} lines={["small dense layer"]} accent={BLUE} />
        <Arrow d="M 520 92 L 556 92" />
        <Box x={560} y={66} w={80} h={52} title={["ReLU → 1"]} lines={["one number"]} accent={BLUE} />
        <Arrow d="M 642 92 L 678 92" />
        <g>
          <text x={716} y={86} textAnchor="middle" fontSize={11.5} fontWeight={600} fill={INK}>
            × 17,000
          </text>
          <text x={716} y={100} textAnchor="middle" fontSize={9.5} fill={INK_2}>
            points (one rise)
          </text>
        </g>
        <text x={W / 2} y={212} textAnchor="middle" fontSize={10} fill={INK_3}>
          Evolution changes only the 572,000 weights. The features, the shape and the depth-3 search around it stay fixed.
        </text>
        <text x={W / 2} y={228} textAnchor="middle" fontSize={10} fill={INK_3}>
          The network never sees the score, the level, the move number or the seed.
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}

export function PairedSeedsSketch({ caption }: { caption?: string }) {
  const W = 760;
  const H = 190;
  const seeds = 8;
  const x0 = 170;
  const step = 66;
  const rows = [
    { label: "candidate A", y: 62, color: BLUE },
    { label: "candidate B", y: 104, color: GREEN },
    { label: "fair leaf (control)", y: 146, color: AMBER },
  ];
  return (
    <figure className="research-fig">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Every candidate and both controls play the same seeds" style={{ fontFamily: FONT }}>
        <text x={x0 + ((seeds - 1) * step) / 2} y={22} textAnchor="middle" fontSize={11} fontWeight={600} fill={INK}>
          one fitness block: the same 32 seeds for everyone (8 shown)
        </text>
        {Array.from({ length: seeds }, (_, i) => (
          <g key={i}>
            <text x={x0 + i * step} y={42} textAnchor="middle" fontSize={9.5} fill={INK_2}>
              seed {i + 1}
            </text>
            <line x1={x0 + i * step} x2={x0 + i * step} y1={48} y2={160} stroke={LINE} strokeDasharray="2 3" />
          </g>
        ))}
        {rows.map((row) => (
          <g key={row.label}>
            <text x={x0 - 24} y={row.y + 4} textAnchor="end" fontSize={10.5} fill={INK}>
              {row.label}
            </text>
            {Array.from({ length: seeds }, (_, i) => (
              <g key={i}>
                {/* the same disc sequence icon in every row: identical luck */}
                <circle cx={x0 + i * step - 9} cy={row.y} r={5} fill="#2b2b33" stroke={LINE} />
                <circle cx={x0 + i * step + 1} cy={row.y} r={5} fill="#2b2b33" stroke={LINE} />
                <circle cx={x0 + i * step + 11} cy={row.y} r={5} fill={row.color} stroke="none" />
              </g>
            ))}
          </g>
        ))}
        <text x={W / 2 + 30} y={182} textAnchor="middle" fontSize={10} fill={INK_3}>
          same discs, same rises, same reveals in every row: the only thing that differs is the evaluator, so A minus B is the evaluator, not luck
        </text>
      </svg>
      {caption && <figcaption>{caption}</figcaption>}
    </figure>
  );
}
