"use client";
/**
 * Thin adapters from nnue-evolution snapshot rows (web/lib/charts/evolution.ts)
 * to the chart kinds. Each adapter builds a figure spec object from rows the
 * snapshot carries and hands it to a kind; it draws nothing itself and
 * prints no number the snapshot does not hold. Reference lines are recorded
 * values (a median, a paired mean with its bootstrap band, the frozen
 * epoch), never computed here. The population band on the evolution curve
 * is the envelope of the plotted candidates: the lowest and highest fitness
 * in each generation, which the swarm chart draws individually.
 *
 * `source` is the run id the snapshot was cut from; pages that know it pass
 * it so every tooltip's source line names the run.
 */
import type { FigureSpec } from "@/lib/charts/spec";
import type { CorpusGame, Generation, PairedContrast, PretrainEpoch, ScreenArm } from "@/lib/charts/evolution";
import { formatValue } from "@/lib/charts/spec";
import { LineChart } from "./kinds/LineChart";
import { PairedDeltas } from "./kinds/PairedDeltas";
import { Strip } from "./kinds/Strip";
import { SERIES } from "./tokens";

const FALLBACK_SOURCE = "RUN-snapshot";

function hours(seconds: number): string {
  if (seconds < 90) return `${seconds.toFixed(0)} s`;
  if (seconds < 5400) return `${(seconds / 60).toFixed(0)} min`;
  return `${(seconds / 3600).toFixed(1)} h`;
}

/* ============================================================ evolution */

export function EvolutionCurve({ generations, source = FALLBACK_SOURCE }: { generations: Generation[]; source?: string }) {
  const withBaseline = generations.some((g) => typeof g.controlBaseline === "number");
  const withFitness = generations.filter((g) => g.fitness.length > 0);
  const spec: FigureSpec = {
    title: "Fitness by generation against the controls that played the same games",
    kind: "line",
    x: { label: "generation" },
    y: { label: "mean score of 32 paired games", unit: "points" },
    series: [
      {
        name: "population mean",
        role: "primary",
        sourceRecord: source,
        sourceField: "evolve.generations[].mean",
        points: generations.map((g) => ({ x: g.generation, y: g.mean, sourceRecord: source, sourceField: "evolve.generations[].mean", label: `fitness block starts at seed ${g.blockStart}${typeof g.sigmaRel === "number" ? `; mutation sigma ${g.sigmaRel.toFixed(4)}` : ""}` })),
      },
      {
        name: "best candidate",
        role: "context",
        sourceRecord: source,
        sourceField: "evolve.generations[].best",
        points: generations.map((g) => ({ x: g.generation, y: g.best, sourceRecord: source, sourceField: "evolve.generations[].best" })),
      },
      {
        name: "top-4 mean",
        role: "context",
        sourceRecord: source,
        sourceField: "evolve.generations[].top4Mean",
        points: generations.map((g) => ({ x: g.generation, y: g.top4Mean, sourceRecord: source, sourceField: "evolve.generations[].top4Mean" })),
      },
      ...(withFitness.length >= 2
        ? [
            {
              name: "population range (lowest to highest candidate)",
              role: "band" as const,
              sourceRecord: source,
              sourceField: "evolve.generations[].fitness",
              points: withFitness.map((g) => ({ x: g.generation, y: g.mean, lo: Math.min(...g.fitness), hi: Math.max(...g.fitness), sourceRecord: source, sourceField: "evolve.generations[].fitness" })),
            },
          ]
        : []),
      {
        name: "fair leaf control (same seeds)",
        role: "control",
        sourceRecord: source,
        sourceField: "evolve.generations[].controlFair",
        points: generations.map((g) => ({ x: g.generation, y: g.controlFair, sourceRecord: source, sourceField: "evolve.generations[].controlFair" })),
      },
      {
        name: "unevolved init control (same seeds)",
        role: "reference",
        sourceRecord: source,
        sourceField: "evolve.generations[].controlInit",
        points: generations.map((g) => ({ x: g.generation, y: g.controlInit, sourceRecord: source, sourceField: "evolve.generations[].controlInit" })),
      },
      ...(withBaseline
        ? [
            {
              name: "first run's frozen candidate (same seeds)",
              role: "control" as const,
              sourceRecord: source,
              sourceField: "evolve.generations[].controlBaseline",
              points: generations.filter((g) => typeof g.controlBaseline === "number").map((g) => ({ x: g.generation, y: g.controlBaseline as number, sourceRecord: source, sourceField: "evolve.generations[].controlBaseline" })),
            },
          ]
        : []),
    ],
  };
  // mean blue; best blue thin; top-4 grey; band blue; fair orange dashed; init grey dashed; baseline violet dashed.
  const overrides = [undefined, { color: SERIES(0), thin: true }, undefined, ...(withFitness.length >= 2 ? [undefined] : []), undefined, undefined, ...(withBaseline ? [{ color: SERIES(6) }] : [])];
  return <LineChart spec={spec} overrides={overrides} />;
}

