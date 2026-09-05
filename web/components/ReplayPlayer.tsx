"use client";

import "../app/app.css";
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";
import { Drop7Board } from "@/components/Drop7Board";
import { DiscFace } from "@/components/discs";
import type {
  ReplayAnimationFrame,
  ReplayData,
  ReplayFrame,
} from "@/lib/leaderboard";
import {
  buildExplosionScoreBars,
  explosionPointsForFrame,
  type ScoreChartAggregation,
} from "@/lib/competition/score-chart";
import styles from "./Drop7Game.module.css";
import { useExplosionPoints } from "./useExplosionPoints";

const REPLAY_FRAME_DURATION_MS = {
  drop: 220,
  burst: 90,
  impact: 75,
  settle: 120,
  rise: 160,
} satisfies Record<ReplayAnimationFrame["kind"], number>;
const PRE_DROP_HOLD_MS = 45;
const BETWEEN_MOVES_MS = 70;

interface ReplayPresentation {
  board: string;
  kind: ReplayAnimationFrame["kind"] | null;
  indexes: readonly number[];
}

type ReplayPhase = "landing" | "animating" | "paused" | "resolved";

function landingBoard(frame: ReplayFrame | undefined) {
  return (
    frame?.placedBoard ??
    frame?.animation?.find(
      (presentationFrame) => presentationFrame.kind === "drop",
    )?.board ??
    frame?.board ??
    ""
  );
}

function boardBeforeMove(frames: ReplayData["frames"], index: number) {
  if (index > 0) return frames[index - 1].board;

  const frame = frames[0];
  const drop = frame?.animation?.find(
    (presentationFrame) => presentationFrame.kind === "drop",
  );
  const placed = drop?.board ?? frame?.placedBoard;
  if (!placed) return frame?.board ?? "";

  const droppedIndex = drop?.indexes[0] ?? topDiscIndex(placed, frame.column);
  if (droppedIndex === undefined) return placed;
  return replaceCell(placed, droppedIndex, "0");
}

function topDiscIndex(board: string, column: number) {
  for (let row = 0; row < 7; row += 1) {
    const index = row * 7 + column;
    if (board[index] !== "0") return index;
  }
  return undefined;
}

function replaceCell(board: string, index: number, cell: string) {
  return `${board.slice(0, index)}${cell}${board.slice(index + 1)}`;
}

function stillPresentation(board: string): ReplayPresentation {
  return { board, kind: null, indexes: [] };
}

