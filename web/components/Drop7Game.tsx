"use client";

/**
 * The playable Drop7 simulator. Three modes: `play` (you choose every column),
 * `evaluate` (the solver recommends, you decide) and `auto` (the solver plays).
 *
 * Rules come from the repository's TypeScript engine; the board is drawn by
 * `Drop7Board`; the solver is `fastEvaluateMoves` (web/lib/play) running in a
 * Web Worker, which is the expectimax from src/core/typescript/solver.ts with a
 * parity-tested faster move generator and leaf. Nothing here is research
 * evidence — a game in a visitor's browser is a demonstration of the rules
 * and of one policy's behaviour.
 */

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  useSyncExternalStore,
  type CSSProperties,
  type ReactNode,
} from "react";
import {
  BOARD_SIZE,
  CRACKED,
  SOLID,
  createGame,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
  type MoveAnimationFrame,
  type MoveResult,
  type RandomSource,
} from "../../src/core/typescript/engine.ts";
import type { EvaluationResult } from "../../src/core/typescript/solver.ts";
import type { SolverRequest, SolverResponse } from "@/lib/play/solver.protocol";
import { Drop7Board, type ColumnNote } from "./Drop7Board";
import { DiscFace } from "./discs";
import styles from "./Drop7Game.module.css";

export type Drop7Mode = "play" | "evaluate" | "auto";

export interface Drop7GameProps {
  /** Starting mode. */
  mode?: Drop7Mode;
  /** Show a play / evaluate / auto switch. */
  modeSwitcher?: boolean;
  /** Initial search horizon; the in-game control allows 1 through 8 ply. */
  maxDepth?: number;
  /** Initial per-move search budget in milliseconds (shown in seconds). */
  timeLimitMs?: number;
  /** Seed of the first game. Fixed by default so server and client render the same board. */
  seed?: number;
  /** When set, the best score is remembered in this browser under this key. */
  bestScoreKey?: string;
  /** Start the solver immediately instead of when the board scrolls into view. */
  eager?: boolean;
  caption?: ReactNode;
}

interface LastMove {
  column: number;
  scoreDelta: number;
  chainLength: number;
  levelAdvanced: boolean;
  clearedBoard: boolean;
}

const INITIAL_SEED = 0xd7072010;
const AUTO_MOVE_DELAY_MS = 520;
const MIN_SEARCH_DEPTH = 1;
const MAX_SEARCH_DEPTH = 8;
const DEFAULT_SEARCH_DEPTH = 4;
const MIN_TIME_LIMIT_SECONDS = 1;
const DEFAULT_TIME_LIMIT_MS = 1_000;
const FRAME_DURATION_MS = {
  drop: 380,
  burst: 175,
  impact: 130,
  settle: 210,
  rise: 280,
} satisfies Record<MoveAnimationFrame["kind"], number>;

const MODE_COPY: Record<Drop7Mode, string> = {
  play: "you choose every column",
  evaluate: "the search recommends; you decide",
  auto: "the search chooses and plays",
};

const LABEL = "text-[0.625rem] font-semibold uppercase tracking-[0.12em]";

/* Personal best: localStorage as an external store, so no effect sets state. */
const bestListeners = new Set<() => void>();
function subscribeBest(callback: () => void) {
  bestListeners.add(callback);
  window.addEventListener("storage", callback);
  return () => {
    bestListeners.delete(callback);
    window.removeEventListener("storage", callback);
  };
}
function readBest(key: string | undefined): number | null {
  if (!key) return null;
  try {
    const stored = window.localStorage.getItem(key);
    return stored !== null && Number.isFinite(Number(stored)) ? Number(stored) : null;
  } catch {
    return null;
  }
}
function recordBest(key: string | undefined, score: number) {
  if (!key) return;
  const current = readBest(key);
  if (current !== null && score <= current) return;
  try {
    window.localStorage.setItem(key, String(score));
  } catch {
    return;
  }
  for (const listener of bestListeners) listener();
}