export function FitnessSwarm({ generations, source = FALLBACK_SOURCE }: { generations: Generation[]; source?: string }) {
  const spec: FigureSpec = {
    title: "Every candidate's fitness in every generation",
    kind: "strip",
    orientation: "vertical",
    x: { label: "generation" },
    y: { label: "candidate fitness (mean of its 32 paired games)", unit: "points" },
    series: [
      {
        name: "one candidate",
        role: "primary",
        sourceRecord: source,
        sourceField: "evolve.generations[].fitness",
        points: generations.flatMap((g) => g.fitness.map((f, i) => ({ x: g.generation, y: f, label: `candidate ${i} of ${g.fitness.length}`, sourceRecord: source, sourceField: `evolve.generations[${g.generation}].fitness[${i}]` }))),
      },
      {
        name: "fair leaf on the same 32 games",
        role: "control",
        sourceRecord: source,
        sourceField: "evolve.generations[].controlFair",
        points: generations.map((g) => ({ x: g.generation, y: g.controlFair, sourceRecord: source, sourceField: "evolve.generations[].controlFair" })),
      },
      {
        name: "unevolved init on the same 32 games",
        role: "reference",
        sourceRecord: source,
        sourceField: "evolve.generations[].controlInit",
        points: generations.map((g) => ({ x: g.generation, y: g.controlInit, sourceRecord: source, sourceField: "evolve.generations[].controlInit" })),
      },
    ],
  };
  return <Strip spec={spec} />;
}

/* =============================================================== corpus */

export function CorpusScores({ games, median, cap, source = FALLBACK_SOURCE }: { games: CorpusGame[]; median?: number; cap?: number; source?: string }) {
  const spec: FigureSpec = {
    title: "Every teacher game's final score, in completion order",
    kind: "strip",
    orientation: "vertical",
    x: { label: "teacher game, in completion order" },
    y: { label: "final score of the teacher game", unit: "points" },
    markers: median !== undefined ? [{ value: median, label: `recorded median ${formatValue(median)}`, axis: "y", sourceRecord: source, sourceField: "corpus.score.median" }] : undefined,
    series: [
      {
        name: "one complete teacher game",
        role: "primary",
        sourceRecord: source,
        sourceField: "corpus.perGame[]",
        points: games.map((g) => ({
          x: g.seed,
          y: g.score,
          censored: g.censored,
          label: `${formatValue(g.moves)} moves${g.censored && cap !== undefined ? ` (stopped at the ${formatValue(cap)}-move cap)` : ""}; depth-5 teacher wall time ${hours(g.wallSeconds)}`,
          sourceRecord: source,
          sourceField: "corpus.perGame[]",
        })),
      },
    ],
  };
  return <Strip spec={spec} />;
}

