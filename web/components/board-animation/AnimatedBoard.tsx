"use client";

import type { CSSProperties } from "react";
import type { BoardClip } from "@/lib/board-animation";
import { Drop7Board } from "../Drop7Board";
import { usePlayback } from "./Playback";
import { BoardRunGuides } from "./BoardRunGuides";

/** Engine snapshots supply the action, score and exact gravity distance. */
export function AnimatedBoard({ clip, showStatus = true, label }: { clip: BoardClip; showStatus?: boolean; label?: string }) {
  const playback = usePlayback();
  const index = Math.min(playback.index, clip.frames.length - 1);
  const frame = clip.frames[index];
  const key = `${playback.cycle}-${index}`;
  const changed = new Set(frame.indexes);
  return (
    <div className="d7-animated-board" data-frame={frame.kind} data-score={frame.score}>
      <div key={key} className="d7-board-motion" style={{ "--d7-step-ms": `${playback.stepMs}ms` } as CSSProperties}>
        <Drop7Board
          cells={frame.board}
          nextDisc={frame.kind === "ready" ? clip.nextDisc : null}
          dropColumn={clip.column}
          showColumnLabels
          animateOverflow
          size="100%"
          label={label ?? `Column ${clip.column + 1}. ${frame.label} ${frame.score} points so far.`}
          highlight={frame.kind === "match" ? frame.indexes : []}
          overlay={frame.runs && <BoardRunGuides runs={frame.runs} />}
          cellClassName={(cellIndex) => {
            if (frame.travel[cellIndex] !== undefined) return `d7-disc-travel${frame.kind === "drop" ? " d7-disc-entry" : ""}`;
            if (changed.has(cellIndex) && frame.kind === "burst") return "d7-disc-burst";
            if (changed.has(cellIndex) && frame.kind === "impact") return "d7-disc-impact";
            return undefined;
          }}
          cellStyle={(cellIndex) => ({ "--d7-travel": frame.travel[cellIndex] ?? 0 } as CSSProperties)}
          explosionPoints={frame.kind === "burst" ? frame.indexes.map((cellIndex) => ({
            id: `${key}-${cellIndex}`, index: cellIndex, points: frame.points / frame.indexes.length,
          })) : []}
        />
      </div>
      {showStatus && <div className="d7-board-status"><span>{frame.label}</span><span className="d7-score" aria-label={`${frame.score} points so far`}>+{frame.score}</span></div>}
    </div>
  );
}
