/**
 * Figures for the ntuple-scale approach page.
 *
 * Data figures read the committed run snapshot
 * (web/content/figures/ntuple-scale/<run-id>.json, written by
 * web/scripts/extract-ntuple-scale.ts from the run's artifacts) and hand its
 * rows to the client charts in ./charts/NTupleCharts.tsx and the shared
 * ScreenPairs chart. Every number they print is a value in that snapshot. A
 * stage whose artifacts do not exist yet renders a visible "not recorded yet"
 * note, never a placeholder number.
 *
 * Server components; styled by the `.research-fig` and `.evo-*` blocks in
 * globals.css.
 */
import type { ReactNode } from "react";
import { readRepoFile } from "@/lib/repo";
import { formatSigned, formatValue } from "@/lib/charts/spec";
import { SNAPSHOT_FORMAT, isFillSelection, type FillReading, type NTupleSnapshot, type TrainingRun } from "@/lib/charts/ntuple-scale";
import { ScreenPairs } from "./charts/EvolutionCharts";
import { PilotArms, TrainingCurve } from "./charts/NTupleCharts";

const RUN = /^RUN-[A-Za-z0-9-]+$/;

const ARM_LABELS: Record<string, string> = {
  "candidate-d3s7": "tables as the depth-3 leaf",
  "candidate-1ply": "tables played directly, one ply",
  "prior-d3s7": "first run's tables as the depth-3 leaf",
  "prior-1ply": "first run's tables played directly, one ply",
  "prior-d4s7": "first run's tables as the depth-4 leaf",
  "fair-d3s7": "fair leaf in the depth-3 search",
  "fair-d4s7": "fair leaf in the depth-4 search",
  "fill-d3s7": "fill-conditioned tables as the depth-3 leaf",
  "fill-1ply": "fill-conditioned tables played directly, one ply",
  "fill-d4s7": "fill-conditioned tables as the depth-4 leaf",
  "control-d3s7": "unconditioned continuation as the depth-3 leaf",
  "zeroed-d3s7": "frozen tables with never-updated entries zeroed, depth-3 leaf",
  "zeroed-d4s7": "frozen tables with never-updated entries zeroed, depth-4 leaf",
  "classmean-d3s7": "frozen tables with never-updated entries at the class mean, depth-3 leaf",
};

