"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { Board, DISC_COLORS } from "@/components/Board";
import type { ReplayData } from "@/lib/leaderboard";

function ScoreChart({
  frames,
  cursor,
  onSeek,
}: {
  frames: ReplayData["frames"];
  cursor: number;
  onSeek: (index: number) => void;
}) {
  const width = 640;
  const height = 90;
  const maxScore = Math.max(1, ...frames.map((frame) => frame.score));
  const points = frames.map(
    (frame, index) =>
      `${(index / Math.max(1, frames.length - 1)) * width},${
        height - (frame.score / maxScore) * (height - 8)
      }`,
  );
  const cursorX = (cursor / Math.max(1, frames.length - 1)) * width;
  return (
    <svg
      viewBox={`0 0 ${width} ${height}`}
      className="w-full cursor-crosshair rounded-lg bg-zinc-950"
      onClick={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const fraction = (event.clientX - rect.left) / rect.width;
        onSeek(Math.round(fraction * (frames.length - 1)));
      }}
      role="img"
      aria-label="Score over the game"
    >
      <polyline
        points={points.join(" ")}
        fill="none"
        stroke="#38bdf8"
        strokeWidth={2}
      />
      {frames.map(
        (frame, index) =>
          frame.levelAdvanced && (
            <line
              key={index}
              x1={(index / Math.max(1, frames.length - 1)) * width}
              y1={0}
              x2={(index / Math.max(1, frames.length - 1)) * width}
              y2={height}
              stroke="#3f3f46"
              strokeDasharray="3 3"
            />
          ),
      )}
      <line x1={cursorX} y1={0} x2={cursorX} y2={height} stroke="#facc15" strokeWidth={2} />
    </svg>
  );
}

export function ReplayPlayer({ game }: { game: ReplayData }) {
  const [cursor, setCursor] = useState(0);
  const [playing, setPlaying] = useState(false);
  const frames = game.frames;
  const frame = frames[cursor];

  const step = useCallback(
    (delta: number) =>
      setCursor((current) =>
        Math.max(0, Math.min(frames.length - 1, current + delta)),
      ),
    [frames.length],
  );

  useEffect(() => {
    if (!playing) return;
    const timer = setInterval(() => {
      setCursor((current) => {
        if (current >= frames.length - 1) {
          setPlaying(false);
          return current;
        }
        return current + 1;
      });
    }, 220);
    return () => clearInterval(timer);
  }, [playing, frames.length]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "ArrowLeft") step(-1);
      if (event.key === "ArrowRight") step(1);
      if (event.key === " ") {
        event.preventDefault();
        setPlaying((value) => !value);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [step]);

  const upcoming = useMemo(
    () => frames.slice(cursor, cursor + 5).map((f) => f.disc),
    [frames, cursor],
  );

  if (!frame) return null;

  return (
    <div className="grid gap-6 lg:grid-cols-[auto_1fr]">
      <div className="flex flex-col items-center gap-3">
        <Board cells={frame.board} size={340} />
        <input
          type="range"
          min={0}
          max={frames.length - 1}
          value={cursor}
          onChange={(event) => setCursor(Number(event.target.value))}
          className="w-full accent-sky-500"
          aria-label="Move"
        />
        <div className="flex items-center gap-2">
          <button
            onClick={() => step(-1)}
            className="rounded-lg border border-zinc-700 px-3 py-1.5 text-sm hover:bg-zinc-800"
          >
            ← Prev
          </button>
          <button
            onClick={() => setPlaying((value) => !value)}
            className="rounded-lg bg-sky-600 px-4 py-1.5 text-sm font-semibold text-white hover:bg-sky-500"
          >
            {playing ? "Pause" : "Play"}
          </button>
          <button
            onClick={() => step(1)}
            className="rounded-lg border border-zinc-700 px-3 py-1.5 text-sm hover:bg-zinc-800"
          >
            Next →
          </button>
        </div>
        <p className="text-xs text-zinc-600">
          Arrow keys step, space plays. Move {frame.move} of {frames.length}.
        </p>
      </div>

      <div className="space-y-4">
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
          <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">Score</div>
            <div className="text-lg font-bold text-zinc-50">
              {frame.score.toLocaleString()}
            </div>
          </div>
          <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">This move</div>
            <div className="text-lg font-bold text-emerald-400">
              +{frame.scoreDelta.toLocaleString()}
            </div>
          </div>
          <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">Chain</div>
            <div className="text-lg font-bold text-zinc-50">×{frame.chainDepth}</div>
          </div>
          <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">
              Rise clock
            </div>
            <div className="text-lg font-bold text-zinc-50">
              {frame.movesRemaining}
              <span className="text-sm text-zinc-500">/5</span>
            </div>
          </div>
        </div>

        <div className="rounded-xl border border-zinc-800 bg-zinc-900/60 px-4 py-3">
          <div className="mb-2 text-[10px] uppercase tracking-wide text-zinc-500">
            Disc tape (predetermined)
          </div>
          <div className="flex items-center gap-1.5">
            {upcoming.map((disc, index) => (
              <span
                key={index}
                className={`flex h-8 w-8 items-center justify-center rounded-full text-sm font-bold text-white ${
                  index === 0 ? "ring-2 ring-yellow-400" : "opacity-70"
                }`}
                style={{ background: DISC_COLORS[disc] }}
                title={index === 0 ? "Played this move" : `Upcoming disc ${index}`}
              >
                {disc}
              </span>
            ))}
            <span className="ml-2 text-xs text-zinc-500">
              played column {frame.column + 1}
            </span>
          </div>
        </div>

        <ScoreChart frames={frames} cursor={cursor} onSeek={(index) => setCursor(index)} />

        <div className="grid grid-cols-3 gap-3 text-center text-sm">
          <div className="rounded-xl border border-zinc-800 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">Cleared</div>
            <div className="font-semibold text-zinc-200">{frame.cleared} discs</div>
          </div>
          <div className="rounded-xl border border-zinc-800 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">Revealed</div>
            <div className="font-semibold text-zinc-200">{frame.revealed} gray</div>
          </div>
          <div className="rounded-xl border border-zinc-800 px-3 py-2">
            <div className="text-[10px] uppercase tracking-wide text-zinc-500">Level rise</div>
            <div className="font-semibold text-zinc-200">
              {frame.levelAdvanced ? "yes +17,000" : "no"}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
