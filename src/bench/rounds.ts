import { BOARD_SIZE, type DiscValue } from "../core/typescript/engine.ts";

/**
 * A scripted round is a fully predetermined Drop7 episode: the visible disc
 * sequence and the hidden value under every gray disc are fixed ahead of time.
 * Every policy benched on a round sees the exact same sequence of discs, and a
 * gray disc always holds the same predetermined value, so two policies can be
 * compared on identical randomness.
 *
 * This is a public playground format, not a research seed cohort: no tier
 * evidence (SCREEN/STANDARD/QUALIFY/...) may be claimed from scripted rounds.
 */
export interface ScriptedRound {
  format: "drop7-scripted-round-v1";
  /** Stable identifier, e.g. "gauntlet-01". */
  id: string;
  /** Human-facing name. */
  name: string;
  /** Provenance only: the generator seed that produced the tapes. */
  generatorSeedHex: string;
  /** Move cap; a game reaching it is censored. */
  maximumMoves: number;
  /**
   * discs[m] is the visible next disc at move m (discs[0] opens the game).
   * Indexed by absolute move number, so every policy sees the same disc
   * sequence no matter how it plays.
   */
  discs: DiscValue[];
  /**
   * latentRows[r][c] is the predetermined hidden value of the covered cell in
   * column c of covered-row generation r. Generation 0 is the initial bottom
   * row; generation r is added by the r-th row rise (which always happens
   * after move 5r, for every policy).
   */
  latentRows: DiscValue[][];
}

export function validateScriptedRound(value: unknown): ScriptedRound {
  const round = value as ScriptedRound;
  const fail = (reason: string): never => {
    throw new Error(`Invalid scripted round: ${reason}`);
  };
  if (!round || typeof round !== "object") fail("not an object");
  if (round.format !== "drop7-scripted-round-v1") fail("bad format");
  if (typeof round.id !== "string" || !/^[a-z0-9][a-z0-9-]*$/.test(round.id)) {
    fail("bad id");
  }
  if (typeof round.name !== "string" || round.name.length === 0) fail("bad name");
  if (typeof round.generatorSeedHex !== "string") fail("bad generatorSeedHex");
  if (!Number.isInteger(round.maximumMoves) || round.maximumMoves < 1) {
    fail("bad maximumMoves");
  }
  const isDisc = (disc: unknown): disc is DiscValue =>
    Number.isInteger(disc) && (disc as number) >= 1 && (disc as number) <= BOARD_SIZE;
  if (!Array.isArray(round.discs) || round.discs.length < round.maximumMoves) {
    fail("disc tape shorter than the move cap");
  }
  if (!round.discs.every(isDisc)) fail("disc tape holds a non-disc value");
  const minimumRows = Math.ceil(round.maximumMoves / 5) + 1;
  if (!Array.isArray(round.latentRows) || round.latentRows.length < minimumRows) {
    fail(`latent rows shorter than ${minimumRows}`);
  }
  for (const row of round.latentRows) {
    if (!Array.isArray(row) || row.length !== BOARD_SIZE || !row.every(isDisc)) {
      fail("a latent row is not exactly seven disc values");
    }
  }
  return round;
}

/** The eight well-known scripted rounds shipped with the repository. */
export const STANDARD_ROUND_IDS = [
  "gauntlet-01",
  "gauntlet-02",
  "gauntlet-03",
  "gauntlet-04",
  "gauntlet-05",
  "gauntlet-06",
  "gauntlet-07",
  "gauntlet-08",
] as const;
