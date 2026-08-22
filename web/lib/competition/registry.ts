import {
  COMPETITION_GAMES,
  type CompetitionGameDefinition,
} from "./game.ts";

/*
 * The checked-in catalog keeps every promoted game forever. DynamoDB records
 * carry the key, so historical submissions never silently switch futures.
 */
const GAME_REGISTRY = new Map<string, CompetitionGameDefinition>(
  COMPETITION_GAMES.map((game) => [game.gameKey, game]),
);

export function getCompetitionGame(gameKey: string) {
  return GAME_REGISTRY.get(gameKey) ?? null;
}

export function listCompetitionGames() {
  return [...COMPETITION_GAMES];
}
