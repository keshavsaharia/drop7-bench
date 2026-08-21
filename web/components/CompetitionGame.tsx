"use client";

import Link from "next/link";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  MOVES_PER_LEVEL,
  createInitialBoard,
  createInitialLatentValues,
  legalColumns,
  playMove,
  type DiscValue,
  type GameState,
  type LatentValues,
} from "../../src/core/typescript/engine.ts";
import type { ScriptedRound } from "../../src/bench/rounds.ts";
import type { CompetitionGameManifest } from "@/lib/competition/game";
import { Drop7Board } from "./Drop7Board";
import { DiscFace } from "./discs";

interface ScriptedSnapshot {
  state: GameState;
  latent: readonly (DiscValue | null)[];
  rowCursor: number;
}

interface SubmissionResult {
  submissionId: string;
  verifiedScore: number;
  clientScore: number;
  scoreMismatch: boolean;
  duplicate: boolean;
}

const LABEL = "text-[0.625rem] font-semibold uppercase tracking-[0.12em]";
const CONSTANT_RANDOM = () => 0.5;

export function CompetitionGame({
  manifest,
  round,
}: {
  manifest: CompetitionGameManifest;
  round: ScriptedRound;
}) {
  const [snapshot, setSnapshot] = useState(() => createStart(round));
  const snapshotRef = useRef(snapshot);
  const columnsRef = useRef<number[]>([]);
  const [submitting, setSubmitting] = useState(false);
  const [needsAuth, setNeedsAuth] = useState(false);
  const [submission, setSubmission] = useState<SubmissionResult | null>(null);
  const [submitError, setSubmitError] = useState<string | null>(null);
  const storageKey = "drop7-competition:" + manifest.gameVersion + ":moves";
  const bestKey = "drop7-competition:" + manifest.gameVersion + ":best";

  const completed =
    snapshot.state.gameOver || snapshot.state.movesPlayed >= round.maximumMoves;
  const legal = useMemo(
    () => new Set(legalColumns(snapshot.state.board)),
    [snapshot.state.board],
  );

  useEffect(() => {
    snapshotRef.current = snapshot;
  }, [snapshot]);

  useEffect(() => {
    const frame = window.requestAnimationFrame(() => {
      try {
        const stored = window.localStorage.getItem(storageKey);
        if (!stored) return;
        const parsed = JSON.parse(stored) as { columns?: unknown };
        if (
          !Array.isArray(parsed.columns) ||
          !parsed.columns.every(
            (column) => Number.isInteger(column) && column >= 0 && column <= 6,
          )
        ) {
          return;
        }
        const restoredColumns = parsed.columns as number[];
        const restored = replayLocal(round, restoredColumns);
        if (!restored) return;
        snapshotRef.current = restored;
        columnsRef.current = restoredColumns;
        setSnapshot(restored);
      } catch {
        // A malformed local draft is ignored; nothing was sent anywhere.
      }
    });
    return () => window.cancelAnimationFrame(frame);
  }, [round, storageKey]);

  const commitMove = useCallback(
    (column: number) => {
      if (submission || submitting) return;
      const current = snapshotRef.current;
      const next = advance(round, current, column);
      if (!next) return;
      const nextColumns = [...columnsRef.current, column];
      snapshotRef.current = next;
      columnsRef.current = nextColumns;
      setSnapshot(next);
      setNeedsAuth(false);
      setSubmitError(null);
      try {
        window.localStorage.setItem(storageKey, JSON.stringify({ columns: nextColumns }));
        if (next.state.gameOver || next.state.movesPlayed >= round.maximumMoves) {
          const prior = Number(window.localStorage.getItem(bestKey) ?? -1);
          if (next.state.score > prior) {
            window.localStorage.setItem(bestKey, String(next.state.score));
          }
        }
      } catch {
        // Local persistence is optional; play remains functional without it.
      }
    },
    [bestKey, round, storageKey, submission, submitting],
  );

  const restart = useCallback(() => {
    const start = createStart(round);
    snapshotRef.current = start;
    columnsRef.current = [];
    setSnapshot(start);
    setSubmission(null);
    setSubmitError(null);
    setNeedsAuth(false);
    try {
      window.localStorage.removeItem(storageKey);
    } catch {
      // Ignore browsers that deny localStorage.
    }
  }, [round, storageKey]);

  const submit = useCallback(async () => {
    if (!completed || submitting || submission) return;
    setSubmitting(true);
    setSubmitError(null);
    setNeedsAuth(false);
    try {
      const response = await fetch("/api/competition/submit", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          competitionId: manifest.competitionId,
          gameVersion: manifest.gameVersion,
          columns: columnsRef.current,
          clientScore: snapshot.state.score,
        }),
      });
      if (response.status === 401) {
        setNeedsAuth(true);
        return;
      }
      const body = (await response.json()) as SubmissionResult & { error?: string };
      if (!response.ok) {
        setSubmitError(body.error ?? "The run could not be validated.");
        return;
      }
      setSubmission(body);
    } catch {
      setSubmitError("The submission service is unavailable. Your game remains in this browser.");
    } finally {
      setSubmitting(false);
    }
  }, [completed, manifest, snapshot.state.score, submission, submitting]);

  return (
    <section className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-4 text-zinc-300">
      <div className="mb-4 flex flex-wrap items-start justify-between gap-4 border-b border-zinc-800 pb-4">
        <div>
          <p className={LABEL + " text-violet-400"}>
            competition · {manifest.gameVersion}
          </p>
          <p className="mt-1.5 text-sm text-zinc-400">
            Everyone receives this exact disc and reveal tape.
          </p>
        </div>
        <div className="flex items-center gap-5 text-right">
          <Stat label="score" value={formatInteger(snapshot.state.score)} />
          <Stat label="drops" value={formatInteger(snapshot.state.movesPlayed)} />
          <Stat label="row in" value={snapshot.state.movesRemaining.toString()} />
        </div>
      </div>

      <div className="mx-auto max-w-[27rem]">
        <div className="mb-3 flex items-center justify-between gap-3">
          <div className="flex items-center gap-2.5">
            <span className={LABEL + " text-zinc-500"}>drop</span>
            <span className="inline-flex size-8 text-sm">
              <DiscFace cell={snapshot.state.nextDisc} />
            </span>
          </div>
          <span className={LABEL + " text-zinc-500"}>
            {completed ? "run complete" : "tap a column"}
          </span>
        </div>

        <Drop7Board
          cells={snapshot.state.board}
          size="100%"
          label={snapshot.state.movesPlayed + " moves played in " + manifest.name}
          overlay={
            <>
              <div className="absolute inset-1.5 grid grid-cols-7">
                {Array.from({ length: 7 }, (_, column) => (
                  <button
                    key={column}
                    type="button"
                    disabled={completed || !legal.has(column)}
                    onClick={() => commitMove(column)}
                    aria-label={
                      legal.has(column)
                        ? "Drop " + snapshot.state.nextDisc + " in column " + (column + 1)
                        : "Column " + (column + 1) + " is full"
                    }
                    className="border-x border-transparent transition-colors enabled:hover:border-violet-500/60 enabled:hover:bg-violet-400/10"
                  />
                ))}
              </div>
              {completed && (
                <div className="pointer-events-none absolute inset-1.5 flex items-center justify-center rounded-sm bg-zinc-950/75 p-5 text-center backdrop-blur-[2px]">
                  <div className="rounded-lg border border-zinc-700 bg-zinc-900 p-5 shadow-xl">
                    <p className="text-2xl font-black text-zinc-50">
                      {formatInteger(snapshot.state.score)} points
                    </p>
                    <p className={LABEL + " mt-2 text-zinc-400"}>
                      {snapshot.state.movesPlayed} verified choices ready
                    </p>
                  </div>
                </div>
              )}
            </>
          }
        />
      </div>

      <div className="mt-5 border-t border-zinc-800 pt-4">
        {completed ? (
          <div className="space-y-3">
            <div className="flex flex-wrap items-center gap-2">
              <button
                type="button"
                onClick={() => void submit()}
                disabled={submitting || Boolean(submission)}
                className="rounded-md border border-violet-500 bg-violet-500 px-3 py-2 text-xs font-bold uppercase tracking-wide text-white transition-colors hover:bg-violet-400 disabled:cursor-not-allowed disabled:opacity-50"
              >
                {submitting ? "validating…" : submission ? "submitted" : "submit verified run"}
              </button>
              <button
                type="button"
                onClick={restart}
                className="rounded-md border border-zinc-700 px-3 py-2 text-xs font-bold uppercase tracking-wide text-zinc-300 hover:border-zinc-500 hover:text-zinc-50"
              >
                play again
              </button>
            </div>
            {needsAuth && (
              <p className="text-sm text-amber-200">
                Sign in explicitly before sharing this run. Nothing has been uploaded yet.{" "}
                <Link
                  href="/api/auth/signin/github?callbackUrl=%2Fcompete"
                  prefetch={false}
                  className="font-semibold underline underline-offset-2"
                >
                  Sign in with GitHub to contribute →
                </Link>
              </p>
            )}
            {submission && (
              <p className="text-sm text-emerald-300">
                Lambda replay verified {formatInteger(submission.verifiedScore)} points.
                {submission.scoreMismatch && (
                  <span className="ml-1 text-amber-300">
                    The client score differed and was flagged; the replayed score ranks.
                  </span>
                )}{" "}
                <Link
                  href={"/leaderboard/human/" + submission.submissionId}
                  className="font-semibold underline underline-offset-2"
                >
                  View replay →
                </Link>
              </p>
            )}
            {submitError && <p className="text-sm text-red-300">{submitError}</p>}
          </div>
        ) : (
          <div className="flex flex-wrap items-center justify-between gap-3 text-xs text-zinc-500">
            <p>This run is local-only. No game data is sent while you play.</p>
            <Link
              href="/api/auth/signin/github?callbackUrl=%2Fcompete"
              prefetch={false}
              className="text-zinc-400 underline underline-offset-2 hover:text-zinc-200"
            >
              Sign in to contribute later
            </Link>
          </div>
        )}
      </div>
    </section>
  );
}

