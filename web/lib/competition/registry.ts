import {
  COMPETITION_GAME,
  COMPETITION_GAME_KEY,
  COMPETITION_ROUND,
  type CompetitionGameManifest,
} from "./game.ts";
import type { ScriptedRound } from "../../../src/bench/rounds.ts";

export interface CompetitionGameDefinition {
  manifest: CompetitionGameManifest;
  round: ScriptedRound;
}

/*
 * Add every promoted game here and keep old entries forever. DynamoDB records
 * carry the key, so historical submissions never silently switch futures.
 */
const GAME_REGISTRY = new Map<string, CompetitionGameDefinition>([
  [
    COMPETITION_GAME_KEY,
    { manifest: COMPETITION_GAME, round: COMPETITION_ROUND },
  ],
]);

export function getCompetitionGame(gameKey: string) {
  return GAME_REGISTRY.get(gameKey) ?? null;
}
