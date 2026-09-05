"use client";

import { createContext, useContext, useEffect, useRef, useState, useSyncExternalStore, type ReactNode } from "react";
import "./board-animation.css";

const subscribeMotion = (notify: () => void) => {
  const media = window.matchMedia("(prefers-reduced-motion: reduce)");
  media.addEventListener("change", notify);
  return () => media.removeEventListener("change", notify);
};
const getMotion = () => window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const serverMotion = () => true;

interface PlaybackState { index: number; cycle: number; playing: boolean; reducedMotion: boolean; stepMs: number }
const PlaybackContext = createContext<PlaybackState | null>(null);

/** One clock for a figure's boards, labels and diagrams. Pauses offscreen and in hidden tabs. */
export function Playback({ children, length, label, stepMs = 1000 }: {
  children: ReactNode; length: number; label: string; stepMs?: number;
}) {
  const host = useRef<HTMLDivElement>(null);
  const [tick, setTick] = useState(0);
  const [paused, setPaused] = useState(false);
  const [stepped, setStepped] = useState(false);
  const [visible, setVisible] = useState(false);
  const reducedMotion = useSyncExternalStore(subscribeMotion, getMotion, serverMotion);
  const remaining = useRef({ tick: 0, ms: stepMs });
  const playing = !paused && visible && !reducedMotion;

  useEffect(() => {
    let inView = false;
    const update = () => setVisible(inView && !document.hidden);
    const observer = new IntersectionObserver(([entry]) => { inView = entry.isIntersecting; update(); });
    if (host.current) observer.observe(host.current);
    document.addEventListener("visibilitychange", update);
    return () => { observer.disconnect(); document.removeEventListener("visibilitychange", update); };
  }, []);

  useEffect(() => {
    if (!playing) return;
    if (remaining.current.tick !== tick) remaining.current = { tick, ms: stepMs };
    const start = performance.now();
    let fired = false;
    const timer = window.setTimeout(() => {
      fired = true;
      setTick((value) => value + 1);
    }, remaining.current.ms);
    return () => {
      window.clearTimeout(timer);
      if (!fired) remaining.current.ms = Math.max(0, remaining.current.ms - (performance.now() - start));
    };
  }, [playing, stepMs, tick]);

  const move = (next: number) => {
    setTick(next);
  };
  return (
    <div ref={host} className="d7-playback" data-playing={playing} data-reduced-motion={reducedMotion} data-stepped={stepped}>
      <div className="d7-playback-toolbar">
        <span className="d7-playback-label"><span className="d7-live-dot" />{label}</span>
        <div className="d7-controls" role="group" aria-label={`${label} animation controls`}>
          {!reducedMotion && <button type="button" onClick={() => { setStepped(false); setPaused(!paused); }}>{paused ? "Play" : "Pause"}</button>}
          <button type="button" onClick={() => { setStepped(true); setPaused(true); move(tick + 1); }}>Next step</button>
          <button type="button" onClick={() => { move((Math.floor(tick / length) + 1) * length); setStepped(false); setPaused(false); }}>Replay</button>
        </div>
      </div>
      <PlaybackContext.Provider value={{ index: tick % length, cycle: Math.floor(tick / length), playing, reducedMotion, stepMs }}>
        {children}
      </PlaybackContext.Provider>
    </div>
  );
}

export function usePlayback() {
  const value = useContext(PlaybackContext);
  if (!value) throw new Error("An animated figure must be inside Playback");
  return value;
}