function createStart(round: ScriptedRound): ScriptedSnapshot {
  return {
    state: {
      board: createInitialBoard(),
      nextDisc: round.discs[0],
      score: 0,
      level: 1,
      movesRemaining: MOVES_PER_LEVEL,
      movesPlayed: 0,
      gameOver: false,
    },
    latent: createInitialLatentValues(round.latentRows[0]),
    rowCursor: 1,
  };
}

function advance(
  round: ScriptedRound,
  snapshot: ScriptedSnapshot,
  column: number,
): ScriptedSnapshot | null {
  let nextRowCursor = snapshot.rowCursor;
  const move = playMove(snapshot.state, column, CONSTANT_RANDOM, {
    captureAnimation: false,
    latent: {
      values: snapshot.latent as LatentValues,
      nextCoveredRow: () => round.latentRows[nextRowCursor++],
    },
  });
  if (!move) return null;
  let state = move.state;
  if (!state.gameOver && state.movesPlayed < round.maximumMoves) {
    state = { ...state, nextDisc: round.discs[state.movesPlayed] };
  }
  return {
    state,
    latent: move.latentValues ?? snapshot.latent,
    rowCursor: nextRowCursor,
  };
}

function replayLocal(
  round: ScriptedRound,
  columns: readonly number[],
): ScriptedSnapshot | null {
  if (columns.length > round.maximumMoves) return null;
  let snapshot = createStart(round);
  for (const column of columns) {
    if (!legalColumns(snapshot.state.board).includes(column)) return null;
    const next = advance(round, snapshot, column);
    if (!next) return null;
    snapshot = next;
  }
  return snapshot;
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <p>
      <span className={LABEL + " block text-zinc-500"}>{label}</span>
      <span className="font-mono text-sm text-zinc-50 tabular-nums">{value}</span>
    </p>
  );
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}
