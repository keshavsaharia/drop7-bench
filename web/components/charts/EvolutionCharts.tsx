"use client";
/**
 * Charts for the nnue-evolution approach page, drawn from the committed run
 * snapshot (web/lib/charts/evolution.ts). Each chart receives rows copied
 * from run artifacts and draws them as written; reference lines (a recorded
 * median, a recorded mean delta) are values the snapshot carries, never
 * computed here.
 */
import { useCallback, useMemo, useState, type MouseEvent } from "react";
import { scaleLinear } from "@visx/scale";
import { LinePath } from "@visx/shape";
import { curveLinear } from "@visx/curve";
import { localPoint } from "@visx/event";
import { formatSigned, formatValue } from "@/lib/charts/spec";
import type { CorpusGame, Generation, PairedContrast, PretrainEpoch, ScreenArm } from "@/lib/charts/evolution";
import { useContainerWidth, useMeasurer, useMounted, type Measurer } from "./layout";
import { BottomAxis, FocusablePoint, LeftAxis, Marker, ZeroLine, rounded, tickValues, usePointHover, type PlacedPoint } from "./primitives";
import { ChartTooltip, type TooltipLine, type TooltipState } from "./Tooltip";
import { DEFAULT_WIDTH, INK, LABEL_SIZE, MUTED, PALETTE, TICK_SIZE } from "./theme";

const TOP = 14;
const RIGHT = 20;

const BLUE = PALETTE[0];
const AMBER = PALETTE[1];
const GREEN = PALETTE[2];
const PINK = PALETTE[3];
const RED = PALETTE[4];
const CYAN = PALETTE[6];

interface Frame {
  width: number;
  height: number;
  plotLeft: number;
  plotRight: number;
  plotTop: number;
  plotBottom: number;
  xScale: ReturnType<typeof scaleLinear<number>>;
  yScale: ReturnType<typeof scaleLinear<number>>;
  xTicks: number[];
  yTicks: number[];
}

/** A vertical plot frame with measured gutters. */
function makeFrame(
  width: number,
  measure: Measurer,
  xDomain: [number, number],
  yDomain: [number, number],
  options: { yLabel?: string; xLabel?: string; integerX?: boolean; xPad?: number; height?: number },
): Frame {
  const innerHeight = options.height ?? Math.max(180, Math.min(300, Math.round(width * 0.42)));
  const yScale = rounded(scaleLinear<number>({ domain: yDomain, nice: 5 }));
  const yTicks = tickValues(yScale, 5);
  const tickWidth = Math.max(0, ...yTicks.map((t) => measure(compact(t), TICK_SIZE)));
  const plotLeft = tickWidth + 14 + (options.yLabel ? 18 : 0);
  const bottomGutter = TICK_SIZE + 12 + (options.xLabel ? LABEL_SIZE + 8 : 0);
  const plotTop = TOP;
  const plotBottom = plotTop + innerHeight;
  const height = plotBottom + bottomGutter;
  const plotRight = width - RIGHT;
  yScale.range([plotBottom, plotTop]);
  const pad = options.xPad ?? 0;
  const xScale = rounded(scaleLinear<number>({ domain: [xDomain[0] - pad, xDomain[1] + pad], range: [plotLeft + 6, plotRight - 6] }));
  const xTicks = tickValues(xScale, Math.max(2, Math.floor((plotRight - plotLeft) / 80)), options.integerX);
  return { width, height, plotLeft, plotRight, plotTop, plotBottom, xScale, yScale, xTicks, yTicks };
}

function compact(value: number): string {
  const abs = Math.abs(value);
  if (abs >= 1e6) return `${(value / 1e6).toFixed(1)}M`;
  if (abs >= 1e3) return `${(value / 1e3).toFixed(0)}k`;
  return value.toFixed(abs >= 1 ? 2 : 3);
}

function hours(seconds: number): string {
  if (seconds < 90) return `${seconds.toFixed(0)} s`;
  if (seconds < 5400) return `${(seconds / 60).toFixed(0)} min`;
  return `${(seconds / 3600).toFixed(1)} h`;
}