const BUTTON =
  `${LABEL} rounded-md border border-zinc-700 px-2.5 py-1.5 text-zinc-300 transition-colors hover:border-zinc-500 hover:text-zinc-50 disabled:cursor-not-allowed disabled:opacity-40`;

export function Drop7Game({
  mode: initialMode = "play",
  modeSwitcher = false,
  maxDepth = DEFAULT_SEARCH_DEPTH,
  timeLimitMs = DEFAULT_TIME_LIMIT_MS,
  seed: initialSeed = INITIAL_SEED,
  bestScoreKey,
  eager = false,
  caption,
}: Drop7GameProps) {
  const [mode, setMode] = useState<Drop7Mode>(initialMode);
  const [depth, setDepth] = useState(() => clamp(Math.trunc(maxDepth), MIN_SEARCH_DEPTH, MAX_SEARCH_DEPTH));
  const [timeLimitSeconds, setTimeLimitSeconds] = useState(() => normalizeTimeLimit(timeLimitMs / 1_000));
  const [seed, setSeed] = useState(() => initialSeed >>> 0);
  const randomRef = useRef<RandomSource>(runtimeRandom(initialSeed >>> 0));
  const [game, setGame] = useState<GameState>(() => createGame(seededRandom(initialSeed >>> 0)));
  const gameRef = useRef(game);
  const hostRef = useRef<HTMLElement>(null);
  const [active, setActive] = useState(eager || initialMode === "play");
  const [evaluation, setEvaluation] = useState<EvaluationResult | null>(null);
  const [thinking, setThinking] = useState(false);
  const [searchError, setSearchError] = useState(false);
  const [autoRunning, setAutoRunning] = useState(initialMode === "auto");
  const autoRunningRef = useRef(initialMode === "auto");
  const [animating, setAnimating] = useState(false);
  const animatingRef = useRef(false);
  const [animationFrame, setAnimationFrame] = useState<MoveAnimationFrame | null>(null);
  const animationRunRef = useRef(0);
  const [lastMove, setLastMove] = useState<LastMove | null>(null);
  const bestScore = useSyncExternalStore(
    subscribeBest,
    () => readBest(bestScoreKey),
    () => null,
  );

  useEffect(() => {
    gameRef.current = game;
  }, [game]);

  useEffect(
    () => () => {
      animationRunRef.current += 1;
      animatingRef.current = false;
    },
    [],
  );

  // Search only once the board is on screen, so an embedded game does not
  // burn a core while the reader is elsewhere on the page.
  useEffect(() => {
    if (active || mode === "play") return;
    const host = hostRef.current;
    if (!host || typeof IntersectionObserver === "undefined") {
      setActive(true);
      setThinking(true);
      return;
    }
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (!entry.isIntersecting) return;
        setActive(true);
        setThinking(true);
        observer.disconnect();
      },
      { rootMargin: "240px" },
    );
    observer.observe(host);
    return () => observer.disconnect();
  }, [active, mode]);

  const commitMove = useCallback(
    (column: number, expectedMove?: number) => {
      if (animatingRef.current) return;
      const current = gameRef.current;
      if (expectedMove !== undefined && current.movesPlayed !== expectedMove) return;
      const result = playMove(current, column, randomRef.current);
      if (!result) return;

      const run = animationRunRef.current + 1;
      animationRunRef.current = run;
      animatingRef.current = true;
      setAnimating(true);
      setAnimationFrame(null);
      setEvaluation(null);
      setThinking(false);
      setSearchError(false);

      const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
      const frames = reducedMotion ? [] : result.animation;

      const finishMove = () => {
        if (animationRunRef.current !== run) return;
        gameRef.current = result.state;
        animatingRef.current = false;
        setGame(result.state);
        setAnimationFrame(null);
        setAnimating(false);
        setLastMove(toLastMove(column, result));
        if (result.state.gameOver) recordBest(bestScoreKey, result.state.score);
        setThinking(
          mode !== "play" && active && !result.state.gameOver && (mode !== "auto" || autoRunningRef.current),
        );
      };

      if (frames.length === 0) {
        finishMove();
        return;
      }

      void (async () => {
        for (const frame of frames) {
          if (animationRunRef.current !== run) return;
          setAnimationFrame(frame);
          await wait(FRAME_DURATION_MS[frame.kind]);
        }
        finishMove();
      })();
    },
    [active, bestScoreKey, mode],
  );

  // One worker per evaluated position; terminated as soon as the position changes.
  useEffect(() => {
    if (!active || animating || mode === "play" || game.gameOver) return;
    if (mode === "auto" && !autoRunning) return;

    let cancelled = false;
    let moveTimer: number | undefined;
    let worker: Worker | undefined;

    const searchTimer = window.setTimeout(() => {
      try {
        worker = new Worker(new URL("../lib/play/solver.worker.ts", import.meta.url), {
          type: "module",
          name: "drop7-expectimax",
        });
      } catch {
        if (!cancelled) {
          setSearchError(true);
          setThinking(false);
        }
        return;
      }
      const request: SolverRequest = {
        state: game,
        maxDepth: depth,
        timeLimitMs: timeLimitSeconds * 1_000,
      };

      worker.addEventListener("message", (event: MessageEvent<SolverResponse>) => {
        if (cancelled) return;
        if (event.data.type === "progress") {
          setEvaluation(event.data.result);
          setSearchError(false);
          return;
        }
        worker?.terminate();
        worker = undefined;
        if (event.data.type === "error") {
          setSearchError(true);
          setThinking(false);
          return;
        }
        const { result } = event.data;
        setEvaluation(result);
        setSearchError(false);
        setThinking(false);
        if (mode === "auto" && result.bestColumn !== null) {
          moveTimer = window.setTimeout(() => {
            if (!cancelled) commitMove(result.bestColumn!, game.movesPlayed);
          }, AUTO_MOVE_DELAY_MS);
        }
      });
      worker.addEventListener("error", () => {
        if (cancelled) return;
        worker?.terminate();
        worker = undefined;
        setSearchError(true);
        setThinking(false);
      });
      worker.postMessage(request);
    }, 30);

    return () => {
      cancelled = true;
      window.clearTimeout(searchTimer);
      if (moveTimer !== undefined) window.clearTimeout(moveTimer);
      worker?.terminate();
    };
  }, [active, animating, autoRunning, commitMove, depth, game, mode, timeLimitSeconds]);

  const startGame = useCallback(
    (nextSeed: number) => {
      animationRunRef.current += 1;
      animatingRef.current = false;
      randomRef.current = runtimeRandom(nextSeed);
      const next = createGame(seededRandom(nextSeed));
      gameRef.current = next;
      setSeed(nextSeed);
      setGame(next);
      setAnimationFrame(null);
      setAnimating(false);
      setEvaluation(null);
      setSearchError(false);
      setLastMove(null);
      setThinking(mode !== "play" && active);
      const shouldAutoRun = mode === "auto";
      autoRunningRef.current = shouldAutoRun;
      setAutoRunning(shouldAutoRun);
    },
    [active, mode],
  );

  const newGame = useCallback(() => startGame(randomSeed()), [startGame]);
  const replayGame = useCallback(() => startGame(seed), [seed, startGame]);

  const switchMode = (next: Drop7Mode) => {
    if (next === mode) return;
    setMode(next);
    setEvaluation(null);
    setSearchError(false);
    const shouldAutoRun = next === "auto";
    autoRunningRef.current = shouldAutoRun;
    setAutoRunning(shouldAutoRun);
    if (next !== "play") setActive(true);
    setThinking(next !== "play" && !game.gameOver);
  };

  const displayBoard = animationFrame?.board ?? game.board;
  const legal = useMemo(() => new Set(legalColumns(game.board)), [game.board]);
  const animatedIndexes = useMemo(() => new Set(animationFrame?.indexes ?? []), [animationFrame]);
  const evaluationByColumn = useMemo(
    () => new Map(evaluation?.columns.map((column) => [column.column, column]) ?? []),
    [evaluation],
  );
  const columnNotes = useMemo<ColumnNote[] | undefined>(() => {
    if (mode === "play") return undefined;
    return Array.from({ length: BOARD_SIZE }, (_, column) => {
      const item = evaluationByColumn.get(column);
      const best = evaluation?.bestColumn === column;
      return {
        label: best ? "best" : `c${column + 1}`,
        value: item ? item.value : null,
        best,
        muted: !legal.has(column),
      };
    });
  }, [evaluation, evaluationByColumn, legal, mode]);

  const updateDepth = (next: number) => {
    const normalized = clamp(Math.trunc(next), MIN_SEARCH_DEPTH, MAX_SEARCH_DEPTH);
    if (normalized !== depth) {
      setDepth(normalized);
      setEvaluation(null);
      setSearchError(false);
      setThinking(mode === "evaluate" || (mode === "auto" && autoRunning));
    }
    return normalized;
  };

  const updateTimeLimit = (next: number) => {
    const normalized = normalizeTimeLimit(next);
    if (normalized !== timeLimitSeconds) {
      setTimeLimitSeconds(normalized);
      setEvaluation(null);
      setSearchError(false);
      setThinking(mode === "evaluate" || (mode === "auto" && autoRunning));
    }
    return normalized;
  };

  const cellMotion = (index: number) =>
    animatedIndexes.has(index) && animationFrame ? styles[animationFrame.kind] : undefined;
  const cellStyle = (index: number) =>
    ({ "--drop7-rows": Math.floor(index / BOARD_SIZE) + 1 }) as CSSProperties;

  const recommendation =
    evaluation?.bestColumn !== null && evaluation?.bestColumn !== undefined
      ? `column ${evaluation.bestColumn + 1}`
      : searchError
        ? "search error"
        : thinking
          ? "searching…"
          : "paused";

  return (
    <section
      ref={hostRef}
      aria-label={`Drop7 in ${mode} mode`}
      aria-busy={animating || thinking}
      className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-4 text-zinc-300"
    >
      <div className="mb-4 flex flex-wrap items-start justify-between gap-4 border-b border-zinc-800 pb-4">
        <div>
          {modeSwitcher ? (
            <div role="group" aria-label="Mode" className="inline-flex rounded-md border border-zinc-700 p-0.5">
              {(["play", "evaluate", "auto"] as const).map((option) => (
                <button
                  key={option}
                  type="button"
                  onClick={() => switchMode(option)}
                  aria-pressed={mode === option}
                  className={`${LABEL} rounded px-2.5 py-1 transition-colors ${
                    mode === option ? "bg-sky-500/20 text-sky-300" : "text-zinc-400 hover:text-zinc-100"
                  }`}
                >
                  {option}
                </button>
              ))}
            </div>
          ) : (
            <p className={`${LABEL} text-sky-400`}>{mode}</p>
          )}
          <p className="mt-1.5 text-sm text-zinc-400">{MODE_COPY[mode]}</p>
        </div>
        <div className="flex items-center gap-5 text-right">
          <Stat label="score" value={formatInteger(game.score)} />
          <Stat label="level" value={game.level.toString()} />
          <Stat label="row in" value={game.movesRemaining.toString()} />
          {bestScoreKey && <Stat label="your best" value={bestScore === null ? "—" : formatInteger(bestScore)} />}
        </div>
      </div>

      <div className={mode === "play" ? "mx-auto max-w-[27rem]" : "grid items-start gap-5 md:grid-cols-[minmax(0,1fr)_15rem]"}>
        <div className="min-w-0">
          <div className="mb-3 flex items-center justify-between gap-3">
            <div className="flex items-center gap-2.5">
              <span className={`${LABEL} text-zinc-500`}>drop</span>
              <span className="inline-flex size-8 text-sm">
                <DiscFace cell={game.nextDisc} />
              </span>
              {animating ? (
                <span className={`${LABEL} text-sky-400`} aria-live="polite">
                  {animationLabel(animationFrame)}
                </span>
              ) : thinking ? (
                <span className={`${LABEL} text-sky-400`} aria-live="polite">
                  searching<span className={styles.caret}>_</span>
                </span>
              ) : null}
            </div>
            {lastMove ? (
              <span className={`${LABEL} text-zinc-500 tabular-nums`} aria-live="polite">
                c{lastMove.column + 1} · +{formatInteger(lastMove.scoreDelta)}
                {lastMove.chainLength > 1 ? ` · chain ×${lastMove.chainLength}` : ""}
                {lastMove.levelAdvanced ? " · level up" : ""}
                {lastMove.clearedBoard ? " · board clear" : ""}
              </span>
            ) : (
              <span className={`${LABEL} text-zinc-500`}>tap a column</span>
            )}
          </div>

          <Drop7Board
            cells={displayBoard}
            size="100%"
            columns={columnNotes}
            cellClassName={cellMotion}
            cellStyle={cellStyle}
            label={`${displayBoard.filter((cell) => cell !== 0).length} occupied cells, including ${
              displayBoard.filter((cell) => cell === SOLID || cell === CRACKED).length
            } gray discs`}
            overlay={
              <>
                <div className="absolute inset-1.5 grid grid-cols-7">
                  {Array.from({ length: BOARD_SIZE }, (_, column) => {
                    const isBest = evaluation?.bestColumn === column;
                    const disabled =
                      game.gameOver || animating || !legal.has(column) || (mode === "auto" && autoRunning);
                    return (
                      <button
                        key={column}
                        type="button"
                        disabled={disabled}
                        onClick={() => commitMove(column)}
                        aria-label={
                          legal.has(column)
                            ? `Drop ${game.nextDisc} in column ${column + 1}${isBest ? ", recommended" : ""}`
                            : `Column ${column + 1} is full`
                        }
                        className={`border-x border-transparent transition-colors enabled:hover:border-sky-500/60 enabled:hover:bg-sky-400/10 ${
                          isBest ? "border-sky-500/70 bg-sky-400/10" : ""
                        }`}
                      />
                    );
                  })}
                </div>
                {game.gameOver && (
                  <div className="absolute inset-1.5 z-10 flex items-center justify-center rounded-sm bg-zinc-950/85 p-5 text-center backdrop-blur-[2px]">
                    <div className="rounded-lg border border-zinc-700 bg-zinc-900 p-5 shadow-xl">
                      <p className="text-2xl font-black text-zinc-50">board overflow</p>
                      <p className={`${LABEL} mt-2 text-zinc-400`}>
                        {formatInteger(game.score)} points · {game.movesPlayed} drops · level {game.level}
                      </p>
                      <div className="mt-4 flex justify-center gap-2">
                        <button
                          type="button"
                          onClick={newGame}
                          className={`${LABEL} rounded-md border border-sky-500 bg-sky-500 px-3 py-2 text-white transition-colors hover:bg-sky-400`}
                        >
                          new game
                        </button>
                        <button type="button" onClick={replayGame} className={BUTTON}>
                          replay this seed
                        </button>
                      </div>
                    </div>
                  </div>
                )}
              </>
            }
          />
        </div>

        {mode !== "play" && (
          <aside className="rounded-lg border border-zinc-800 bg-zinc-950/60 p-4">
            <div className="flex items-start justify-between gap-3">
              <div>
                <p className={`${LABEL} text-zinc-500`}>expectimax</p>
                <p className="mt-1 text-xl font-bold text-zinc-50">{recommendation}</p>
              </div>
              {mode === "auto" && (
                <button
                  type="button"
                  onClick={() => {
                    const willRun = !autoRunning;
                    autoRunningRef.current = willRun;
                    setAutoRunning(willRun);
                    setThinking(willRun);
                    if (willRun) setEvaluation(null);
                  }}
                  disabled={game.gameOver}
                  className={BUTTON}
                >
                  {autoRunning ? "pause" : "resume"}
                </button>
              )}
            </div>

            <div className="mt-5">
              <NumericStepper
                label="search depth"
                value={depth}
                minimum={MIN_SEARCH_DEPTH}
                maximum={MAX_SEARCH_DEPTH}
                step={1}
                unit="ply"
                onChange={updateDepth}
              />
            </div>
            <div className="mt-4">
              <NumericStepper
                label="time limit"
                value={timeLimitSeconds}
                minimum={MIN_TIME_LIMIT_SECONDS}
                step={1}
                unit="seconds"
                onChange={updateTimeLimit}
              />
            </div>

            <dl className="mt-5 space-y-2 border-t border-zinc-800 pt-4">
              <Metric label="completed" value={evaluation ? `${evaluation.depth}/${evaluation.requestedDepth} ply` : "—"} />
              <Metric label="positions" value={evaluation ? formatInteger(evaluation.nodes) : "—"} />
              <Metric label="work" value={evaluation ? formatInteger(evaluation.work) : "—"} />
              <Metric
                label="cache"
                value={evaluation ? `${formatInteger(evaluation.cacheEntries)} · ${formatInteger(evaluation.cacheHits)} hits` : "—"}
              />
              <Metric label="elapsed" value={evaluation ? formatDuration(evaluation.elapsedMs) : "—"} />
              <Metric
                label="status"
                value={
                  thinking
                    ? "working"
                    : searchError
                      ? "error"
                      : evaluation?.complete
                        ? "complete"
                        : evaluation
                          ? "time limit"
                          : "idle"
                }
                accent={evaluation?.complete === true}
              />
            </dl>

            <div className="mt-5 border-t border-zinc-800 pt-4">
              <p className={`${LABEL} text-zinc-500`}>model</p>
              <p className="mt-2 text-xs leading-relaxed text-zinc-400">
                Iterative-deepening expectimax over every legal column, every hidden
                number a reveal could show, and every next disc, with a mirror-aware
                transposition cache. The horizon score is the engine&apos;s
                &ldquo;combined&rdquo; heuristic: chain readiness, cover exposure, height,
                and clog penalties. The recommendation is from the last depth that
                finished inside the budget.
              </p>
            </div>
          </aside>
        )}
      </div>

      <div className="mt-5 flex flex-wrap items-center justify-between gap-3 border-t border-zinc-800 pt-4">
        <div className="flex flex-wrap items-center gap-x-4 gap-y-2">
          <Legend cell={SOLID} label="solid · 2 hits to open" />
          <Legend cell={CRACKED} label="cracked · 1 hit to open" />
          <span className={`${LABEL} text-zinc-500`}>match a run&apos;s length to clear</span>
        </div>
        <div className="flex items-center gap-2">
          <span className={`${LABEL} font-mono text-zinc-600 normal-case tracking-normal`} title="seed of this game">
            game {formatSeed(seed)}
          </span>
          <button type="button" onClick={replayGame} className={BUTTON} title="Start this seed again">
            replay
          </button>
          <button type="button" onClick={newGame} className={BUTTON}>
            new game
          </button>
        </div>
      </div>
      {caption && <p className="mt-3 text-xs leading-relaxed text-zinc-400">{caption}</p>}
    </section>
  );
}

