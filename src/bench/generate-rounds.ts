/**
 * Generates the checked-in scripted rounds under src/bench/rounds/.
 *
 * The generator seeds live in their own 0x5eed**** domain, which does not
 * overlap any historical or reserved research seed range (see
 * research/seeds/leases/). A scripted round is a fixed test vector: the discs
 * and latent rows are materialized here, so policies never touch the
 * generator seed and no research seed range is consumed by benchmarking.
 *
 * Run: node --experimental-strip-types src/bench/generate-rounds.ts
 */
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  BOARD_SIZE,
  seededRandom,
  type DiscValue,
} from "../core/typescript/engine.ts";
import {
  STANDARD_ROUND_IDS,
  validateScriptedRound,
  type ScriptedRound,
} from "./rounds.ts";

const MAXIMUM_MOVES = 2_000;
const ROUNDS_DIR = join(dirname(fileURLToPath(import.meta.url)), "rounds");

function generateRound(id: string, index: number): ScriptedRound {
  const generatorSeed = 0x5eed_0000 + index;
  const random = seededRandom(generatorSeed);
  const disc = () =>
    (Math.floor(random() * BOARD_SIZE) + 1) as DiscValue;
  const discs = Array.from({ length: MAXIMUM_MOVES }, disc);
  // One initial covered row plus one per possible rise, with margin.
  const latentRows = Array.from({ length: MAXIMUM_MOVES / 5 + 20 }, () =>
    Array.from({ length: BOARD_SIZE }, disc),
  );
  return validateScriptedRound({
    format: "drop7-scripted-round-v1",
    id,
    name: `Gauntlet ${String(index).padStart(2, "0")}`,
    generatorSeedHex: `0x${generatorSeed.toString(16)}`,
    maximumMoves: MAXIMUM_MOVES,
    discs,
    latentRows,
  });
}

mkdirSync(ROUNDS_DIR, { recursive: true });
for (const [index, id] of STANDARD_ROUND_IDS.entries()) {
  const round = generateRound(id, index + 1);
  const path = join(ROUNDS_DIR, `${id}.json`);
  writeFileSync(path, `${JSON.stringify(round)}\n`);
  console.log(`wrote ${path}`);
}
