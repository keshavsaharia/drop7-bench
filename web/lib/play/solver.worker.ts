/**
 * Web Worker entry for the in-browser solver. One request evaluates one
 * position with iterative deepening, posting each completed depth as progress
 * so the page can show the best-so-far recommendation before the budget ends.
 */
import { fastEvaluateMoves } from "./fast-search.ts";
import type { SolverRequest, SolverResponse } from "./solver.protocol.ts";

// The DOM library types `self` as Window; this module is a dedicated worker.
const workerScope = self as unknown as Pick<Worker, "addEventListener" | "postMessage">;

workerScope.addEventListener("message", (event: MessageEvent<SolverRequest>) => {
  let response: SolverResponse;
  try {
    response = {
      type: "result",
      result: fastEvaluateMoves(event.data.state, {
        maxDepth: event.data.maxDepth,
        timeLimitMs: event.data.timeLimitMs,
        onDepthComplete: (result) => {
          workerScope.postMessage({ type: "progress", result } satisfies SolverResponse);
        },
      }),
    };
  } catch (error) {
    response = {
      type: "error",
      message: error instanceof Error ? error.message : "Search failed",
    };
  }
  workerScope.postMessage(response);
});
