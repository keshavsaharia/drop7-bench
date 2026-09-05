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
import { SNAPSHOT_FORMAT, type NTupleSnapshot, type TrainingRun } from "@/lib/charts/ntuple-scale";
import { ScreenPairs } from "./charts/EvolutionCharts";
import { PilotArms, TrainingCurve } from "./charts/NTupleCharts";

const RUN = /^RUN-[A-Za-z0-9-]+$/;

const ARM_LABELS: Record<string, string> = {
  "candidate-d3s7": "tables as the depth-3 leaf",
  "candidate-1ply": "tables played directly, one ply",
  "fair-d3s7": "fair leaf in the depth-3 search",
  "fair-d4s7": "fair leaf in the depth-4 search",
};

const CONTRAST_LABELS: Record<string, string> = {
  "candidate-d3s7-vs-fair-d3s7": "tables as the depth-3 leaf minus the fair leaf in the same search",
  "candidate-1ply-vs-fair-d3s7": "tables played directly minus the fair leaf in the depth-3 search",
  "candidate-d3s7-vs-fair-d4s7": "tables as the depth-3 leaf minus the fair leaf in the depth-4 search",
  "fair-d4s7-vs-fair-d3s7": "fair leaf at depth 4 minus fair leaf at depth 3",
  "candidate-d3s7-vs-candidate-1ply": "tables as the depth-3 leaf minus the same tables played directly",
};

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

export function NTupleStatus({ run }: { run: string }) {
  const snapshot = loadSnapshot(run);
  if (!snapshot) return <Absent run={run} stage="whole" />;
  const { gates, pilot, main, freeze, screen } = snapshot;
  const pilotArms = pilot ? Object.values(pilot.arms) : [];
  const pilotDone = pilotArms.filter((a) => a.done).length;
  const stages: { name: string; state: StageState; detail: string }[] = [
    { name: "0 · CHECK gates", state: gates ? "done" : "pending", detail: gates ? (gates.passed ? `${gates.gates.length} gates passed on the probe block` : "a gate failed") : "not run" },
    {
      name: "A · pilot arms",
      state: pilot ? (pilotDone === 6 ? "done" : "running") : "pending",
      detail: pilot ? `${pilotDone} of 6 arms trained for ${formatValue(pilotArms[0]?.movesTotal ?? 0)} moves${pilot.selection ? `; arm ${pilot.selection.arm} selected` : ""}` : "waits for the gates",
    },
    {
      name: "B · main run",
      state: main ? (main.done ? "done" : "running") : "pending",
      detail: main ? `${formatValue(main.movesTotal)} moves, ${main.validations.length} validation points${main.best ? `; best margin ${formatSigned(main.best.pairedDeltaD3)} at ${formatValue(main.best.moves)} moves` : ""}` : "waits for the pilot selection",
    },
    { name: "C · freeze", state: freeze?.candidateSha256 ? "done" : "pending", detail: freeze?.candidateSha256 ? `candidate frozen, SHA-256 ${freeze.candidateSha256.slice(0, 12)}…` : "waits for the main run" },
    { name: "D · held-out screen", state: screen ? "done" : "pending", detail: screen ? (screen.gate ? `preregistered gate ${screen.gate.allPassed ? "passed" : "not passed"}` : "played; contrasts pending") : "opens once, after the candidate is frozen" },
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
          ...(last.directMean !== null ? [{ label: "latest validation, direct play", value: formatValue(Math.round(last.directMean)) }] : []),
          ...(last.touchedEntries !== null ? [{ label: "table entries updated at least once", value: formatValue(last.touchedEntries) }] : []),
        ]
      : []),
    ...(run.best ? [{ label: "best validation point", value: `${formatSigned(Math.round(run.best.pairedDeltaD3))} at ${formatValue(run.best.moves)} moves` }] : []),
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
      <h4 className="rchart-title">Six configurations, one budget each</h4>
      <PilotArms pilot={pilot} source={snapshot.runId} />
      <Stats
        items={[
          ...Object.entries(pilot.arms).map(([name, a]) => ({
            label: `${name}: ${a.layout}, alpha ${a.alpha}`,
            value: a.finalMargin !== null ? `${formatSigned(Math.round(a.finalMargin))} at ${formatValue(a.movesTotal)} moves${a.done ? "" : " (running)"}` : "no validation point yet",
          })),
          ...(pilot.selection ? [{ label: "selected", value: `arm ${pilot.selection.arm} (${pilot.selection.layout}, alpha ${pilot.selection.alpha})` }] : []),
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
    </Frame>
  );
}