function ScoreChart({
  frames,
  cursor,
  aggregation,
  cumulative,
}: {
  frames: ReplayData["frames"];
  cursor: number;
  aggregation: ScoreChartAggregation;
  cumulative: boolean;
}) {
  const width = 640;
  const height = 156;
  const plot = { left: 44, right: 10, top: 10, bottom: 25 };
  const bars = buildExplosionScoreBars(frames, aggregation, cumulative);
  const maxPoints = Math.max(1, ...bars.map((bar) => bar.points));
  const plotWidth = width - plot.left - plot.right;
  const plotHeight = height - plot.top - plot.bottom;
  const baseline = height - plot.bottom;
  const slotWidth = plotWidth / Math.max(1, bars.length);
  const barGap = Math.min(7, Math.max(1, slotWidth * 0.22));
  const barWidth = Math.max(1, slotWidth - barGap);
  const activeMove = frames[cursor]?.move ?? 1;
  const activeBarIndex = bars.findIndex(
    (bar) => activeMove >= bar.startMove && activeMove <= bar.endMove,
  );
  const roundSpans = bars.reduce<
    { round: number; start: number; end: number }[]
  >((spans, bar, index) => {
    const current = spans.at(-1);
    if (current?.round === bar.round) {
      current.end = index + 1;
    } else {
      spans.push({ round: bar.round, start: index, end: index + 1 });
    }
    return spans;
  }, []);
  const yAt = (points: number) =>
    baseline - (points / maxPoints) * plotHeight;
  const labelEvery = Math.max(1, Math.ceil(roundSpans.length / 8));
  const ariaMode =
    aggregation === "move"
      ? "by move"
      : cumulative
        ? "as a running total after each five-move round"
        : "aggregated by five-move round";

  return (
    <svg
      viewBox={`0 0 ${width} ${height}`}
      className="w-full rounded-md border border-rule bg-bg"
      role="img"
      aria-label={`Explosion points ${ariaMode}. Level-rise bonuses excluded.`}
    >
      <defs>
        <pattern
          id="replay-round-stripes"
          width="8"
          height="8"
          patternUnits="userSpaceOnUse"
          patternTransform="rotate(35)"
        >
          <line
            x1="0"
            y1="0"
            x2="0"
            y2="8"
            stroke="var(--color-accent)"
            strokeOpacity="0.09"
            strokeWidth="3"
          />
        </pattern>
      </defs>
      {roundSpans.map((span, index) => {
        const x = plot.left + span.start * slotWidth;
        const spanWidth = (span.end - span.start) * slotWidth;
        return (
          <g key={span.round}>
            <rect
              x={x}
              y={plot.top}
              width={spanWidth}
              height={plotHeight}
              fill={index % 2 === 0 ? "url(#replay-round-stripes)" : "var(--color-ink)"}
              fillOpacity={index % 2 === 0 ? 1 : 0.018}
            />
            {index > 0 && (
              <line
                x1={x}
                y1={plot.top}
                x2={x}
                y2={baseline}
                stroke="var(--color-rule-strong)"
                strokeWidth={1}
              />
            )}
            {(index % labelEvery === 0 ||
              span.start <= activeBarIndex && activeBarIndex < span.end) && (
              <text
                x={x + spanWidth / 2}
                y={height - 8}
                textAnchor="middle"
                fill="var(--color-ink-3)"
                fontSize={9}
              >
                R{span.round}
              </text>
            )}
          </g>
        );
      })}
      <line
        x1={plot.left}
        y1={baseline}
        x2={width - plot.right}
        y2={baseline}
        stroke="var(--color-rule-strong)"
      />
      <line
        x1={plot.left}
        y1={plot.top}
        x2={width - plot.right}
        y2={plot.top}
        stroke="var(--color-rule)"
        strokeDasharray="3 4"
      />
      <text
        x={plot.left - 7}
        y={plot.top + 3}
        textAnchor="end"
        fill="var(--color-ink-3)"
        fontSize={9}
      >
        {formatCompactInteger(maxPoints)}
      </text>
      <text
        x={plot.left - 7}
        y={baseline + 3}
        textAnchor="end"
        fill="var(--color-ink-4)"
        fontSize={9}
      >
        0
      </text>
      {bars.map((bar, index) => {
        const x = plot.left + index * slotWidth + barGap / 2;
        const y = yAt(bar.points);
        const barHeight = Math.max(bar.points === 0 ? 1 : 2, baseline - y);
        const active = index === activeBarIndex;
        return (
          <rect
            key={`${bar.startMove}-${bar.endMove}`}
            x={x}
            y={baseline - barHeight}
            width={barWidth}
            height={barHeight}
            rx={Math.min(2.5, barWidth / 3)}
            fill={active ? "var(--color-highlight)" : "var(--color-accent)"}
            fillOpacity={active ? 0.95 : 0.78}
          />
        );
      })}
    </svg>
  );
}

