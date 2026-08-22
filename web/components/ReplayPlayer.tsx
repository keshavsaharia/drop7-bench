"use client";

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
import styles from "./Drop7Game.module.css";

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
}: {
  frames: ReplayData["frames"];
  cursor: number;
}) {
  const width = 640;
  const height = 132;
  const topPadding = 9;
  const bottomPadding = 10;
  const maxScore = Math.max(1, ...frames.map((frame) => frame.score));
  const xAt = (index: number) =>
    (index / Math.max(1, frames.length - 1)) * width;
  const yAt = (score: number) =>
    height -
    bottomPadding -
    (score / maxScore) * (height - topPadding - bottomPadding);
  const points = frames.map(
    (frame, index) => `${xAt(index)},${yAt(frame.score)}`,
  );
  const cursorX = xAt(cursor);
  const cursorY = yAt(frames[cursor]?.score ?? 0);

  return (
    <svg
      viewBox={`0 0 ${width} ${height}`}
      className="w-full rounded-lg border border-zinc-800 bg-zinc-950"
      role="img"
      aria-label="Score trajectory over the game"
    >
      <defs>
        <linearGradient id="score-trajectory-fill" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#38bdf8" stopOpacity="0.28" />
          <stop offset="100%" stopColor="#38bdf8" stopOpacity="0" />
        </linearGradient>
      </defs>
      <polygon
        points={`0,${height} ${points.join(" ")} ${width},${height}`}
        fill="url(#score-trajectory-fill)"
      />
      {frames.map(
        (frame, index) =>
          frame.levelAdvanced && (
            <line
              key={index}
              x1={xAt(index)}
              y1={0}
              x2={xAt(index)}
              y2={height}
              stroke="#3f3f46"
              strokeDasharray="3 3"
            />
          ),
      )}
      <polyline
        points={points.join(" ")}
        fill="none"
        stroke="#38bdf8"
        strokeWidth={2.5}
        strokeLinejoin="round"
      />
      <line
        x1={cursorX}
        y1={0}
        x2={cursorX}
        y2={height}
        stroke="#facc15"
        strokeWidth={1.5}
      />
      <circle
        cx={cursorX}
        cy={cursorY}
        r={4.5}
        fill="#facc15"
        stroke="#18181b"
        strokeWidth={2}
      />
    </svg>
  );
}