/* ------------------------------------------------------------------------ */

function NumericStepper({
  label,
  value,
  minimum,
  maximum,
  step,
  unit,
  onChange,
}: {
  label: string;
  value: number;
  minimum: number;
  maximum?: number;
  step: number;
  unit: string;
  onChange: (value: number) => number;
}) {
  const [draft, setDraft] = useState(() => String(value));
  const parseDraft = () => {
    if (draft.trim() === "") return null;
    const parsed = Number(draft);
    return Number.isFinite(parsed) ? parsed : null;
  };
  const commitDraft = () => {
    const parsed = parseDraft();
    setDraft(String(parsed === null ? value : onChange(parsed)));
  };
  const takeStep = (direction: -1 | 1) => {
    const base = parseDraft() ?? value;
    setDraft(String(onChange(base + direction * step)));
  };
  const stepButton =
    "border border-zinc-700 text-lg text-zinc-300 transition-colors hover:border-zinc-500 hover:text-zinc-50 disabled:cursor-not-allowed disabled:opacity-35";

  return (
    <>
      <p className={`${LABEL} text-zinc-500`}>{label}</p>
      <div className="mt-2 grid grid-cols-[2.5rem_1fr_2.5rem]" role="group" aria-label={`${label} in ${unit}`}>
        <button
          type="button"
          onClick={() => takeStep(-1)}
          disabled={value <= minimum}
          aria-label={`Decrease ${label}`}
          className={`${stepButton} rounded-l-md border-r-0`}
        >
          −
        </button>
        <input
          type="number"
          min={minimum}
          max={maximum}
          step={step}
          value={draft}
          onChange={(event) => setDraft(event.currentTarget.value)}
          onBlur={commitDraft}
          onKeyDown={(event) => {
            if (event.key !== "Enter") return;
            event.preventDefault();
            event.currentTarget.blur();
          }}
          aria-label={`${label} in ${unit}`}
          className={`min-w-0 border border-zinc-700 bg-zinc-950 px-2 py-2 text-center font-mono text-sm text-zinc-100 outline-none transition-colors focus:border-sky-500 ${styles.numberInput}`}
        />
        <button
          type="button"
          onClick={() => takeStep(1)}
          disabled={maximum !== undefined && value >= maximum}
          aria-label={`Increase ${label}`}
          className={`${stepButton} rounded-r-md border-l-0`}
        >
          +
        </button>
      </div>
      <p className={`${LABEL} mt-1.5 text-right text-zinc-600`}>
        {maximum === undefined ? `${minimum}+ ${unit}` : `${minimum}–${maximum} ${unit}`}
      </p>
    </>
  );
}

