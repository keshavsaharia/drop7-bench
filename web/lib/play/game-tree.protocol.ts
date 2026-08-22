import type { GameState } from "../../../src/core/typescript/engine.ts";
import type { GameTree } from "./game-tree.ts";

export interface GameTreeRequest {
  requestId: number;
  seed: number;
  moves: number;
  /** When given, build below this state instead of reproducing the seeded position. */
  root?: GameState;
  leafDepth: number;
  maxOutcomes: number;
}

export type GameTreeResponse =
  | { type: "tree"; requestId: number; tree: GameTree; elapsedMs: number }
  | { type: "error"; requestId: number; message: string };