export function ReplayPlayer({ game }: { game: ReplayData }) {
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
  const animationsEnabledRef = useRef(hasAnimations);
  const activeRunRef = useRef<AbortController | null>(null);
  const frame = frames[cursor];

  const cancelActiveRun = useCallback(() => {
    activeRunRef.current?.abort();
    activeRunRef.current = null;
    setAnimating(false);
    setPhase("paused");
    setPresentation((current) => stillPresentation(current.board));
  }, []);

  const beginRun = useCallback(() => {
    activeRunRef.current?.abort();
    const controller = new AbortController();
    activeRunRef.current = controller;
    setAnimating(true);
    return controller;
  }, []);

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
    [frames],
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
        className="rounded-xl border border-zinc-800 bg-zinc-900/45 p-4 sm:p-5"
      >
        <div className="flex items-center gap-2 border-b border-zinc-800 pb-4 text-zinc-100">
          <span className="inline-flex size-8 text-sm">
            <DiscFace cell={frame.disc} />
          </span>
          <span className="text-sm text-zinc-400">placed in</span>
          <strong className="text-lg">column {frame.column + 1}</strong>
        </div>

        <div className="mt-5 flex justify-center">
          <Drop7Board
            cells={presentation.board}
            size="min(100%, 18rem)"
            label={`Move ${frame.move}: disc ${frame.disc} placed in column ${frame.column + 1}`}
            cellClassName={cellMotion}
            cellStyle={cellStyle}
          />
        </div>

        <div className="mx-auto mt-4 max-w-[28rem] space-y-3">
          <div className="grid grid-cols-[1fr_auto_1fr] items-center gap-2">
            <button
              type="button"
              onClick={() => step(-1)}
              disabled={cursor === 0}
              className="rounded-lg border border-zinc-700 px-3 py-2 text-sm font-semibold text-zinc-300 transition-colors hover:border-zinc-500 hover:text-zinc-50 disabled:cursor-not-allowed disabled:opacity-35"
            >
              ← Previous
            </button>
            <button
              type="button"
              onClick={togglePlay}
              className="min-w-24 rounded-lg bg-violet-500 px-4 py-2 text-sm font-bold text-white transition-colors hover:bg-violet-400"
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
              className="rounded-lg border border-zinc-700 px-3 py-2 text-sm font-semibold text-zinc-300 transition-colors hover:border-zinc-500 hover:text-zinc-50 disabled:cursor-not-allowed disabled:opacity-35"
            >
              Next →
            </button>
          </div>
          <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-zinc-500">
            <span>Arrow keys step · space plays</span>
            {hasAnimations && (
              <button
                type="button"
                role="switch"
                aria-checked={animationsEnabled}
                onClick={toggleAnimations}
                className="inline-flex items-center gap-2 rounded-full border border-zinc-700 px-2.5 py-1.5 font-semibold text-zinc-300 hover:border-zinc-500"
              >
                <span
                  aria-hidden="true"
                  className={`relative h-4 w-7 rounded-full transition-colors ${
                    animationsEnabled ? "bg-violet-500" : "bg-zinc-700"
                  }`}
                >
                  <span
                    className={`absolute left-0 top-0.5 size-3 rounded-full bg-white transition-transform ${
                      animationsEnabled ? "translate-x-3.5" : "translate-x-0.5"
                    }`}
                  />
                </span>
                Animations {animationsEnabled ? "on" : "off"}
              </button>
            )}
          </div>
        </div>
      </section>

      <aside className="space-y-4">
        <section className="rounded-xl border border-zinc-800 bg-zinc-900/45 p-4">
          <div className="mb-3 flex items-end justify-between gap-3">
            <div>
              <p className="text-[0.65rem] font-semibold uppercase tracking-[0.14em] text-zinc-500">
                Score trajectory
              </p>
              <p className="mt-1 font-mono text-lg font-bold tabular-nums text-zinc-50">
                {formatInteger(frame.score)}
              </p>
            </div>
          </div>
          <ScoreChart frames={frames} cursor={cursor} />
        </section>

        <section className="rounded-xl border border-zinc-800 bg-zinc-900/45 p-4">
          <p className="mb-3 text-[0.65rem] font-semibold uppercase tracking-[0.14em] text-zinc-500">
            Move metrics
          </p>
          <div className="grid grid-cols-2 gap-2.5">
            <Metric label="Score" value={formatInteger(frame.score)} />
            <Metric label="This move" value={`+${formatInteger(frame.scoreDelta)}`} tone="emerald" />
            <Metric label="Chain" value={`×${frame.chainDepth}`} />
            <Metric label="Rise in" value={`${frame.movesRemaining}/5 drops`} />
            <Metric label="Cleared" value={`${frame.cleared} discs`} />
            <Metric label="Revealed" value={`${frame.revealed} gray`} />
          </div>
          {frame.levelAdvanced && (
            <p className="mt-2.5 rounded-lg border border-sky-500/30 bg-sky-500/10 px-3 py-2 text-xs font-semibold text-sky-200">
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
    <div className="rounded-lg border border-zinc-800 bg-zinc-950/55 px-3 py-2.5">
      <p className="text-[0.6rem] font-semibold uppercase tracking-[0.12em] text-zinc-600">
        {label}
      </p>
      <p
        className={`mt-0.5 font-mono text-sm font-bold tabular-nums ${
          tone === "emerald" ? "text-emerald-400" : "text-zinc-100"
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
