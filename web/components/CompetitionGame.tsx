"use client";

import Link from "next/link";
import { signIn } from "next-auth/react";
import "../app/app.css";
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  createInitialLatentValues,
  legalColumns,
  playMove,
  type DiscValue,
  type GameState,
  type LatentValues,
  type MoveAnimationFrame,
} from "../../src/core/typescript/engine.ts";
import type { ScriptedRound } from "../../src/bench/rounds.ts";
import type { CompetitionGameManifest } from "@/lib/competition/game";
import { Drop7Board } from "./Drop7Board";
import { DiscFace } from "./discs";
import styles from "./Drop7Game.module.css";
import { useExplosionPoints } from "./useExplosionPoints";

interface ScriptedSnapshot {
  state: GameState;
  latent: readonly (DiscValue | null)[];
  rowCursor: number;
}

interface AnimatedMove {
  snapshot: ScriptedSnapshot;
  animation: readonly MoveAnimationFrame[];
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
const FRAME_DURATION_MS = {
  drop: 380,
  burst: 175,
  impact: 130,
  settle: 210,
  rise: 280,
} satisfies Record<MoveAnimationFrame["kind"], number>;

export function CompetitionGame({
  manifest,
  round,
  showExplosionPoints = true,
}: {
  manifest: CompetitionGameManifest;
  round: ScriptedRound;
  /** Show each exploding disc's point value rising from the board. Defaults to true. */
  showExplosionPoints?: boolean;
}) {
  const [snapshot, setSnapshot] = useState(() => createStart(round));
  const snapshotRef = useRef(snapshot);
  const columnsRef = useRef<number[]>([]);
  const [animating, setAnimating] = useState(false);
  const animatingRef = useRef(false);
  const [animationFrame, setAnimationFrame] = useState<MoveAnimationFrame | null>(null);
  const animationRunRef = useRef(0);
  const {
    explosionPoints,
    captureExplosionFrame,
    clearExplosionPoints,
  } = useExplosionPoints(showExplosionPoints);
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

  useEffect(
    () => () => {
      animationRunRef.current += 1;
      animatingRef.current = false;
    },
    [],
  );

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
      if (submission || submitting || animatingRef.current) return;
      const current = snapshotRef.current;
      const move = advance(round, current, column, true);
      if (!move) return;
      const nextColumns = [...columnsRef.current, column];
      columnsRef.current = nextColumns;
      setNeedsAuth(false);
      setSubmitError(null);
      try {
        window.localStorage.setItem(storageKey, JSON.stringify({ columns: nextColumns }));
        if (move.snapshot.state.gameOver || move.snapshot.state.movesPlayed >= round.maximumMoves) {
          const prior = Number(window.localStorage.getItem(bestKey) ?? -1);
          if (move.snapshot.state.score > prior) {
            window.localStorage.setItem(bestKey, String(move.snapshot.state.score));
          }
        }
      } catch {
        // Local persistence is optional; play remains functional without it.
      }

      const run = animationRunRef.current + 1;
      animationRunRef.current = run;
      animatingRef.current = true;
      setAnimating(true);
      setAnimationFrame(null);
      clearExplosionPoints();
      const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
      const frames = reducedMotion ? [] : move.animation;

      const finishMove = () => {
        if (animationRunRef.current !== run) return;
        snapshotRef.current = move.snapshot;
        animatingRef.current = false;
        setSnapshot(move.snapshot);
        setAnimationFrame(null);
        setAnimating(false);
      };

      if (frames.length === 0) {
        finishMove();
        return;
      }

      void (async () => {
        for (const frame of frames) {
          if (animationRunRef.current !== run) return;
          captureExplosionFrame(frame);
          setAnimationFrame(frame);
          await wait(FRAME_DURATION_MS[frame.kind]);
        }
        finishMove();
      })();
    },
    [
      bestKey,
      captureExplosionFrame,
      clearExplosionPoints,
      round,
      storageKey,
      submission,
      submitting,
    ],
  );

  const restart = useCallback(() => {
    animationRunRef.current += 1;
    animatingRef.current = false;
    const start = createStart(round);
    snapshotRef.current = start;
    columnsRef.current = [];
    setSnapshot(start);
    setSubmission(null);
    setSubmitError(null);
    setNeedsAuth(false);
    setAnimationFrame(null);
    clearExplosionPoints();
    setAnimating(false);
    try {
      window.localStorage.removeItem(storageKey);
    } catch {
      // Ignore browsers that deny localStorage.
    }
  }, [clearExplosionPoints, round, storageKey]);

  const displayBoard = animationFrame?.board ?? snapshot.state.board;
  const animatedIndexes = useMemo(
    () => new Set(animationFrame?.indexes ?? []),
    [animationFrame],
  );
  const cellMotion = (index: number) =>
    animatedIndexes.has(index) && animationFrame ? styles[animationFrame.kind] : undefined;
  const cellStyle = (index: number) =>
    ({ "--drop7-rows": Math.floor(index / BOARD_SIZE) + 1 }) as CSSProperties;

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
    <section className="rounded-lg border border-rule bg-surface p-4 text-ink-1">
      <div className="mb-4 flex flex-wrap items-start justify-between gap-4 border-b border-rule pb-4">
        <div>
          <p className={LABEL + " text-accent"}>
            competition · {manifest.gameVersion}
          </p>
          <p className="mt-1.5 text-small text-ink-2">
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
            <span className={LABEL + " text-ink-3"}>drop</span>
            <span className="inline-flex size-8 text-sm">
              <DiscFace cell={snapshot.state.nextDisc} />
            </span>
          </div>
          <span className={LABEL + " text-ink-3"}>
            {completed ? "run complete" : animating ? animationLabel(animationFrame) : "tap a column"}
          </span>
        </div>

        <Drop7Board
          cells={displayBoard}
          size="100%"
          label={snapshot.state.movesPlayed + " moves played in " + manifest.name}
          cellClassName={cellMotion}
          cellStyle={cellStyle}
          explosionPoints={explosionPoints}
          showExplosionPoints={showExplosionPoints}
          overlay={
            <>
              <div className="absolute inset-1.5 grid grid-cols-7">
                {Array.from({ length: 7 }, (_, column) => (
                  <button
                    key={column}
                    type="button"
                    disabled={completed || animating || !legal.has(column)}
                    onClick={() => commitMove(column)}
                    aria-label={
                      legal.has(column)
                        ? "Drop " + snapshot.state.nextDisc + " in column " + (column + 1)
                        : "Column " + (column + 1) + " is full"
                    }
                    className="border-x border-transparent transition-colors motion-reduce:transition-none enabled:hover:border-accent/60 enabled:hover:bg-accent/10"
                  />
                ))}
              </div>
              {completed && (
                <div className="pointer-events-none absolute inset-1.5 flex items-center justify-center rounded-sm bg-bg/75 p-5 text-center backdrop-blur-[2px]">
                  <div className="rounded-md border border-rule-strong bg-surface p-5">
                    <p className="font-display text-h2 font-bold text-ink">
                      {formatInteger(snapshot.state.score)} points
                    </p>
                    <p className={LABEL + " mt-2 text-ink-2"}>
                      {snapshot.state.movesPlayed} verified choices ready
                    </p>
                  </div>
                </div>
              )}
            </>
          }
        />
      </div>

      <div className="mt-5 border-t border-rule pt-4">
        {completed ? (
          submission ? (
            <div className="space-y-3">
              <div role="status" className="app-notice app-notice--success w-full">
                <div className="flex items-start gap-3">
                  <span aria-hidden="true" className="app-notice-mark">
                    ✓
                  </span>
                  <div className="min-w-0">
                    <p className="font-semibold text-ink">Your most recent run was submitted.</p>
                    <p className="mt-1 text-small text-ink-1">
                      {formatInteger(submission.verifiedScore)} points are now recorded for this game.
                    </p>
                    {submission.scoreMismatch && (
                      <p className="mt-1 text-small text-status-paused">
                        The reported score differed from the replayed score and was flagged; the replayed score ranks.
                      </p>
                    )}
                    <Link
                      href={"/leaderboard/human/" + submission.submissionId}
                      className="mt-2 inline-block text-small font-semibold text-accent underline underline-offset-2 hover:text-ink"
                    >
                      View submitted replay →
                    </Link>
                  </div>
                </div>
              </div>
              <button
                type="button"
                onClick={restart}
                className="w-full rounded-md border border-accent-strong bg-accent-strong px-4 py-3 text-small font-bold uppercase tracking-[0.08em] text-accent-fg transition-[filter] motion-reduce:transition-none hover:brightness-110"
              >
                Play again
              </button>
              <p className="text-center text-caption text-ink-3">
                Starts a fresh local game and clears the saved choices from this run.
              </p>
            </div>
          ) : (
            <div className="space-y-3">
              <div className="flex flex-wrap items-center gap-2">
                <button
                  type="button"
                  onClick={() => void submit()}
                  disabled={submitting}
                  className="rounded-sm border border-accent-strong bg-accent-strong px-4 py-2.5 text-caption font-bold uppercase tracking-wide text-accent-fg transition-[filter] motion-reduce:transition-none hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-50"
                >
                  {submitting ? "validating…" : "submit verified run"}
                </button>
                <button
                  type="button"
                  onClick={restart}
                  className="rounded-sm border border-rule-strong px-4 py-2.5 text-caption font-bold uppercase tracking-wide text-ink-1 transition-colors motion-reduce:transition-none hover:border-accent hover:bg-accent-soft hover:text-ink"
                >
                  Play again
                </button>
              </div>
              {needsAuth && (
                <p className="text-small text-status-paused">
                  Sign in explicitly before sharing this run. Nothing has been uploaded yet.{" "}
                  <button
                    type="button"
                    onClick={() => void signIn("github", { redirectTo: "/compete" })}
                    className="font-semibold underline underline-offset-2"
                  >
                    Sign in with GitHub to contribute →
                  </button>
                </p>
              )}
              {submitError && <p className="text-small text-danger">{submitError}</p>}
            </div>
          )
        ) : (
          <div className="flex flex-wrap items-center justify-between gap-3 text-caption text-ink-3">
            <p>This run is local-only. No game data is sent while you play.</p>
            <button
              type="button"
              onClick={() => void signIn("github", { redirectTo: "/compete" })}
              className="text-ink-2 underline underline-offset-2 hover:text-ink"
            >
              Sign in to contribute later
            </button>
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
  captureAnimation: boolean,
): AnimatedMove | null {
  let nextRowCursor = snapshot.rowCursor;
  const move = playMove(snapshot.state, column, CONSTANT_RANDOM, {
    captureAnimation,
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
    snapshot: {
      state,
      latent: move.latentValues ?? snapshot.latent,
      rowCursor: nextRowCursor,
    },
    animation: move.animation,
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
    const next = advance(round, snapshot, column, false);
    if (!next) return null;
    snapshot = next.snapshot;
  }
  return snapshot;
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <p>
      <span className={LABEL + " block text-ink-3"}>{label}</span>
      <span className="font-mono text-small text-ink tabular-nums">{value}</span>
    </p>
  );
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function animationLabel(frame: MoveAnimationFrame | null) {
  if (!frame) return "moving";
  if (frame.kind === "drop") return "dropping";
  if (frame.kind === "rise") return "level up";
  if (frame.kind === "settle") return "settling";
  if (frame.kind === "burst") return `chain ${frame.chainDepth ?? 1}`;
  return "burst";
}

function wait(milliseconds: number) {
  return new Promise<void>((resolve) => window.setTimeout(resolve, milliseconds));
}
