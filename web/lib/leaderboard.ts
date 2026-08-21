import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { DATA_DIR } from "./repo";

export interface LeaderboardGame {
  policyId: string;
  roundId: string;
  score: number;
  moves: number;
  censored: boolean;
  maxChain: number;
  discsCleared: number;
  coveredRevealed: number;
  illegalMoves: number;
  elapsedMs: number;
  checksum: string;
}

export interface LeaderboardSummary {
  policyId: string;
  games: number;
  meanScore: number | null;
  medianScore: number | null;
  minimumScore: number | null;
  maximumScore: number | null;
  meanMoves: number | null;
  censoredGames: number;
  illegalMoves: number;
  meanClearsPerMove: number;
  meanRevealsPerMove: number;
  maxChain: number;
  elapsedMs: number;
}

export interface LeaderboardData {
  format: string;
  generatedAt: string;
  moveCap: number | null;
  rounds: { id: string; name: string; generatorSeedHex: string }[];
  policies: {
    id: string;
    name: string;
    family: string;
    description: string;
    publicInformation: boolean;
  }[];
  games: LeaderboardGame[];
  summaries: LeaderboardSummary[];
}

export interface ReplayFrame {
  move: number;
  disc: number;
  column: number;
  scoreDelta: number;
  score: number;
  board: string;
  nextDisc: number | null;
  movesRemaining: number;
  chainDepth: number;
  cleared: number;
  revealed: number;
  levelAdvanced: boolean;
}

export interface ReplayData extends LeaderboardGame {
  frames: ReplayFrame[];
}

export function loadLeaderboard(): LeaderboardData | null {
  const path = join(DATA_DIR, "leaderboard.json");
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8")) as LeaderboardData;
}

export function loadReplay(policyId: string, roundId: string): ReplayData | null {
  const safe = /^[a-z0-9][a-z0-9-]*$/;
  if (!safe.test(policyId) || !safe.test(roundId)) return null;
  const path = join(DATA_DIR, "replays", `${policyId}--${roundId}.json`);
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8")) as ReplayData;
}
