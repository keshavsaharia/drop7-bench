"use client";
/**
 * Thin adapters from ntuple-scale snapshot rows (web/lib/charts/ntuple-scale.ts)
 * to the chart kinds. Each adapter builds a figure spec from rows the snapshot
 * carries and hands it to a kind; it draws nothing itself and prints no number
 * the snapshot does not hold. The screen contrast reuses ScreenPairs from
 * EvolutionCharts, whose contrast shape the ntuple-scale snapshot shares.
 *
 * `source` is the run id the snapshot was cut from, so every tooltip's source
 * line names the run.
 */
import type { FigureSpec } from "@/lib/charts/spec";
import type { PilotStage, TrainingRun } from "@/lib/charts/ntuple-scale";
import { LineChart } from "./kinds/LineChart";
import { ResearchChart } from "./ResearchChart";
import { SERIES } from "./tokens";

const FALLBACK_SOURCE = "RUN-snapshot";

function millions(moves: number): number {
  return Math.round(moves / 1e4) / 100;
}

/** Validation line-up by training moves: the tables inside the depth-3 search,
 * the tables played directly, and the fair leaf inside the same search on the
 * same validation games, with the training games' own mean as context. */
export function TrainingCurve({ run, source = FALLBACK_SOURCE }: { run: TrainingRun; source?: string }) {
  const v = run.validations;
  const n = run.validateGames;
  const quick = run.chunks.filter((c) => typeof c.quickDirectMean === "number");
  const spec: FigureSpec = {
    title: `Validation scores by training moves (${run.name}: ${run.layout}, alpha ${run.alpha})`,
    kind: "line",
    x: { label: "training moves", unit: "millions" },
    y: { label: `mean score of ${n} paired games`, unit: "points" },
    series: [
      {
        name: "tables as the depth-3 leaf",
        role: "primary",
        sourceRecord: source,
        sourceField: `${run.name}.validations[].ntupleD3Mean`,
        points: v.map((p) => ({
          x: millions(p.movesTrained),
          y: p.ntupleD3Mean,
          n,
          wtl: p.wtl,
          label: `paired margin over the fair leaf ${p.pairedDeltaD3 >= 0 ? "+" : ""}${Math.round(p.pairedDeltaD3).toLocaleString()} (bootstrap lower bound ${Math.round(p.bootstrapLower95).toLocaleString()})${p.plateau ? `; plateau rule: last ${p.plateau.window} points mean ${Math.round(p.plateau.recentMean).toLocaleString()}, previous ${Math.round(p.plateau.previousMean).toLocaleString()}${p.plateau.stop ? ", stopped" : ""}` : ""}${p.isBest ? "; frozen as the candidate" : ""}`,
          sourceRecord: source,
        })),
      },
      {
        name: "fair leaf in the same search (same games)",
        role: "control",
        sourceRecord: source,
        sourceField: `${run.name}.validations[].fairD3Mean`,
        points: v.map((p) => ({ x: millions(p.movesTrained), y: p.fairD3Mean, n, sourceRecord: source })),
      },
      {
        name: "tables played directly, one ply",
        role: "context",
        sourceRecord: source,
        sourceField: `${run.name}.chunks[].quickDirectMean`,
        points: quick.map((c) => ({ x: millions(c.movesTotal), y: c.quickDirectMean as number, n, sourceRecord: source })),
      },
      {
        name: "training games (one-ply play, mean per chunk)",
        role: "context",
        sourceRecord: source,
        sourceField: `${run.name}.chunks[].trainMeanScore`,
        points: run.chunks.map((c) => ({ x: millions(c.movesTotal), y: c.trainMeanScore, label: `${c.gamesTotal.toLocaleString()} games so far; ${Math.round(c.movesPerSecond).toLocaleString()} moves per second`, sourceRecord: source })),
      },
    ],
  };
  const overrides = [undefined, undefined, { color: SERIES(2) }, { color: SERIES(7), thin: true }];
  return <LineChart spec={spec} overrides={overrides} />;
}

/** The six pilot arms' final paired margins over the fair leaf. */
export function PilotArms({ pilot, source = FALLBACK_SOURCE }: { pilot: PilotStage; source?: string }) {
  const rows = Object.entries(pilot.arms).filter(([, arm]) => arm.validations.length > 0);
  const spec: FigureSpec = {
    title: "Pilot arms: paired margin over the fair leaf at the final validation point",
    kind: "delta",
    orientation: "horizontal",
    x: { label: "arm" },
    y: { label: "tables in d3s7 minus fair leaf in d3s7, 64 paired games", unit: "points" },
    series: [
      {
        name: "final validation margin",
        role: "primary",
        sourceRecord: source,
        sourceField: "pilot.arms[].validations[-1].pairedDeltaD3",
        points: rows.map(([name, arm]) => {
          const last = arm.validations[arm.validations.length - 1];
          return {
            x: `${name}: ${arm.layout}, alpha ${arm.alpha}`,
            y: last.pairedDeltaD3,
            lo: last.bootstrapLower95,
            hi: last.bootstrapUpper95,
            floor: last.detectionFloor,
            wtl: last.wtl,
            n: arm.validateGames,
            label: `${arm.entries.toLocaleString()} table entries; ${last.movesTrained.toLocaleString()} training moves${pilot.selection?.arm === name ? "; selected for the main run" : ""}`,
            sourceRecord: source,
          };
        }),
      },
    ],
  };
  return <ResearchChart spec={spec} />;
}