const CONTRAST_LABELS: Record<string, string> = {
  "candidate-d3s7-vs-fair-d3s7": "tables as the depth-3 leaf minus the fair leaf in the same search",
  "candidate-1ply-vs-fair-d3s7": "tables played directly minus the fair leaf in the depth-3 search",
  "candidate-d3s7-vs-fair-d4s7": "tables as the depth-3 leaf minus the fair leaf in the depth-4 search",
  "fair-d4s7-vs-fair-d3s7": "fair leaf at depth 4 minus fair leaf at depth 3",
  "candidate-d3s7-vs-candidate-1ply": "tables as the depth-3 leaf minus the same tables played directly",
  "prior-d3s7-vs-fair-d3s7": "first run's tables as the depth-3 leaf minus the fair leaf in the same search",
  "candidate-d3s7-vs-prior-d3s7": "wider tables minus the first run's tables, both as the depth-3 leaf",
  "prior-d3s7-vs-fair-d4s7": "first run's tables as the depth-3 leaf minus the fair leaf in the depth-4 search",
  "prior-1ply-vs-fair-d3s7": "first run's tables played directly minus the fair leaf in the depth-3 search",
  "prior-d4s7-vs-prior-d3s7": "tables as the depth-4 leaf minus the same tables as the depth-3 leaf",
  "prior-d4s7-vs-fair-d4s7": "tables as the depth-4 leaf minus the fair leaf in the same depth-4 search",
  "prior-d4s7-vs-fair-d3s7": "tables as the depth-4 leaf minus the fair leaf in the depth-3 search",
  "fill-d3s7-vs-prior-d3s7": "fill-conditioned tables minus the frozen tables, both as the depth-3 leaf",
  "fill-d3s7-vs-control-d3s7": "fill-conditioned tables minus the unconditioned continuation, both as the depth-3 leaf",
  "control-d3s7-vs-prior-d3s7": "unconditioned continuation minus the frozen tables, both as the depth-3 leaf",
  "zeroed-d3s7-vs-prior-d3s7": "zeroed edit minus the frozen tables, both as the depth-3 leaf",
  "classmean-d3s7-vs-prior-d3s7": "class-mean edit minus the frozen tables, both as the depth-3 leaf",
  "fill-d4s7-vs-prior-d4s7": "fill-conditioned tables minus the frozen tables, both as the depth-4 leaf",
  "zeroed-d4s7-vs-prior-d4s7": "zeroed edit minus the frozen tables, both as the depth-4 leaf",
  "fill-d4s7-vs-fill-d3s7": "fill-conditioned tables as the depth-4 leaf minus the same tables as the depth-3 leaf",
  "zeroed-d4s7-vs-zeroed-d3s7": "zeroed edit as the depth-4 leaf minus the same tables as the depth-3 leaf",
  "fill-d3s7-vs-fair-d3s7": "fill-conditioned tables as the depth-3 leaf minus the fair leaf in the same search",
  "fill-1ply-vs-prior-1ply": "fill-conditioned tables played directly minus the frozen tables played directly",
  "fill-1ply-vs-fair-d3s7": "fill-conditioned tables played directly minus the fair leaf in the depth-3 search",
};

const FILL_READING_LABELS: [keyof Pick<NonNullable<NTupleSnapshot["screen"]>["fill"] extends infer F ? (F extends null ? never : F) : never, "conditioning" | "continuation" | "zeroed" | "classmean" | "fillDepth4" | "zeroedDepth4">, string][] = [
  ["conditioning", "conditioning: fill-conditioned tables minus the unconditioned continuation, depth 3"],
  ["continuation", "continuation: unconditioned continuation minus the frozen tables, depth 3"],
  ["zeroed", "zeroed edit minus the frozen tables, depth 3"],
  ["classmean", "class-mean edit minus the frozen tables, depth 3"],
  ["fillDepth4", "fill-conditioned tables minus the frozen tables, depth 4"],
  ["zeroedDepth4", "zeroed edit minus the frozen tables, depth 4"],
];

function loadSnapshot(run: string): NTupleSnapshot | null {
  if (!RUN.test(run)) return null;
  const raw = readRepoFile(`web/content/figures/ntuple-scale/${run}.json`);
  if (!raw) return null;
  try {
    const parsed = JSON.parse(raw) as NTupleSnapshot;
    return parsed.format === SNAPSHOT_FORMAT ? parsed : null;
  } catch {
    return null;
  }
}

