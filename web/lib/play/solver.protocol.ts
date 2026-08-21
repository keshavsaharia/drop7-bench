import type { GameState } from "../../../src/core/typescript/engine.ts";
import type { EvaluationResult } from "../../../src/core/typescript/solver.ts";

export interface SolverRequest {
  state: GameState;
  maxDepth: number;
  timeLimitMs: number;
}

export type SolverResponse =
  | { type: "progress"; result: EvaluationResult }
  | { type: "result"; result: EvaluationResult }
  | { type: "error"; message: string };