export function ReplayPlayer({
  game,
  showExplosionPoints = true,
}: {
  game: ReplayData;
  /** Show each exploding disc's point value rising from the board. Defaults to true. */
  showExplosionPoints?: boolean;
}) {
  const frames = game.frames;
  const [cursor, setCursor] = useState(0);
  const [phase, setPhase] = useState<ReplayPhase>("landing");
  const [animating, setAnimating] = useState(false);
  const [presentation, setPresentation] = useState<ReplayPresentation>(() =>
    stillPresentation(landingBoard(frames[0])),
  );
  const hasAnimations = useMemo(
    () => frames.some((frame) => (frame.animation?.length ?? 0) > 0),
    [frames],
  );
  const [animationsEnabled, setAnimationsEnabled] = useState(hasAnimations);
  const [scoreAggregation, setScoreAggregation] =
    useState<ScoreChartAggregation>("move");
  const [cumulativeScore, setCumulativeScore] = useState(false);
  const animationsEnabledRef = useRef(hasAnimations);
  const activeRunRef = useRef<AbortController | null>(null);
  const {
    explosionPoints,
    captureExplosionFrame,
    clearExplosionPoints,
  } = useExplosionPoints(showExplosionPoints);
  const frame = frames[cursor];
  const chartBars = useMemo(
    () =>
      buildExplosionScoreBars(
        frames,
        scoreAggregation,
        scoreAggregation === "round" && cumulativeScore,
      ),
    [cumulativeScore, frames, scoreAggregation],
  );
  const activeScoreBar = chartBars.find(
    (bar) => frame && frame.move >= bar.startMove && frame.move <= bar.endMove,
  );

  const cancelActiveRun = useCallback(() => {
    activeRunRef.current?.abort();
    activeRunRef.current = null;
    setAnimating(false);
    setPhase("paused");
    setPresentation((current) => stillPresentation(current.board));
    clearExplosionPoints();
  }, [clearExplosionPoints]);

  const beginRun = useCallback(() => {
    activeRunRef.current?.abort();
    const controller = new AbortController();
    activeRunRef.current = controller;
    clearExplosionPoints();
    setAnimating(true);
    return controller;
  }, [clearExplosionPoints]);

  const finishRun = useCallback((controller: AbortController) => {
    if (activeRunRef.current !== controller) return;
    activeRunRef.current = null;
    setAnimating(false);
  }, []);

  const animateMove = useCallback(
    async (index: number, signal: AbortSignal) => {
      const selectedFrame = frames[index];
      if (!selectedFrame || signal.aborted) return false;

      setCursor(index);
      setPhase("animating");
      const animation = animationsEnabledRef.current
        ? (selectedFrame.animation ?? [])
        : [];

      if (animation.length === 0) {
        setPresentation(stillPresentation(selectedFrame.board));
        setPhase("resolved");
        return true;
      }

      setPresentation(stillPresentation(boardBeforeMove(frames, index)));
      if (!(await waitFor(PRE_DROP_HOLD_MS, signal))) return false;

      for (const animationFrame of animation) {
        if (signal.aborted) return false;
        captureExplosionFrame(animationFrame);
        setPresentation({
          board: animationFrame.board,
          kind: animationFrame.kind,
          indexes: animationFrame.indexes,
        });
        if (
          !(await waitFor(
            REPLAY_FRAME_DURATION_MS[animationFrame.kind],
            signal,
          ))
        ) {
          return false;
        }
      }

      if (signal.aborted) return false;
      setPresentation(stillPresentation(selectedFrame.board));
      setPhase("resolved");
      return true;
    },
    [captureExplosionFrame, frames],
  );

  const step = useCallback(
    (delta: number) => {
      const target = Math.max(
        0,
        Math.min(frames.length - 1, cursor + delta),
      );
      if (target === cursor) return;

      const controller = beginRun();
      void animateMove(target, controller.signal).finally(() => {
        finishRun(controller);
      });
    },
    [animateMove, beginRun, cursor, finishRun, frames.length],
  );

  const togglePlay = useCallback(() => {
    if (animating) {
      cancelActiveRun();
      return;
    }

    const start =
      phase === "resolved"
        ? cursor >= frames.length - 1
          ? 0
          : cursor + 1
        : cursor;
    const controller = beginRun();
    void (async () => {
      for (let index = start; index < frames.length; index += 1) {
        if (!(await animateMove(index, controller.signal))) return;
        if (
          index < frames.length - 1 &&
          !(await waitFor(BETWEEN_MOVES_MS, controller.signal))
        ) {
          return;
        }
      }
    })().finally(() => {
      finishRun(controller);
    });
  }, [
    animateMove,
    animating,
    beginRun,
    cancelActiveRun,
    cursor,
    finishRun,
    frames.length,
    phase,
  ]);

  const toggleAnimations = useCallback(() => {
    const next = !animationsEnabledRef.current;
    animationsEnabledRef.current = next;
    setAnimationsEnabled(next);
    cancelActiveRun();
  }, [cancelActiveRun]);

  useEffect(
    () => () => {
      activeRunRef.current?.abort();
    },
    [],
  );

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (
        event.target instanceof HTMLInputElement ||
        event.target instanceof HTMLButtonElement ||
        event.target instanceof HTMLAnchorElement
      ) {
        return;
      }
      if (event.key === "ArrowLeft") step(-1);
      if (event.key === "ArrowRight") step(1);
      if (event.key === " ") {
        event.preventDefault();
        togglePlay();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [step, togglePlay]);

  const animatedIndexes = useMemo(
    () => new Set(presentation.indexes),
    [presentation.indexes],
  );
  const cellMotion = (index: number) =>
    animatedIndexes.has(index) && presentation.kind
      ? styles[presentation.kind]
      : undefined;
  const cellStyle = (index: number) =>
    ({
      "--drop7-rows": Math.floor(index / 7) + 1,
      ...(animatedIndexes.has(index) && presentation.kind
        ? {
            "--drop7-motion-duration": `${REPLAY_FRAME_DURATION_MS[presentation.kind]}ms`,
          }
        : {}),
    }) as CSSProperties;

  if (!frame || !presentation.board) return null;

  return (
    <div className="grid gap-5 lg:grid-cols-[minmax(0,1.35fr)_minmax(17rem,0.75fr)]">
      <section
        aria-busy={animating}
        className="rounded-lg border border-rule bg-surface p-4 sm:p-5"
      >
        <div className="flex items-center gap-2 border-b border-rule pb-4 text-ink">
          <span className="inline-flex size-8 text-sm">
            <DiscFace cell={frame.disc} />
          </span>
          <span className="text-small text-ink-2">placed in</span>
          <strong className="text-lg">column {frame.column + 1}</strong>
        </div>

        <div className="mt-5 flex justify-center">
          <Drop7Board
            cells={presentation.board}
            size="min(100%, 18rem)"
            label={`Move ${frame.move}: disc ${frame.disc} placed in column ${frame.column + 1}`}
            cellClassName={cellMotion}
            cellStyle={cellStyle}
            explosionPoints={explosionPoints}
            showExplosionPoints={showExplosionPoints}
          />
        </div>

        <div className="mx-auto mt-4 max-w-[28rem] space-y-3">
          <div className="grid grid-cols-[1fr_auto_1fr] items-center gap-2">
            <button
              type="button"
              onClick={() => step(-1)}
              disabled={cursor === 0}
              className="rounded-md border border-rule-strong px-3 py-2 text-small font-semibold text-ink-1 transition-colors motion-reduce:transition-none hover:border-ink-4 hover:text-ink disabled:cursor-not-allowed disabled:opacity-35"
            >
              ← Previous
            </button>
            <button
              type="button"
              onClick={togglePlay}
              className="min-w-24 rounded-md bg-accent-strong px-4 py-2 text-small font-semibold text-accent-fg transition-[filter] motion-reduce:transition-none hover:brightness-110"
            >
              {animating
                ? "Pause"
                : cursor === frames.length - 1 && phase === "resolved"
                  ? "Replay"
                  : "Play"}
            </button>
            <button
              type="button"
              onClick={() => step(1)}
              disabled={cursor === frames.length - 1}
              className="rounded-md border border-rule-strong px-3 py-2 text-small font-semibold text-ink-1 transition-colors motion-reduce:transition-none hover:border-ink-4 hover:text-ink disabled:cursor-not-allowed disabled:opacity-35"
            >
              Next →
            </button>
          </div>
          <div className="flex flex-wrap items-center justify-between gap-2 text-caption text-ink-3">
            <span>Arrow keys step · space plays</span>
            {hasAnimations && (
              <button
                type="button"
                role="switch"
                aria-checked={animationsEnabled}
                onClick={toggleAnimations}
                className="app-switch"
              >
                <span aria-hidden="true" className="app-switch-track">
                  <span className="app-switch-knob" />
                </span>
                Animations {animationsEnabled ? "on" : "off"}
              </button>
            )}
          </div>
        </div>
      </section>

      <aside className="space-y-4">
        <section className="rounded-lg border border-rule bg-surface p-4">
          <div className="mb-3 flex flex-wrap items-end justify-between gap-3">
            <div>
              <p className="label">Explosion points</p>
              <p className="mt-1 font-mono text-h3 font-semibold tabular-nums text-ink">
                {cumulativeScore && scoreAggregation === "round" ? "" : "+"}
                {formatInteger(
                  activeScoreBar?.points ?? explosionPointsForFrame(frame),
                )}
              </p>
              <p className="text-caption text-ink-3">
                {scoreAggregation === "move"
                  ? `move ${frame.move}`
                  : `round ${activeScoreBar?.round ?? 1}${
                      cumulativeScore ? " running total" : " total"
                    }`}
              </p>
            </div>
            <div
              className="inline-flex rounded-md border border-rule-strong bg-raised p-0.5 text-caption font-semibold"
              role="group"
              aria-label="Explosion score aggregation"
            >
              <button
                type="button"
                aria-pressed={scoreAggregation === "move"}
                onClick={() => setScoreAggregation("move")}
                className={`rounded-sm px-2.5 py-1.5 transition-colors motion-reduce:transition-none ${
                  scoreAggregation === "move"
                    ? "bg-hover text-ink"
                    : "text-ink-3 hover:text-ink-1"
                }`}
              >
                Each move
              </button>
              <button
                type="button"
                aria-pressed={scoreAggregation === "round"}
                onClick={() => setScoreAggregation("round")}
                className={`rounded-sm px-2.5 py-1.5 transition-colors motion-reduce:transition-none ${
                  scoreAggregation === "round"
                    ? "bg-hover text-ink"
                    : "text-ink-3 hover:text-ink-1"
                }`}
              >
                By round
              </button>
            </div>
          </div>
          <ScoreChart
            frames={frames}
            cursor={cursor}
            aggregation={scoreAggregation}
            cumulative={scoreAggregation === "round" && cumulativeScore}
          />
          <div className="mt-2.5 flex flex-wrap items-center justify-between gap-2 text-caption text-ink-3">
            <span>Level-rise bonuses excluded · stripes mark 5 moves</span>
            {scoreAggregation === "round" && (
              <button
                type="button"
                role="switch"
                aria-checked={cumulativeScore}
                onClick={() => setCumulativeScore((current) => !current)}
                className="app-switch"
              >
                <span aria-hidden="true" className="app-switch-track">
                  <span className="app-switch-knob" />
                </span>
                Running total {cumulativeScore ? "on" : "off"}
              </button>
            )}
          </div>
        </section>

        <section className="rounded-lg border border-rule bg-surface p-4">
          <p className="label mb-3">Move metrics</p>
          <div className="grid grid-cols-2 gap-2.5">
            <Metric label="Score" value={formatInteger(frame.score)} />
            <Metric label="This move" value={`+${formatInteger(frame.scoreDelta)}`} tone="emerald" />
            <Metric label="Chain" value={`×${frame.chainDepth}`} />
            <Metric label="Rise in" value={`${frame.movesRemaining}/5 drops`} />
            <Metric label="Cleared" value={`${frame.cleared} discs`} />
            <Metric label="Revealed" value={`${frame.revealed} gray`} />
          </div>
          {frame.levelAdvanced && (
            <p className="app-level-rise">
              Level rise · +17,000 points
            </p>
          )}
        </section>
      </aside>
    </div>
  );
}

function Metric({
  label,
  value,
  tone = "default",
}: {
  label: string;
  value: string;
  tone?: "default" | "emerald";
}) {
  return (
    <div className="rounded-md border border-rule bg-raised px-3 py-2.5">
      <p className="label">{label}</p>
      <p
        className={`mt-0.5 font-mono text-small font-semibold tabular-nums ${
          tone === "emerald" ? "text-status-completed" : "text-ink"
        }`}
      >
        {value}
      </p>
    </div>
  );
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function formatCompactInteger(value: number) {
  return Intl.NumberFormat("en-US", {
    notation: "compact",
    maximumFractionDigits: 1,
  }).format(Math.round(value));
}

function waitFor(milliseconds: number, signal: AbortSignal) {
  if (signal.aborted) return Promise.resolve(false);
  return new Promise<boolean>((resolve) => {
    const timer = window.setTimeout(() => {
      signal.removeEventListener("abort", cancel);
      resolve(true);
    }, milliseconds);
    const cancel = () => {
      window.clearTimeout(timer);
      signal.removeEventListener("abort", cancel);
      resolve(false);
    };
    signal.addEventListener("abort", cancel, { once: true });
  });
}