function snapshotPath(run: string) {
  return `web/content/figures/ntuple-scale/${run}.json`;
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
            Values copied from the run snapshot <code>{snapshotPath(run)}</code>, which <code>web/scripts/extract-ntuple-scale.ts</code> writes from the run&apos;s artifacts
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
            <code>node --experimental-strip-types web/scripts/extract-ntuple-scale.ts --run {run}</code> once it has.
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

function minutes(seconds: number): string {
  return seconds >= 3600 ? `${(seconds / 3600).toFixed(1)} h` : `${(seconds / 60).toFixed(0)} min`;
}

type StageState = "done" | "running" | "pending";

/* ============================================================ status strip */

function plateauDetail(main: TrainingRun): string {
  const last = main.validations[main.validations.length - 1];
  if (main.stop) {
    if (main.stop.reason === "plateau" && main.stop.recentWindowMean !== null && main.stop.previousWindowMean !== null) {
      return `; stopped by the plateau rule at point ${main.stop.validationPoints}: last ${main.stop.plateauWindow} points mean ${formatSigned(Math.round(main.stop.recentWindowMean))}, the ${main.stop.plateauWindow} before ${formatSigned(Math.round(main.stop.previousWindowMean))}`;
    }
    return `; stopped by the ${main.stop.reason} rule`;
  }
  if (last?.plateau) {
    return `; plateau rule at point ${last.plateau.points}: last ${last.plateau.window} points mean ${formatSigned(Math.round(last.plateau.recentMean))}, previous ${formatSigned(Math.round(last.plateau.previousMean))} (training continues while the last window is above the previous one)`;
  }
  return "";
}

export function NTupleStatus({ run }: { run: string }) {
  const snapshot = loadSnapshot(run);
  if (!snapshot) return <Absent run={run} stage="whole" />;
  const { gates, smoke, pilot, main, freeze, screen } = snapshot;
  const pilotArms = pilot ? Object.values(pilot.arms) : [];
  const pilotDone = pilotArms.filter((a) => a.done).length;
  const replication = !pilot && (smoke !== null || Boolean(screen?.replication) || Boolean(main?.stop));
  const depthOnly = !pilot && !smoke && !main && (Boolean(screen?.depth) || Boolean(freeze?.priorSha256));
  const fillShaped = Boolean(pilot && ("occ5" in pilot.arms || "hgt5" in pilot.arms || "control" in pilot.arms));
  if (fillShaped && pilot) {
    const selection = isFillSelection(pilot.selection) ? pilot.selection : null;
    const armNames = Object.keys(pilot.arms);
    const fill = screen?.fill ?? null;
    const edits = snapshot.edits ?? null;
    const stages: { name: string; state: StageState; detail: string }[] = [
      { name: "0 · CHECK gates", state: gates ? "done" : "pending", detail: gates ? (gates.passed && (snapshot.gatesHgt5?.passed ?? true) ? `${gates.gates.length} gates passed on the probe block for each fill layout` : "a gate failed") : "not run" },
      {
        name: "A · two edits of the frozen tables",
        state: edits ? "done" : "pending",
        detail: edits ? `${formatValue(edits.untouchedEntries)} of ${formatValue(edits.entries)} entries were never updated; zeroed edit changed ${formatValue(edits.outputs.zeroed?.entriesChanged ?? 0)}, class-mean edit ${formatValue(edits.outputs.classmean?.entriesChanged ?? 0)}` : "waits for the gates",
      },
      {
        name: "B · three warm-started arms",
        state: (pilotDone === armNames.length && pilotDone > 0 ? "done" : pilotDone > 0 || pilotArms.some((a) => a.chunks.length > 0) ? "running" : "pending") as StageState,
        detail: `${pilotDone} of ${armNames.length} arms done${pilotArms.map((a) => ` · ${a.name}: ${a.validations.length} validation points${a.bestMargin !== null ? `, best margin ${formatSigned(Math.round(a.bestMargin))}` : ""}${a.stop ? ` (${a.stop.reason})` : ""}`).join("")}`,
      },
      {
        name: "C · selection and freeze",
        state: freeze?.candidateSha256 ? "done" : selection ? "running" : "pending",
        detail: selection ? `candidate arm ${selection.candidateArm}, best margin ${formatSigned(Math.round(selection.candidateBestMargin))} against the control arm's ${formatSigned(Math.round(selection.controlBestMargin))}; training-signal check ${selection.trainingSignal.passed ? "passed" : "not passed"}${freeze?.candidateSha256 ? `; candidate SHA-256 ${freeze.candidateSha256.slice(0, 12)}…, gates re-run on every frozen file` : ""}` : "waits for the three arms",
      },
      {
        name: "D · held-out screen, eleven arms",
        state: screen ? "done" : "pending",
        detail: screen
          ? screen.gate
            ? `preregistered gate ${screen.gate.allPassed ? "passed" : "not passed"}${fill?.conditioning ? `; conditioning ${fill.conditioning.verdict}` : ""}${fill?.zeroed ? `; zeroed edit ${fill.zeroed.verdict}` : ""}${fill?.fillDepth4 ? `; at depth 4 ${fill.fillDepth4.verdict}` : ""}`
            : "played; contrasts pending"
          : "opens once, after every table file is hashed and gated",
      },
    ];
    return <StatusFrame run={run} snapshot={snapshot} stages={stages} />;
  }
  if (depthOnly) {
    const depth = screen?.depth ?? null;
    const stages: { name: string; state: StageState; detail: string }[] = [
      { name: "0 · CHECK gates", state: gates ? "done" : "pending", detail: gates ? (gates.passed ? `${gates.gates.length} gates passed on the probe block, on the frozen tables` : "a gate failed") : "not run" },
      {
        name: "A · freeze",
        state: freeze?.priorSha256 ? "done" : "pending",
        detail: freeze?.priorSha256 ? `the first run's tables verified unchanged, SHA-256 ${freeze.priorSha256.slice(0, 12)}…; nothing is trained` : "waits for the gates",
      },
      {
        name: "B · held-out screen, four arms",
        state: screen ? "done" : "pending",
        detail: screen
          ? screen.gate
            ? `preregistered gate ${screen.gate.allPassed ? "passed" : "not passed"}${depth?.persistence ? `; margin over the fair leaf at depth 4 ${depth.persistence.allPassed ? "kept" : "not kept"}` : ""}${depth ? `; depth-step interaction ${depth.interaction.verdict}` : ""}`
            : "played; contrasts pending"
          : "opens once, after the tables are verified",
      },
    ];
    return <StatusFrame run={run} snapshot={snapshot} stages={stages} />;
  }
  const stageA = replication
    ? {
        name: "A · throughput smoke",
        state: (smoke ? (smoke.done ? "done" : "running") : "pending") as StageState,
        detail: smoke ? `${formatValue(smoke.movesTotal)} moves on the probe block at ${formatValue(Math.round(smoke.meanMovesPerSecond ?? 0))} moves per second, ${formatValue(smoke.entries)} table entries; tables discarded` : "waits for the gates",
      }
    : {
        name: "A · pilot arms",
        state: (pilot ? (pilotDone === 6 ? "done" : "running") : "pending") as StageState,
        detail: pilot ? `${pilotDone} of 6 arms trained for ${formatValue(pilotArms[0]?.movesTotal ?? 0)} moves${pilot.selection && !isFillSelection(pilot.selection) ? `; arm ${pilot.selection.arm} selected` : ""}` : "waits for the gates",
      };
  const stages: { name: string; state: StageState; detail: string }[] = [
    { name: "0 · CHECK gates", state: gates ? "done" : "pending", detail: gates ? (gates.passed ? `${gates.gates.length} gates passed on the probe block` : "a gate failed") : "not run" },
    stageA,
    {
      name: replication ? "B · main run, until the margins plateau" : "B · main run",
      state: main ? (main.done ? "done" : "running") : "pending",
      detail: main
        ? `${formatValue(main.movesTotal)} moves, ${main.validations.length} validation points on ${main.validateGames} paired games${main.best ? `; best margin ${formatSigned(Math.round(main.best.pairedDeltaD3))} at ${formatValue(main.best.moves)} moves` : ""}${replication ? plateauDetail(main) : ""}`
        : replication
          ? "waits for the smoke run"
          : "waits for the pilot selection",
    },
    {
      name: "C · freeze",
      state: freeze?.candidateSha256 ? "done" : "pending",
      detail: freeze?.candidateSha256 ? `candidate frozen, SHA-256 ${freeze.candidateSha256.slice(0, 12)}…${freeze.priorSha256 ? `; first run's tables verified, SHA-256 ${freeze.priorSha256.slice(0, 12)}…` : ""}` : "waits for the main run",
    },
    {
      name: "D · held-out screen",
      state: screen ? "done" : "pending",
      detail: screen
        ? screen.gate
          ? `preregistered gate ${screen.gate.allPassed ? "passed" : "not passed"}${screen.replication ? `; replication of the first run ${screen.replication.allPassed ? "passed" : "not passed"}` : ""}${screen.scale ? `; scale verdict ${screen.scale.verdict}` : ""}`
          : "played; contrasts pending"
        : "opens once, after the candidate is frozen",
    },
  ];
  return <StatusFrame run={run} snapshot={snapshot} stages={stages} />;
}

function StatusFrame({ run, snapshot, stages }: { run: string; snapshot: NTupleSnapshot; stages: { name: string; state: StageState; detail: string }[] }) {
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

function trainingStats(run: TrainingRun) {
  const last = run.validations[run.validations.length - 1];
  const lastChunk = run.chunks[run.chunks.length - 1];
  return [
    { label: "training moves", value: formatValue(run.movesTotal) },
    { label: "training games", value: formatValue(run.gamesTotal) },
    { label: "table entries", value: formatValue(run.entries) },
    { label: "moves per second", value: run.meanMovesPerSecond ? formatValue(Math.round(run.meanMovesPerSecond)) : "n/a" },
    { label: "wall time", value: minutes(run.wallSeconds) },
    ...(lastChunk ? [{ label: "training-game mean (latest chunk)", value: `${formatValue(Math.round(lastChunk.trainMeanScore))} / ${lastChunk.trainMeanMoves.toFixed(1)} moves` }] : []),
    ...(last
      ? [
          { label: "latest validation, tables as leaf", value: formatValue(Math.round(last.ntupleD3Mean)) },
          { label: "latest validation, fair leaf", value: formatValue(Math.round(last.fairD3Mean)) },
          { label: "latest paired margin", value: `${formatSigned(Math.round(last.pairedDeltaD3))} (lower bound ${formatSigned(Math.round(last.bootstrapLower95))})` },
          { label: "wins / ties / losses", value: last.wtl.join(" / ") },
          ...(last.plateau ? [{ label: `plateau rule, mean of the last ${last.plateau.window} points / the ${last.plateau.window} before`, value: `${formatSigned(Math.round(last.plateau.recentMean))} / ${formatSigned(Math.round(last.plateau.previousMean))}${last.plateau.stop ? " (stopped)" : ""}` }] : []),
          ...(last.directMean !== null ? [{ label: "latest validation, direct play", value: formatValue(Math.round(last.directMean)) }] : []),
          ...(last.touchedEntries !== null ? [{ label: "table entries updated at least once", value: formatValue(last.touchedEntries) }] : []),
        ]
      : []),
    ...(run.best ? [{ label: "best validation point", value: `${formatSigned(Math.round(run.best.pairedDeltaD3))} at ${formatValue(run.best.moves)} moves` }] : []),
    ...(run.stop ? [{ label: "stopped by", value: `the ${run.stop.reason} rule after ${run.stop.validationPoints} validation points` }] : []),
    { label: "validation block", value: `${run.validateGames} paired games, training role` },
    { label: "illegal / incomplete decisions", value: `${run.illegalDecisions} / ${run.incompleteDecisions}` },
  ];
}

export function NTupleTrainingFigure({ run, arm = "main", caption }: { run: string; arm?: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const training = arm === "main" ? snapshot?.main : snapshot?.pilot?.arms[arm];
  if (!snapshot || !training || training.chunks.length === 0) return <Absent run={run} stage={arm === "main" ? "main training" : `pilot arm ${arm}`} caption={caption} />;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes(arm === "main" ? "/main/" : `/pilot/${arm}/`) || s.endsWith("analysis.json"))}>
      <h4 className="rchart-title">{arm === "main" ? "Main run" : `Pilot arm ${arm}`}: the validation line-up as training proceeds</h4>
      <TrainingCurve run={training} source={snapshot.runId} />
      <Stats items={trainingStats(training)} />
    </Frame>
  );
}