/* ============================================================= pretrain */

export function PretrainCurve({ epochs, bestEpoch, source = FALLBACK_SOURCE }: { epochs: PretrainEpoch[]; bestEpoch?: number; source?: string }) {
  const spec: FigureSpec = {
    title: "Supervised warm start: loss by epoch",
    kind: "line",
    x: { label: "epoch" },
    y: { label: "Huber loss (rise units, 17,000 points)" },
    markers: bestEpoch !== undefined && epochs.some((e) => e.epoch === bestEpoch) ? [{ value: bestEpoch, label: `epoch ${bestEpoch} frozen as the warm start`, axis: "x", sourceRecord: source, sourceField: "pretrain.report.bestEpoch" }] : undefined,
    series: [
      {
        name: "training loss",
        role: "primary",
        sourceRecord: source,
        sourceField: "pretrain.epochs[].trainHuber",
        points: epochs.map((e) => ({ x: e.epoch, y: e.trainHuber, sourceRecord: source, sourceField: "pretrain.epochs[].trainHuber" })),
      },
      {
        name: "validation loss (whole-origin held-out games)",
        role: "primary",
        sourceRecord: source,
        sourceField: "pretrain.epochs[].valHuber",
        points: epochs.map((e) => ({ x: e.epoch, y: e.valHuber, label: `validation Pearson ${e.valPearson.toFixed(4)}`, sourceRecord: source, sourceField: "pretrain.epochs[].valHuber" })),
      },
    ],
  };
  return <LineChart spec={spec} height={220} />;
}

/* ================================================================ screen */

export function ScreenPairs({ contrast, arms, candidateLabel, referenceLabel, source = FALLBACK_SOURCE }: { contrast: PairedContrast; arms: Record<string, ScreenArm>; candidateLabel: string; referenceLabel: string; source?: string }) {
  const candidateGames = new Map(arms[contrast.candidateArm]?.perGame.map((g) => [g.seedHex, g]) ?? []);
  const referenceGames = new Map(arms[contrast.referenceArm]?.perGame.map((g) => [g.seedHex, g]) ?? []);
  const rows = contrast.perSeed;
  const half = rows.length >= 2 ? Math.floor(rows.length / 2) + 0.5 : null;
  const spec: FigureSpec = {
    title: "Paired score difference on every held-out game",
    kind: "paired",
    x: { label: "held-out game, in seed order" },
    y: { label: "paired score difference (candidate minus reference)", unit: "points" },
    markers: [
      { value: contrast.meanDelta, lo: contrast.bootstrapLower95, hi: contrast.bootstrapUpper95, label: `recorded paired mean ${contrast.meanDelta > 0 ? "+" : ""}${formatValue(contrast.meanDelta)} (band: 95 % bootstrap)`, axis: "y", sourceRecord: source, sourceField: "screen.paired.meanDelta / bootstrapLower95 / bootstrapUpper95" },
      ...(half !== null ? [{ value: half, label: "first half | second half", axis: "x" as const, sourceRecord: source, sourceField: "screen.paired.halves" }] : []),
    ],
    series: [
      {
        name: `${candidateLabel} minus ${referenceLabel}`,
        role: "primary",
        sourceRecord: source,
        sourceField: "screen.paired.perSeed[].delta",
        points: rows.map((r) => {
          const c = candidateGames.get(r.seedHex);
          const f = referenceGames.get(r.seedHex);
          const parts = [c ? `${candidateLabel}: ${formatValue(c.score)} points, ${c.moves} moves` : null, f ? `${referenceLabel}: ${formatValue(f.score)} points, ${f.moves} moves` : null].filter(Boolean);
          return { x: r.seedHex, y: r.delta, label: parts.join(" · ") || undefined, sourceRecord: source, sourceField: "screen.paired.perSeed[].delta" };
        }),
      },
    ],
  };
  return <PairedDeltas spec={spec} />;
}