function Axes({ frame, yLabel, xLabel }: { frame: Frame; yLabel?: string; xLabel?: string }) {
  return (
    <>
      <LeftAxis scale={frame.yScale} ticks={frame.yTicks} x={frame.plotLeft} width={frame.plotRight - frame.plotLeft} label={yLabel} plotTop={frame.plotTop} plotBottom={frame.plotBottom} />
      <BottomAxis scale={frame.xScale} ticks={frame.xTicks} y={frame.plotBottom} plotTop={frame.plotTop} plotLeft={frame.plotLeft} plotRight={frame.plotRight} label={xLabel} labelY={frame.height - 4} grid={false} />
    </>
  );
}

function Legend({ items }: { items: { name: string; color: string; dashed?: boolean; band?: boolean }[] }) {
  return (
    <ul className="rchart-legend">
      {items.map((item) => (
        <li key={item.name}>
          <span
            className={"rchart-swatch" + (item.dashed ? " is-dashed" : "")}
            style={item.dashed ? { borderColor: item.color } : { background: item.color, opacity: item.band ? 0.35 : 1 }}
          />
          {item.name}
        </li>
      ))}
    </ul>
  );
}

/** Hover model keyed on the nearest x value (one tooltip for every series at that x). */
function useNearestX(xs: number[], xScale: (v: number) => number, lines: (index: number) => TooltipLine[]) {
  const [index, setIndex] = useState<number | null>(null);
  const [tooltip, setTooltip] = useState<TooltipState | null>(null);
  const onMouseMove = useCallback(
    (event: MouseEvent<SVGSVGElement>) => {
      const local = localPoint(event.currentTarget, event);
      if (!local || xs.length === 0) return;
      let best = 0;
      let bestDistance = Infinity;
      xs.forEach((x, i) => {
        const distance = Math.abs(xScale(x) - local.x);
        if (distance < bestDistance) {
          best = i;
          bestDistance = distance;
        }
      });
      if (bestDistance > 40) {
        setIndex(null);
        setTooltip(null);
        return;
      }
      setIndex(best);
      setTooltip({ x: event.clientX, y: event.clientY, lines: lines(best) });
    },
    [xs, xScale, lines],
  );
  const onMouseLeave = useCallback(() => {
    setIndex(null);
    setTooltip(null);
  }, []);
  return { index, tooltip, onMouseMove, onMouseLeave };
}

/* ============================================================ evolution */