export function NTuplePilotFigure({ run, caption }: { run: string; caption?: string }) {
  const snapshot = loadSnapshot(run);
  const pilot = snapshot?.pilot;
  if (!snapshot || !pilot || Object.values(pilot.arms).every((a) => a.validations.length === 0)) return <Absent run={run} stage="pilot" caption={caption} />;
  return (
    <Frame run={run} caption={caption} sources={snapshot.sources.filter((s) => s.includes("/pilot/") || s.endsWith("analysis.json"))}>
      <h4 className="rchart-title">{isFillSelection(pilot.selection) || "control" in pilot.arms ? "Three warm-started arms, one rule each" : "Six configurations, one budget each"}</h4>
      <PilotArms pilot={pilot} source={snapshot.runId} />
      <Stats
        items={[
          ...Object.entries(pilot.arms).map(([name, a]) => ({
            label: `${name}: ${a.layout}, alpha ${a.alpha}`,
            value: a.finalMargin !== null ? `final ${formatSigned(Math.round(a.finalMargin))}${a.bestMargin !== null ? `, best ${formatSigned(Math.round(a.bestMargin))}${a.best ? ` at ${formatValue(a.best.moves)} moves` : ""}` : ""} after ${formatValue(a.movesTotal)} moves${a.done ? "" : " (running)"}` : "no validation point yet",
          })),
          ...(pilot.selection
            ? isFillSelection(pilot.selection)
              ? [
                  { label: "selected fill candidate", value: `arm ${pilot.selection.candidateArm}, best margin ${formatSigned(Math.round(pilot.selection.candidateBestMargin))} at ${formatValue(pilot.selection.candidateBestMoves ?? 0)} moves` },
                  { label: "control candidate", value: `best margin ${formatSigned(Math.round(pilot.selection.controlBestMargin))} at ${formatValue(pilot.selection.controlBestMoves ?? 0)} moves` },
                  { label: "training-signal check", value: pilot.selection.trainingSignal.passed ? "passed: a fill arm beat the control arm's best margin" : "not passed: neither fill arm beat the control arm's best margin" },
                ]
              : [{ label: "selected", value: `arm ${pilot.selection.arm} (${pilot.selection.layout}, alpha ${pilot.selection.alpha})` }]
            : []),
        ]}
      />
    </Frame>
  );
}

