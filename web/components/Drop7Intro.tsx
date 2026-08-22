"use client";

/**
 * Visual introduction to Drop7 for the rules page: a live board with a compact
 * score and rise clock, but no solver readout or game controls. A depth-3 expectimax with a one-second
 * budget chooses each drop unless the reader hovers the board and clicks a
 * column. Same engine, board, animation frames, and worker as `Drop7Game`.
 */

import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createGame,
  legalColumns,
  playMove,
  seededRandom,
  type GameState,
  type MoveAnimationFrame,
  type RandomSource,
} from "../../src/core/typescript/engine.ts";
import type { SolverRequest, SolverResponse } from "@/lib/play/solver.protocol";
import { Drop7Board } from "./Drop7Board";
import styles from "./Drop7Game.module.css";
import { useExplosionPoints } from "./useExplosionPoints";

const INITIAL_SEED = 0xd7070017;
const SEARCH_DEPTH = 3;
const SEARCH_TIME_MS = 1_000;
const AUTO_MOVE_DELAY_MS = 700;
const RESTART_DELAY_MS = 1_600;
const FRAME_DURATION_MS = {
  drop: 380,
  burst: 175,
  impact: 130,
  settle: 210,
  rise: 280,
} satisfies Record<MoveAnimationFrame["kind"], number>;