export function EvolutionCurve({ generations }: { generations: Generation[] }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  const frame = useMemo(() => {
    const ys = generations.flatMap((g) => [g.best, g.mean, g.top4Mean, g.controlFair, g.controlInit, ...g.fitness]);
    const xs = generations.map((g) => g.generation);
    return makeFrame(width, measure, [Math.min(...xs), Math.max(...xs, Math.min(...xs) + 1)], [Math.min(...ys), Math.max(...ys)], {
      yLabel: "mean score of 32 paired games (points)",
      xLabel: "generation",
      integerX: true,
      xPad: 0.5,
    });
  }, [generations, width, measure]);
  const xs = useMemo(() => generations.map((g) => g.generation), [generations]);
  const lines = useCallback(
    (i: number): TooltipLine[] => {
      const g = generations[i];
      const sorted = [...g.fitness].sort((a, b) => b - a);
      return [
        { text: `generation ${g.generation}`, strong: true },
        { text: `fitness block starts at seed ${g.blockStart}`, muted: true },
        { text: `best candidate: ${formatValue(g.best)}`, swatch: BLUE },
        { text: `top-4 mean: ${formatValue(g.top4Mean)}`, swatch: CYAN },
        { text: `population mean: ${formatValue(g.mean)}`, swatch: GREEN },
        { text: `fair leaf control: ${formatValue(g.controlFair)}`, swatch: AMBER },
        { text: `unevolved init control: ${formatValue(g.controlInit)}`, swatch: PINK },
        { text: `population mean minus fair: ${formatSigned(g.mean - g.controlFair)}`, muted: true },
        ...(sorted.length ? [{ text: `${sorted.length} candidates, lowest ${formatValue(sorted[sorted.length - 1])}`, muted: true }] : []),
      ];
    },
    [generations],
  );
  const hover = useNearestX(xs, frame.xScale, lines);

  const bandPath = useMemo(() => {
    const withFitness = generations.filter((g) => g.fitness.length > 0);
    if (withFitness.length < 2) return null;
    const top = withFitness.map((g) => `${frame.xScale(g.generation)},${frame.yScale(Math.max(...g.fitness))}`);
    const bottom = [...withFitness].reverse().map((g) => `${frame.xScale(g.generation)},${frame.yScale(Math.min(...g.fitness))}`);
    return `M${top.join(" L")} L${bottom.join(" L")} Z`;
  }, [generations, frame]);

  const series: { key: keyof Generation; color: string; dashed?: boolean }[] = [
    { key: "controlFair", color: AMBER, dashed: true },
    { key: "controlInit", color: PINK, dashed: true },
    { key: "mean", color: GREEN },
    { key: "top4Mean", color: CYAN },
    { key: "best", color: BLUE },
  ];

  return (
    <div className="rchart" ref={ref}>
      <svg width={frame.width} height={frame.height} viewBox={`0 0 ${frame.width} ${frame.height}`} className="rchart-svg" role="img" aria-label="Fitness by generation against the paired controls" onMouseMove={hover.onMouseMove} onMouseLeave={hover.onMouseLeave}>
        <Axes frame={frame} yLabel="mean score of 32 paired games (points)" xLabel="generation" />
        {bandPath && <path d={bandPath} fill={BLUE} opacity={0.12} />}
        {series.map((s) => (
          <LinePath<Generation>
            key={s.key}
            data={generations}
            x={(g) => frame.xScale(g.generation)}
            y={(g) => frame.yScale(g[s.key] as number)}
            stroke={s.color}
            strokeWidth={s.key === "best" || s.key === "mean" ? 2.2 : 1.6}
            strokeDasharray={s.dashed ? "6 4" : undefined}
            curve={curveLinear}
          />
        ))}
        {generations.length === 1 &&
          series.map((s) => <circle key={s.key} cx={frame.xScale(generations[0].generation)} cy={frame.yScale(generations[0][s.key] as number)} r={4} fill={s.color} />)}
        {hover.index !== null && (
          <g>
            <line x1={frame.xScale(xs[hover.index])} x2={frame.xScale(xs[hover.index])} y1={frame.plotTop} y2={frame.plotBottom} stroke={INK} strokeOpacity={0.35} strokeDasharray="3 3" />
            {series.map((s) => (
              <circle key={s.key} cx={frame.xScale(xs[hover.index!])} cy={frame.yScale(generations[hover.index!][s.key] as number)} r={4.5} fill={s.color} stroke={INK} strokeWidth={1.5} />
            ))}
          </g>
        )}
      </svg>
      <Legend
        items={[
          { name: "best candidate", color: BLUE },
          { name: "top-4 mean", color: CYAN },
          { name: "population mean", color: GREEN },
          { name: "population range (lowest to highest candidate)", color: BLUE, band: true },
          { name: "fair leaf control (same seeds)", color: AMBER, dashed: true },
          { name: "unevolved init control (same seeds)", color: PINK, dashed: true },
        ]}
      />
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}

export function FitnessSwarm({ generations }: { generations: Generation[] }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  const frame = useMemo(() => {
    const ys = generations.flatMap((g) => [g.controlFair, g.controlInit, ...g.fitness]);
    const xs = generations.map((g) => g.generation);
    return makeFrame(width, measure, [Math.min(...xs), Math.max(...xs, Math.min(...xs) + 1)], [Math.min(...ys), Math.max(...ys)], {
      yLabel: "candidate fitness (points)",
      xLabel: "generation",
      integerX: true,
      xPad: 0.5,
    });
  }, [generations, width, measure]);
  const points = useMemo<PlacedPoint[]>(() => {
    const slot = generations.length > 1 ? Math.abs(frame.xScale(generations[1].generation) - frame.xScale(generations[0].generation)) : 40;
    const spread = Math.min(10, slot * 0.35);
    const out: PlacedPoint[] = [];
    for (const g of generations) {
      const n = g.fitness.length || 1;
      g.fitness.forEach((f, i) => {
        // Deterministic horizontal offset by candidate index so dots at similar fitness do not stack exactly.
        const offset = ((i % 7) - 3) * (spread / 3.5);
        out.push({
          key: `${g.generation}-${i}`,
          cx: frame.xScale(g.generation) + offset,
          cy: frame.yScale(f),
          color: BLUE,
          lines: [
            { text: `generation ${g.generation}, candidate ${i}`, strong: true },
            { text: `fitness (mean of 32 paired games): ${formatValue(f)}`, swatch: BLUE },
            { text: `fair leaf on the same games: ${formatValue(g.controlFair)}`, swatch: AMBER },
            { text: `minus fair: ${formatSigned(f - g.controlFair)}`, muted: true },
            { text: `${n} candidates in this generation`, muted: true },
          ],
        });
      });
    }
    return out;
  }, [generations, frame]);
  const hover = usePointHover(points);

  return (
    <div className="rchart" ref={ref}>
      <svg width={frame.width} height={frame.height} viewBox={`0 0 ${frame.width} ${frame.height}`} className="rchart-svg" role="img" aria-label="Every candidate's fitness in every generation" onMouseMove={hover.onMouseMove} onMouseLeave={hover.onMouseLeave}>
        <Axes frame={frame} yLabel="candidate fitness (points)" xLabel="generation" />
        {generations.map((g) => {
          const x = frame.xScale(g.generation);
          return (
            <g key={g.generation}>
              <line x1={x - 12} x2={x + 12} y1={frame.yScale(g.controlFair)} y2={frame.yScale(g.controlFair)} stroke={AMBER} strokeWidth={2} />
              <line x1={x - 12} x2={x + 12} y1={frame.yScale(g.controlInit)} y2={frame.yScale(g.controlInit)} stroke={PINK} strokeWidth={2} strokeDasharray="3 2" />
            </g>
          );
        })}
        {points.map((p) => (
          <circle key={p.key} cx={p.cx} cy={p.cy} r={hover.active === p.key ? 4.5 : 2.6} fill={BLUE} fillOpacity={hover.active === p.key ? 1 : 0.55} stroke={hover.active === p.key ? INK : "none"} />
        ))}
      </svg>
      <Legend
        items={[
          { name: "one candidate (mean of its 32 paired games)", color: BLUE },
          { name: "fair leaf on the same 32 games", color: AMBER },
          { name: "unevolved init on the same 32 games", color: PINK, dashed: true },
        ]}
      />
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}

/* =============================================================== corpus */

export function CorpusScores({ games, median, cap }: { games: CorpusGame[]; median?: number; cap?: number }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  const frame = useMemo(() => {
    const ys = games.map((g) => g.score);
    return makeFrame(width, measure, [1, Math.max(games.length, 2)], [0, Math.max(...ys, 1)], {
      yLabel: "final score of the teacher game (points)",
      xLabel: "teacher game, in completion order",
      integerX: true,
      xPad: 0.5,
    });
  }, [games, width, measure]);
  const points = useMemo<PlacedPoint[]>(
    () =>
      games.map((g, i) => ({
        key: g.seed,
        cx: frame.xScale(i + 1),
        cy: frame.yScale(g.score),
        color: g.censored ? AMBER : BLUE,
        hollow: g.censored,
        lines: [
          { text: `game ${i + 1}, seed ${g.seed}`, strong: true },
          { text: `score: ${formatValue(g.score)} points`, swatch: g.censored ? AMBER : BLUE },
          { text: `${formatValue(g.moves)} moves${g.censored ? ` (stopped at the ${cap ?? ""} move cap)` : ""}` },
          { text: `depth-5 teacher wall time: ${hours(g.wallSeconds)}`, muted: true },
        ],
      })),
    [games, frame, cap],
  );
  const hover = usePointHover(points);

  return (
    <div className="rchart" ref={ref}>
      <svg width={frame.width} height={frame.height} viewBox={`0 0 ${frame.width} ${frame.height}`} className="rchart-svg" role="img" aria-label="Every teacher game's final score" onMouseMove={hover.onMouseMove} onMouseLeave={hover.onMouseLeave}>
        <Axes frame={frame} yLabel="final score of the teacher game (points)" xLabel="teacher game, in completion order" />
        {median !== undefined && (
          <g>
            <line x1={frame.plotLeft} x2={frame.plotRight} y1={frame.yScale(median)} y2={frame.yScale(median)} stroke={GREEN} strokeDasharray="5 4" />
            <text x={frame.plotLeft + 6} y={frame.yScale(median) - 5} textAnchor="start" fontSize={10} fill={GREEN}>
              recorded median {formatValue(median)}
            </text>
          </g>
        )}
        {points.map((p) => (
          <FocusablePoint key={p.key} point={p} onFocus={hover.onFocusPoint} onBlur={hover.onBlurPoint}>
            <Marker point={p} r={3.5} active={hover.active === p.key} />
          </FocusablePoint>
        ))}
      </svg>
      <Legend
        items={[
          { name: "one complete teacher game", color: BLUE },
          { name: "game stopped at the move cap (censored)", color: AMBER, dashed: true },
          { name: "recorded median", color: GREEN, dashed: true },
        ]}
      />
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}

/* ============================================================= pretrain */

export function PretrainCurve({ epochs, bestEpoch }: { epochs: PretrainEpoch[]; bestEpoch?: number }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  const frame = useMemo(() => {
    const ys = epochs.flatMap((e) => [e.trainHuber, e.valHuber]);
    const xs = epochs.map((e) => e.epoch);
    return makeFrame(width, measure, [Math.min(...xs), Math.max(...xs, Math.min(...xs) + 1)], [0, Math.max(...ys, 0.01)], {
      yLabel: "Huber loss (rise units, 17,000 points)",
      xLabel: "epoch",
      integerX: true,
      xPad: 0.5,
      height: 220,
    });
  }, [epochs, width, measure]);
  const xs = useMemo(() => epochs.map((e) => e.epoch), [epochs]);
  const lines = useCallback(
    (i: number): TooltipLine[] => {
      const e = epochs[i];
      return [
        { text: `epoch ${e.epoch}${e.epoch === bestEpoch ? " (best by validation loss)" : ""}`, strong: true },
        { text: `training Huber: ${e.trainHuber.toFixed(4)}`, swatch: BLUE },
        { text: `validation Huber: ${e.valHuber.toFixed(4)}`, swatch: AMBER },
        { text: `validation Pearson: ${e.valPearson.toFixed(4)}`, muted: true },
      ];
    },
    [epochs, bestEpoch],
  );
  const hover = useNearestX(xs, frame.xScale, lines);

  return (
    <div className="rchart" ref={ref}>
      <svg width={frame.width} height={frame.height} viewBox={`0 0 ${frame.width} ${frame.height}`} className="rchart-svg" role="img" aria-label="Supervised warm-start loss by epoch" onMouseMove={hover.onMouseMove} onMouseLeave={hover.onMouseLeave}>
        <Axes frame={frame} yLabel="Huber loss (rise units)" xLabel="epoch" />
        {bestEpoch !== undefined && epochs.some((e) => e.epoch === bestEpoch) && (
          <line x1={frame.xScale(bestEpoch)} x2={frame.xScale(bestEpoch)} y1={frame.plotTop} y2={frame.plotBottom} stroke={GREEN} strokeDasharray="4 4" />
        )}
        <LinePath<PretrainEpoch> data={epochs} x={(e) => frame.xScale(e.epoch)} y={(e) => frame.yScale(e.trainHuber)} stroke={BLUE} strokeWidth={2} curve={curveLinear} />
        <LinePath<PretrainEpoch> data={epochs} x={(e) => frame.xScale(e.epoch)} y={(e) => frame.yScale(e.valHuber)} stroke={AMBER} strokeWidth={2} curve={curveLinear} />
        {epochs.map((e) => (
          <g key={e.epoch}>
            <circle cx={frame.xScale(e.epoch)} cy={frame.yScale(e.trainHuber)} r={hover.index !== null && xs[hover.index] === e.epoch ? 4.5 : 3} fill={BLUE} />
            <circle cx={frame.xScale(e.epoch)} cy={frame.yScale(e.valHuber)} r={hover.index !== null && xs[hover.index] === e.epoch ? 4.5 : 3} fill={AMBER} />
          </g>
        ))}
      </svg>
      <Legend
        items={[
          { name: "training loss", color: BLUE },
          { name: "validation loss (whole-origin held-out games)", color: AMBER },
          { name: "epoch frozen as the warm start", color: GREEN, dashed: true },
        ]}
      />
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}

/* ================================================================ screen */

export function ScreenPairs({ contrast, arms, candidateLabel, referenceLabel }: { contrast: PairedContrast; arms: Record<string, ScreenArm>; candidateLabel: string; referenceLabel: string }) {
  const mounted = useMounted();
  const measure = useMeasurer(mounted);
  const [ref, width] = useContainerWidth<HTMLDivElement>(DEFAULT_WIDTH);
  const rows = contrast.perSeed;
  const frame = useMemo(() => {
    const ys = rows.map((r) => r.delta);
    return makeFrame(width, measure, [1, Math.max(rows.length, 2)], [Math.min(0, ...ys), Math.max(0, ...ys)], {
      yLabel: "paired score difference (points)",
      xLabel: "held-out game, in seed order",
      integerX: true,
      xPad: 0.5,
    });
  }, [rows, width, measure]);
  const candidateGames = useMemo(() => new Map(arms[contrast.candidateArm]?.perGame.map((g) => [g.seedHex, g]) ?? []), [arms, contrast.candidateArm]);
  const referenceGames = useMemo(() => new Map(arms[contrast.referenceArm]?.perGame.map((g) => [g.seedHex, g]) ?? []), [arms, contrast.referenceArm]);
  const zeroY = frame.yScale(0);
  const barWidth = Math.max(2, Math.min(10, ((frame.plotRight - frame.plotLeft) / Math.max(rows.length, 1)) * 0.7));
  const points = useMemo<PlacedPoint[]>(
    () =>
      rows.map((r, i) => {
        const cx = frame.xScale(i + 1);
        const cy = frame.yScale(r.delta);
        const c = candidateGames.get(r.seedHex);
        const f = referenceGames.get(r.seedHex);
        return {
          key: r.seedHex,
          cx,
          cy,
          color: r.delta >= 0 ? GREEN : RED,
          bar: { x: cx - barWidth / 2, y: Math.min(cy, zeroY), w: barWidth, h: Math.abs(zeroY - cy) },
          lines: [
            { text: `game ${i + 1}, seed ${r.seedHex}`, strong: true },
            { text: `${candidateLabel}: ${c ? formatValue(c.score) : "n/a"}${c ? ` (${c.moves} moves)` : ""}`, swatch: BLUE },
            { text: `${referenceLabel}: ${f ? formatValue(f.score) : "n/a"}${f ? ` (${f.moves} moves)` : ""}`, swatch: AMBER },
            { text: `difference: ${formatSigned(r.delta)} points`, swatch: r.delta >= 0 ? GREEN : RED },
          ],
        };
      }),
    [rows, frame, candidateGames, referenceGames, barWidth, zeroY, candidateLabel, referenceLabel],
  );
  const hover = usePointHover(points);
  const half = rows.length >= 2 ? Math.floor(rows.length / 2) + 0.5 : null;

  return (
    <div className="rchart" ref={ref}>
      <svg width={frame.width} height={frame.height} viewBox={`0 0 ${frame.width} ${frame.height}`} className="rchart-svg" role="img" aria-label="Paired score difference on every held-out game" onMouseMove={hover.onMouseMove} onMouseLeave={hover.onMouseLeave}>
        <Axes frame={frame} yLabel="paired score difference (points)" xLabel="held-out game, in seed order" />
        <ZeroLine x1={frame.plotLeft} x2={frame.plotRight} y1={zeroY} y2={zeroY} />
        {half !== null && <line x1={frame.xScale(half)} x2={frame.xScale(half)} y1={frame.plotTop} y2={frame.plotBottom} stroke={MUTED} strokeDasharray="2 4" />}
        <line x1={frame.plotLeft} x2={frame.plotRight} y1={frame.yScale(contrast.meanDelta)} y2={frame.yScale(contrast.meanDelta)} stroke={CYAN} strokeDasharray="5 4" />
        <text x={frame.plotRight} y={frame.yScale(contrast.meanDelta) - 5} textAnchor="end" fontSize={10} fill={CYAN}>
          recorded mean {formatSigned(contrast.meanDelta)}
        </text>
        {points.map((p) => (
          <rect key={p.key} x={p.bar!.x} y={p.bar!.y} width={p.bar!.w} height={p.bar!.h} fill={p.color} opacity={hover.active === p.key ? 1 : 0.7} />
        ))}
        {points.map((p) => (
          <FocusablePoint key={`f-${p.key}`} point={p} onFocus={hover.onFocusPoint} onBlur={hover.onBlurPoint}>
            <rect x={p.bar!.x - 2} y={Math.min(p.bar!.y, zeroY - 4)} width={p.bar!.w + 4} height={Math.max(p.bar!.h, 8)} fill="transparent" />
          </FocusablePoint>
        ))}
      </svg>
      <Legend
        items={[
          { name: `${candidateLabel} scored higher`, color: GREEN },
          { name: `${referenceLabel} scored higher`, color: RED },
          { name: "recorded paired mean difference", color: CYAN, dashed: true },
          { name: "first half / second half of the block", color: MUTED, dashed: true },
        ]}
      />
      <ChartTooltip state={hover.tooltip} />
    </div>
  );
}