export function NTupleScreenFigure({ run, contrast = "candidate-d3s7-vs-fair-d3s7", caption }: { run: string; contrast?: string; caption?: string }) {
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
        <ScreenPairs contrast={paired} arms={screen.arms} candidateLabel={candidateLabel} referenceLabel={referenceLabel} source={snapshot.runId} />
      ) : (
        <p className="rchart-empty">Per-game rows were not retained in the snapshot; the recorded contrast is below.</p>
      )}
      <Stats
        items={[
          { label: "paired mean difference", value: `${formatSigned(paired.meanDelta)} points` },
          { label: "bootstrap 95% lower bound", value: formatSigned(paired.bootstrapLower95) },
          { label: "Student-t 95% lower bound", value: formatSigned(paired.studentTLower95) },
          { label: "bootstrap 95% upper bound", value: formatSigned(paired.bootstrapUpper95) },
          { label: "wins / ties / losses", value: paired.wtl.join(" / ") },
          { label: "first half / second half", value: `${formatSigned(paired.halves[0])} / ${formatSigned(paired.halves[1])}` },
          { label: "paired sd", value: formatValue(paired.pairedSd) },
          { label: "detection floor", value: formatValue(paired.detectionFloor) },
          { label: "moves, paired mean difference", value: formatSigned(paired.movesDelta) },
          { label: `${candidateLabel}: mean`, value: formatValue(screen.arms[paired.candidateArm]?.mean ?? 0) },
          { label: `${referenceLabel}: mean`, value: formatValue(screen.arms[paired.referenceArm]?.mean ?? 0) },
        ]}
      />
    </Frame>
  );
}