export function Drop7Intro({
  maxDepth = SEARCH_DEPTH,
  timeLimitMs = SEARCH_TIME_MS,
}: {
  maxDepth?: number;
  timeLimitMs?: number;
}) {
  const randomRef = useRef<RandomSource>(runtimeRandom(INITIAL_SEED));
  const [game, setGame] = useState<GameState>(() => createGame(seededRandom(INITIAL_SEED)));
  const gameRef = useRef(game);
  const hostRef = useRef<HTMLElement>(null);
  const [active, setActive] = useState(false);
  const [animating, setAnimating] = useState(false);
  const animatingRef = useRef(false);
  const [animationFrame, setAnimationFrame] = useState<MoveAnimationFrame | null>(null);
  const animationRunRef = useRef(0);
  const {
    explosionPoints,
    captureExplosionFrame,
    clearExplosionPoints,
  } = useExplosionPoints();
  const [autoColumn, setAutoColumn] = useState<number | null>(null);
  const [hoverColumn, setHoverColumn] = useState<number | null>(null);

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

  useEffect(() => {
    if (active) return;
    const host = hostRef.current;
    if (!host || typeof IntersectionObserver === "undefined") {
      setActive(true);
      return;
    }
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (!entry.isIntersecting) return;
        setActive(true);
        observer.disconnect();
      },
      { rootMargin: "240px" },
    );
    observer.observe(host);
    return () => observer.disconnect();
  }, [active]);

  const startGame = useCallback((nextSeed: number) => {
    animationRunRef.current += 1;
    animatingRef.current = false;
    randomRef.current = runtimeRandom(nextSeed);
    const next = createGame(seededRandom(nextSeed));
    gameRef.current = next;
    setGame(next);
    setAnimationFrame(null);
    clearExplosionPoints();
    setAnimating(false);
    setAutoColumn(null);
  }, [clearExplosionPoints]);

  const commitMove = useCallback((column: number, expectedMove?: number) => {
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
    clearExplosionPoints();
    setAutoColumn(null);

    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const frames = reducedMotion ? [] : result.animation;

    const finishMove = () => {
      if (animationRunRef.current !== run) return;
      gameRef.current = result.state;
      animatingRef.current = false;
      setGame(result.state);
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
  }, [captureExplosionFrame, clearExplosionPoints]);

  useEffect(() => {
    if (!active || animating || game.gameOver) return;

    let cancelled = false;
    let worker: Worker | undefined;
    const searchTimer = window.setTimeout(() => {
      try {
        worker = new Worker(new URL("../lib/play/solver.worker.ts", import.meta.url), {
          type: "module",
          name: "drop7-intro",
        });
      } catch {
        if (!cancelled) {
          const legal = legalColumns(game.board);
          setAutoColumn(legal[0] ?? null);
        }
        return;
      }
      const request: SolverRequest = {
        state: game,
        maxDepth,
        timeLimitMs,
      };
      worker.addEventListener("message", (event: MessageEvent<SolverResponse>) => {
        if (cancelled) return;
        if (event.data.type === "progress") return;
        worker?.terminate();
        worker = undefined;
        if (event.data.type === "error") {
          const legal = legalColumns(game.board);
          setAutoColumn(legal[0] ?? null);
          return;
        }
        setAutoColumn(event.data.result.bestColumn);
      });
      worker.addEventListener("error", () => {
        if (cancelled) return;
        worker?.terminate();
        worker = undefined;
        const legal = legalColumns(game.board);
        setAutoColumn(legal[0] ?? null);
      });
      worker.postMessage(request);
    }, 30);

    return () => {
      cancelled = true;
      window.clearTimeout(searchTimer);
      worker?.terminate();
    };
  }, [active, animating, game, maxDepth, timeLimitMs]);

  useEffect(() => {
    if (animating || game.gameOver || autoColumn === null || hoverColumn !== null) return;
    const timer = window.setTimeout(() => {
      commitMove(autoColumn, game.movesPlayed);
    }, AUTO_MOVE_DELAY_MS);
    return () => window.clearTimeout(timer);
  }, [animating, autoColumn, commitMove, game.gameOver, game.movesPlayed, hoverColumn]);

  useEffect(() => {
    if (!game.gameOver || animating) return;
    const timer = window.setTimeout(() => startGame(randomSeed()), RESTART_DELAY_MS);
    return () => window.clearTimeout(timer);
  }, [animating, game.gameOver, startGame]);

  const legal = useMemo(() => new Set(legalColumns(game.board)), [game.board]);
  const displayBoard = animationFrame?.board ?? game.board;
  const animatedIndexes = useMemo(() => new Set(animationFrame?.indexes ?? []), [animationFrame]);
  const aimColumn = hoverColumn ?? autoColumn;
  const previewColumn = animating || game.gameOver ? undefined : aimColumn;
  const risePending = animating && game.movesRemaining === 1;
  const completedRiseDots = risePending
    ? MOVES_PER_LEVEL
    : MOVES_PER_LEVEL - game.movesRemaining;

  const cellMotion = (index: number) =>
    animatedIndexes.has(index) && animationFrame ? styles[animationFrame.kind] : undefined;
  const cellStyle = (index: number) =>
    ({ "--drop7-rows": Math.floor(index / BOARD_SIZE) + 1 }) as CSSProperties;

  return (
    <figure
      ref={hostRef}
      className="drop7-intro"
      aria-label="Live Drop7 game. A search chooses each column unless you click one."
      aria-busy={animating}
    >
      <div className="drop7-intro-board">
        <div className="drop7-intro-shell">
          <div className="drop7-intro-hud">
            <p className="drop7-intro-score">
              <span>score</span>
              <strong aria-live="polite">{formatInteger(game.score)}</strong>
            </p>
            <div
              className="drop7-intro-rise"
              aria-label={
                risePending
                  ? "Board rise in progress"
                  : `Board rises after ${game.movesRemaining} more drops`
              }
            >
              <span>rise</span>
              <span className="drop7-intro-rise-dots" aria-hidden="true">
                {Array.from({ length: MOVES_PER_LEVEL }, (_, index) => (
                  <span
                    key={index}
                    className={`drop7-intro-rise-dot${
                      index < completedRiseDots ? " is-spent" : ""
                    }${risePending ? " is-imminent" : ""}`}
                  />
                ))}
              </span>
            </div>
          </div>
          <Drop7Board
            cells={displayBoard}
            nextDisc={animating || game.gameOver ? null : game.nextDisc}
            dropColumn={previewColumn}
            size="100%"
            cellClassName={cellMotion}
            cellStyle={cellStyle}
            explosionPoints={explosionPoints}
            label="A live Drop7 board. Click a column to drop the next disc."
            overlay={
              <>
                <div
                  className="drop7-intro-columns"
                  onPointerLeave={() => setHoverColumn(null)}
                >
                  {Array.from({ length: BOARD_SIZE }, (_, column) => {
                    const aiming = aimColumn === column && !animating && !game.gameOver;
                    const disabled = game.gameOver || animating || !legal.has(column);
                    return (
                      <button
                        key={column}
                        type="button"
                        disabled={disabled}
                        className={`drop7-intro-col${aiming ? " is-aim" : ""}`}
                        onPointerEnter={() => {
                          if (!legal.has(column) || animating || game.gameOver) return;
                          setHoverColumn(column);
                        }}
                        onClick={() => commitMove(column)}
                        aria-label={
                          legal.has(column)
                            ? `Drop ${game.nextDisc} in column ${column + 1}`
                            : `Column ${column + 1} is full`
                        }
                      />
                    );
                  })}
                </div>
                {game.gameOver && (
                  <div className="drop7-intro-ended" aria-live="polite">
                    the board overflowed
                  </div>
                )}
              </>
            }
          />
        </div>
      </div>
      <figcaption>
        Watch a game, or hover the board and click a column to drop the next disc.
      </figcaption>
    </figure>
  );
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function wait(milliseconds: number) {
  return new Promise<void>((resolve) => window.setTimeout(resolve, milliseconds));
}

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
