"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { scoreForWave } from "../../src/core/typescript/engine.ts";
import type { ExplosionPoint } from "./Drop7Board";

interface ExplosionFrame {
  kind: string;
  indexes: readonly number[];
  chainDepth?: number;
}

const POINT_LIFETIME_MS = 900;

/** Keeps score labels alive while the board advances to later animation frames. */
export function useExplosionPoints(enabled = true) {
  const [points, setPoints] = useState<ExplosionPoint[]>([]);
  const nextIdRef = useRef(0);
  const timersRef = useRef(new Set<number>());

  const clearExplosionPoints = useCallback(() => {
    for (const timer of timersRef.current) window.clearTimeout(timer);
    timersRef.current.clear();
    setPoints([]);
  }, []);

  const captureExplosionFrame = useCallback(
    (frame: ExplosionFrame) => {
      if (!enabled || frame.kind !== "burst" || frame.indexes.length === 0) return;

      const pointsPerDisc = scoreForWave(frame.chainDepth ?? 1);
      const additions = frame.indexes.map((index) => ({
        id: nextIdRef.current++,
        index,
        points: pointsPerDisc,
      }));
      const ids = new Set(additions.map((point) => point.id));
      setPoints((current) => [...current, ...additions]);

      const timer = window.setTimeout(() => {
        timersRef.current.delete(timer);
        setPoints((current) =>
          current.filter(
            (point) => typeof point.id !== "number" || !ids.has(point.id),
          ),
        );
      }, POINT_LIFETIME_MS);
      timersRef.current.add(timer);
    },
    [enabled],
  );

  useEffect(
    () => () => {
      for (const timer of timersRef.current) window.clearTimeout(timer);
      timersRef.current.clear();
    },
    [],
  );

  return {
    explosionPoints: enabled ? points : [],
    captureExplosionFrame,
    clearExplosionPoints,
  };
}
