/**
 * Builds a game tree off the main thread so a reveal-heavy position (a
 * million-outcome chance node) never freezes the page. One request at a time
 * per worker; the component terminates and recreates it to cancel.
 */
import { buildGameTree, positionFromSeed } from "./game-tree.ts";
import type { GameTreeRequest, GameTreeResponse } from "./game-tree.protocol.ts";

const workerScope = self as unknown as Pick<Worker, "addEventListener" | "postMessage">;

workerScope.addEventListener("message", (event: MessageEvent<GameTreeRequest>) => {
  const request = event.data;
  let response: GameTreeResponse;
  try {
    const root = request.root ?? positionFromSeed(request.seed, request.moves);
    const started = Date.now();
    const tree = buildGameTree(root, request.leafDepth, request.maxOutcomes);
    response = { type: "tree", requestId: request.requestId, tree, elapsedMs: Date.now() - started };
  } catch (error) {
    response = { type: "error", requestId: request.requestId, message: error instanceof Error ? error.message : String(error) };
  }
  workerScope.postMessage(response);
});