export function NTupleGateTable({ run }: { run: string }) {
  const snapshot = loadSnapshot(run);
  const screen = snapshot?.screen;
  if (!snapshot || !screen) return <Absent run={run} stage="held-out screen" />;
  return (
    <Frame run={run} sources={snapshot.sources.filter((s) => s.includes("screen"))}>
      <h4 className="rchart-title">The screen arms and the preregistered gate</h4>
      <div className="evo-table-wrap">
        <table className="evo-table">
          <thead>
            <tr>
              <th>arm</th>
              <th>mean</th>
              <th>median</th>
              <th>lower quartile</th>
              <th>best game</th>
              <th>moves</th>
              <th>clears / move</th>
              <th>reveals / move</th>
            </tr>
          </thead>
          <tbody>
            {Object.entries(screen.arms).map(([name, a]) => (
              <tr key={name}>
                <td>{ARM_LABELS[name] ?? name}</td>
                <td>{formatValue(a.mean)}</td>
                <td>{formatValue(a.median)}</td>
                <td>{formatValue(a.q25)}</td>
                <td>{formatValue(a.max)}</td>
                <td>{a.movesMean.toFixed(1)}</td>
                <td>{a.clearsPerMove.toFixed(3)}</td>
                <td>{a.revealsPerMove.toFixed(3)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {screen.gate && (
        <ul className="evo-gate">
          {screen.gate.checks.map((check) => (
            <li key={check.criterion} className={check.passed ? "is-pass" : "is-fail"}>
              <span className="evo-gate-mark">{check.passed ? "pass" : "fail"}</span> {check.criterion}
              {check.observed !== undefined && check.observed !== null && check.observed !== "" ? <> · observed {typeof check.observed === "number" ? formatSigned(check.observed) : JSON.stringify(check.observed)}</> : null}
            </li>
          ))}
          <li className={screen.gate.allPassed ? "is-pass" : "is-fail"}>
            <span className="evo-gate-mark">{screen.gate.allPassed ? "pass" : "fail"}</span> every criterion
          </li>
        </ul>
      )}
      {screen.depth && (
        <>
          <h4 className="rchart-title">The fourth ply on the tables and on the fair leaf</h4>
          <p className="rchart-note">
            The fourth ply&apos;s paired gain on the tables is {formatSigned(Math.round(screen.depth.tablesStep.meanDelta))} (bounds {formatSigned(Math.round(screen.depth.tablesStep.bootstrapLower95))} to {formatSigned(Math.round(screen.depth.tablesStep.bootstrapUpper95))})
            {screen.depth.fairStep ? <>, and on the fair leaf on the same games {formatSigned(Math.round(screen.depth.fairStep.meanDelta))} (bounds {formatSigned(Math.round(screen.depth.fairStep.bootstrapLower95))} to {formatSigned(Math.round(screen.depth.fairStep.bootstrapUpper95))})</> : null}
            . Their per-game difference is {formatSigned(Math.round(screen.depth.interaction.meanDelta))} with bounds {formatSigned(Math.round(screen.depth.interaction.bootstrapLower95))} to {formatSigned(Math.round(screen.depth.interaction.bootstrapUpper95))} and a detection floor of {formatValue(Math.round(screen.depth.interaction.detectionFloor))}; the preregistered verdict is <strong>{screen.depth.interaction.verdict}</strong>.
          </p>
          {screen.depth.persistence && (
            <>
              <h4 className="rchart-title">Persistence: the tables against the fair leaf, both at depth 4</h4>
              <ul className="evo-gate">
                {screen.depth.persistence.checks.map((check) => (
                  <li key={`persistence-${check.criterion}`} className={check.passed ? "is-pass" : "is-fail"}>
                    <span className="evo-gate-mark">{check.passed ? "pass" : "fail"}</span> {check.criterion}
                    {check.observed !== undefined && check.observed !== null && check.observed !== "" ? <> · observed {typeof check.observed === "number" ? formatSigned(check.observed) : JSON.stringify(check.observed)}</> : null}
                  </li>
                ))}
                <li className={screen.depth.persistence.allPassed ? "is-pass" : "is-fail"}>
                  <span className="evo-gate-mark">{screen.depth.persistence.allPassed ? "pass" : "fail"}</span> every criterion
                </li>
              </ul>
            </>
          )}
        </>
      )}
      {screen.fill && (
        <>
          <h4 className="rchart-title">The readings beside the gate, each with its preregistered verdict</h4>
          <div className="evo-table-wrap">
            <table className="evo-table">
              <thead>
                <tr>
                  <th>reading</th>
                  <th>verdict</th>
                  <th>paired mean</th>
                  <th>bootstrap lower bound</th>
                  <th>upper bound</th>
                  <th>detection floor</th>
                  <th>wins / ties / losses</th>
                </tr>
              </thead>
              <tbody>
                {FILL_READING_LABELS.map(([key, label]) => {
                  const r = screen.fill?.[key] as FillReading | null | undefined;
                  if (!r) return null;
                  return (
                    <tr key={key}>
                      <td>{label}</td>
                      <td>
                        <strong>{r.verdict}</strong>
                        {r.allPassed !== undefined ? ` (four criteria ${r.allPassed ? "passed" : "not passed"})` : ""}
                      </td>
                      <td>{formatSigned(Math.round(r.meanDelta))}</td>
                      <td>{formatSigned(Math.round(r.bootstrapLower95))}</td>
                      <td>{formatSigned(Math.round(r.bootstrapUpper95))}</td>
                      <td>{formatValue(Math.round(r.detectionFloor))}</td>
                      <td>{r.wtl.join(" / ")}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </>
      )}
      {screen.replication && (
        <>
          <h4 className="rchart-title">Replication: the first run&apos;s frozen tables on this fresh block</h4>
          <ul className="evo-gate">
            {screen.replication.checks.map((check) => (
              <li key={check.criterion} className={check.passed ? "is-pass" : "is-fail"}>
                <span className="evo-gate-mark">{check.passed ? "pass" : "fail"}</span> {check.criterion}
                {check.observed !== undefined && check.observed !== null && check.observed !== "" ? <> · observed {typeof check.observed === "number" ? formatSigned(check.observed) : JSON.stringify(check.observed)}</> : null}
              </li>
            ))}
            <li className={screen.replication.allPassed ? "is-pass" : "is-fail"}>
              <span className="evo-gate-mark">{screen.replication.allPassed ? "pass" : "fail"}</span> every replication criterion
            </li>
          </ul>
        </>
      )}
      {screen.scale && (
        <>
          <h4 className="rchart-title">Scale: the wider tables against the first run&apos;s tables, same seeds</h4>
          <Stats
            items={[
              { label: "preregistered verdict", value: screen.scale.verdict },
              { label: "paired mean difference", value: `${formatSigned(screen.scale.meanDelta)} points` },
              { label: "bootstrap 95% lower bound", value: formatSigned(screen.scale.bootstrapLower95) },
              { label: "Student-t 95% lower bound", value: formatSigned(screen.scale.studentTLower95) },
              { label: "bootstrap 95% upper bound", value: formatSigned(screen.scale.bootstrapUpper95) },
              { label: "detection floor", value: formatValue(screen.scale.detectionFloor) },
              { label: "wins / ties / losses", value: screen.scale.wtl.join(" / ") },
            ]}
          />
        </>
      )}
    </Frame>
  );
}
