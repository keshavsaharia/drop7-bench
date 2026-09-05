import type { BoardRun } from "@/lib/board-animation";

/** A board overlay showing the full touching row or column behind a match. */
export function BoardRunGuides({ runs }: { runs: readonly BoardRun[] }) {
  return <div className="d7-run-guides" aria-hidden="true"><svg viewBox="0 0 700 700">
    {runs.map((run) => <rect key={`${run.start}-${run.end}`}
      x={(run.start % 7) * 100 + 4} y={Math.floor(run.start / 7) * 100 + 4}
      width={((run.end % 7) - (run.start % 7) + 1) * 100 - 8}
      height={(Math.floor(run.end / 7) - Math.floor(run.start / 7) + 1) * 100 - 8}
      rx="12" />)}
  </svg></div>;
}