function Legend({ cell, label }: { cell: number; label: string }) {
  return (
    <span className="flex items-center gap-1.5">
      <span className="inline-flex size-4">
        <DiscFace cell={cell} />
      </span>
      <span className={`${LABEL} text-zinc-500`}>{label}</span>
    </span>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <p>
      <span className={`${LABEL} block text-zinc-500`}>{label}</span>
      <span className="font-mono text-sm text-zinc-50 tabular-nums">{value}</span>
    </p>
  );
}

function Metric({ label, value, accent = false }: { label: string; value: string; accent?: boolean }) {
  return (
    <div className="flex items-baseline justify-between gap-3">
      <dt className={`${LABEL} text-zinc-500`}>{label}</dt>
      <dd className={`font-mono text-xs tabular-nums ${accent ? "text-sky-400" : "text-zinc-300"}`}>{value}</dd>
    </div>
  );
}

function toLastMove(column: number, result: MoveResult): LastMove {
  return {
    column,
    scoreDelta: result.scoreDelta,
    chainLength: result.waves.length,
    levelAdvanced: result.levelAdvanced,
    clearedBoard: result.clearedBoard,
  };
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function formatDuration(milliseconds: number) {
  if (milliseconds < 10) return "<0.01 s";
  return `${(milliseconds / 1_000).toFixed(2)} s`;
}

function formatSeed(seed: number) {
  return `0x${(seed >>> 0).toString(16).padStart(8, "0")}`;
}

function clamp(value: number, minimum: number, maximum: number) {
  if (!Number.isFinite(value)) return minimum;
  return Math.min(maximum, Math.max(minimum, value));
}

function normalizeTimeLimit(seconds: number) {
  if (!Number.isFinite(seconds)) return MIN_TIME_LIMIT_SECONDS;
  return Math.max(MIN_TIME_LIMIT_SECONDS, Math.trunc(seconds));
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

/**
 * The first disc of a game is drawn by `createGame` from a fresh generator;
 * the runtime generator skips that draw so the rest of the game continues the
 * same stream, which makes a seed fully reproducible.
 */
function runtimeRandom(seed: number): RandomSource {
  const random = seededRandom(seed);
  random();
  return random;
}

function randomSeed() {
  if (typeof crypto !== "undefined" && "getRandomValues" in crypto) {
    const words = new Uint32Array(1);
    crypto.getRandomValues(words);
    return words[0] >>> 0;
  }
  return (Math.random() * 0x1_0000_0000) >>> 0;
}
